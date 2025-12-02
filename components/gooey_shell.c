#include "gooey_shell.h"
#include "gooey_shell_core.h"
#include "gooey_shell_tiling.h"
#include "gooey_shell_config.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <GLPS/glps_thread.h>
#include <sys/select.h>
#include <pwd.h>
#include <sys/stat.h>
PrecomputedAtoms atoms;
Window *opened_windows = NULL;
int opened_windows_count = 0;
int opened_windows_capacity = 0;
volatile int dbus_thread_running = 0;
gthread_t dbus_thread;
gthread_mutex_t dbus_mutex;
gthread_mutex_t window_list_mutex;
int window_list_update_pending = 0;
int pending_x_flush = 0;
void LogError(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
}
void LogInfo(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    printf("[INFO] ");
    vprintf(message, args);
    printf("\n");
    va_end(args);
}
void SafeXFree(void *data)
{
    if (data)
        XFree(data);
}
void ScheduleWindowListUpdate(GooeyShellState *state)
{
    if (!window_list_update_pending)
    {
        window_list_update_pending = 1;
        window_list_update_pending = 0;
    }
}
void OptimizedXFlush(GooeyShellState *state)
{
    if (!pending_x_flush)
    {
        pending_x_flush = 1;
        XFlush(state->display);
        pending_x_flush = 0;
    }
}
void ProcessDBusMessage(GooeyShellState *state, DBusMessage *msg)
{
    if (!state || !msg)
        return;
    if (dbus_message_is_method_call(msg, "dev.binaryink.gshell", "GetWindowList"))
    {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter args, array_iter;
        dbus_message_iter_init_append(reply, &args);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                         DBUS_TYPE_STRING_AS_STRING,
                                         &array_iter);
        glps_thread_mutex_lock(&window_list_mutex);
        for (int i = 0; i < opened_windows_count; i++)
        {
            char window_id[32];
            snprintf(window_id, sizeof(window_id), "%lu", opened_windows[i]);
            const char *window_str = window_id;
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &window_str);
        }
        glps_thread_mutex_unlock(&window_list_mutex);
        dbus_message_iter_close_container(&args, &array_iter);
        dbus_connection_send(state->dbus_connection, reply, NULL);
        dbus_connection_flush(state->dbus_connection);
        dbus_message_unref(reply);
    }
    else if (dbus_message_is_method_call(msg, "dev.binaryink.gshell", "minimize") ||
             dbus_message_is_method_call(msg, "dev.binaryink.gshell", "RestoreWindow") ||
             dbus_message_is_method_call(msg, "dev.binaryink.gshell", "CloseWindow") ||
             dbus_message_is_method_call(msg, "dev.binaryink.gshell", "SendWindowCommand"))
    {
        HandleDBusWindowCommand(state, msg);
    }
}
void *DBusListenerThread(void *arg)
{
    GooeyShellState *state = (GooeyShellState *)arg;
    if (!state || !state->dbus_connection)
    {
        return NULL;
    }
    LogInfo("DBus listener thread started");
    while (dbus_thread_running)
    {
        glps_thread_mutex_lock(&dbus_mutex);
        int dbus_initialized = state->is_dbus_init;
        glps_thread_mutex_unlock(&dbus_mutex);
        if (!dbus_initialized)
        {
            break;
        }
        if (!dbus_connection_read_write(state->dbus_connection, 10))
        {
            usleep(10000);
            continue;
        }
        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(state->dbus_connection)) != NULL)
        {
            ProcessDBusMessage(state, msg);
            dbus_message_unref(msg);
        }
        usleep(5000);
    }
    LogInfo("DBus listener thread stopped");
    return NULL;
}
void SetupDBUS(GooeyShellState *state)
{
    int ret;
    dbus_error_init(&state->dbus_error);
    state->dbus_connection = dbus_bus_get(DBUS_BUS_SESSION, &state->dbus_error);
    if (dbus_error_is_set(&state->dbus_error))
    {
        LogError("DBus connection error: %s", state->dbus_error.message);
        dbus_error_free(&state->dbus_error);
        return;
    }
    if (!state->dbus_connection)
    {
        LogError("Failed to get DBus connection");
        return;
    }
    ret = dbus_bus_request_name(state->dbus_connection, "dev.binaryink.gshell", DBUS_NAME_FLAG_REPLACE_EXISTING, &state->dbus_error);
    if (dbus_error_is_set(&state->dbus_error))
    {
        LogError("DBus name request error: %s", state->dbus_error.message);
        dbus_error_free(&state->dbus_error);
        return;
    }
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        LogError("Failed to acquire DBus name");
        return;
    }
    dbus_bus_add_match(state->dbus_connection,
                       "type='method_call',interface='dev.binaryink.gshell'",
                       &state->dbus_error);
    if (dbus_error_is_set(&state->dbus_error))
    {
        LogError("DBus match error: %s", state->dbus_error.message);
        dbus_error_free(&state->dbus_error);
        return;
    }
    state->is_dbus_init = true;
    dbus_thread_running = 1;
    if (glps_thread_create(&dbus_thread, NULL, DBusListenerThread, state) != 0)
    {
        LogError("Failed to create DBus thread");
        state->is_dbus_init = false;
    }
}
void SendWindowStateThroughDBus(GooeyShellState *state, Window window, const char *state_str)
{
    if (!state->dbus_connection || !state->is_dbus_init)
    {
        return;
    }
    DBusMessage *message;
    DBusMessageIter args;
    char window_id[64];
    message = dbus_message_new_signal("/dev/binaryink/gshell",
                                      "dev.binaryink.gshell",
                                      "WindowStateChanged");
    if (!message)
    {
        return;
    }
    dbus_message_iter_init_append(message, &args);
    snprintf(window_id, sizeof(window_id), "%lu", window);
    const char *window_str = window_id;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &window_str);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &state_str);
    dbus_connection_send(state->dbus_connection, message, NULL);
    dbus_connection_flush(state->dbus_connection);
    dbus_message_unref(message);
}
void SendWorkspaceChangedThroughDBus(GooeyShellState *state, int old_workspace, int new_workspace)
{
    if (!state->dbus_connection || !state->is_dbus_init)
    {
        return;
    }
    DBusMessage *message;
    DBusMessageIter args;
    message = dbus_message_new_signal("/dev/binaryink/gshell",
                                      "dev.binaryink.gshell",
                                      "WorkspaceChanged");
    if (!message)
    {
        return;
    }
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &old_workspace);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &new_workspace);
    dbus_connection_send(state->dbus_connection, message, NULL);
    dbus_connection_flush(state->dbus_connection);
    dbus_message_unref(message);
}
void HandleDBusWindowCommand(GooeyShellState *state, DBusMessage *msg)
{
    if (!state || !msg)
        return;
    const char *method = dbus_message_get_member(msg);
    if (!method)
        return;
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter))
    {
        return;
    }
    const char *window_id_str = NULL;
    const char *command = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &window_id_str);
        dbus_message_iter_next(&iter);
    }
    if (strcmp(method, "SendWindowCommand") == 0)
    {
        if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&iter, &command);
        }
    }
    if (!window_id_str)
    {
        return;
    }
    Window window = (Window)strtoul(window_id_str, NULL, 10);
    if (window == 0)
    {
        return;
    }
    WindowNode *node = FindWindowNodeByFrame(state, window);
    if (!node)
    {
        return;
    }
    if (strcmp(method, "SendWindowCommand") == 0 && command)
    {
        if (strcmp(command, "minimize") == 0)
        {
            MinimizeWindow(state, node);
        }
        else if (strcmp(command, "restore") == 0)
        {
            RestoreWindow(state, node);
        }
        else if (strcmp(command, "close") == 0)
        {
            CloseWindow(state, node);
        }
        else if (strcmp(command, "toggle_tiling") == 0)
        {
            GooeyShell_ToggleFloating(state, node->client);
        }
        else if (strcmp(command, "focus_next") == 0)
        {
            GooeyShell_FocusNextWindow(state);
        }
        else if (strcmp(command, "focus_previous") == 0)
        {
            GooeyShell_FocusPreviousWindow(state);
        }
    }
    else if (strcmp(method, "minimize") == 0)
    {
        MinimizeWindow(state, node);
    }
    else if (strcmp(method, "RestoreWindow") == 0)
    {
        RestoreWindow(state, node);
    }
    else if (strcmp(method, "CloseWindow") == 0)
    {
        CloseWindow(state, node);
    }
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply)
    {
        dbus_connection_send(state->dbus_connection, reply, NULL);
        dbus_connection_flush(state->dbus_connection);
        dbus_message_unref(reply);
    }
}
int ValidateWindowState(GooeyShellState *state)
{
    if (!state || !state->display)
    {
        LogError("Invalid window state");
        return 0;
    }
    return 1;
}
void AddToOpenedWindows(Window window)
{
    glps_thread_mutex_lock(&window_list_mutex);
    if (opened_windows_count >= opened_windows_capacity)
    {
        int new_capacity = opened_windows_capacity == 0 ? 16 : opened_windows_capacity * 2;
        Window *new_array = realloc(opened_windows, new_capacity * sizeof(Window));
        if (!new_array)
        {
            LogError("Failed to realloc opened_windows");
            glps_thread_mutex_unlock(&window_list_mutex);
            return;
        }
        opened_windows = new_array;
        opened_windows_capacity = new_capacity;
    }
    opened_windows[opened_windows_count++] = window;
    glps_thread_mutex_unlock(&window_list_mutex);
}
void RemoveFromOpenedWindows(Window window)
{
    glps_thread_mutex_lock(&window_list_mutex);
    for (int i = 0; i < opened_windows_count; i++)
    {
        if (opened_windows[i] == window)
        {
            opened_windows[i] = opened_windows[opened_windows_count - 1];
            opened_windows_count--;
            break;
        }
    }
    glps_thread_mutex_unlock(&window_list_mutex);
}
int IsWindowInOpenedWindows(Window window)
{
    glps_thread_mutex_lock(&window_list_mutex);
    int found = 0;
    for (int i = 0; i < opened_windows_count; i++)
    {
        if (opened_windows[i] == window)
        {
            found = 1;
            break;
        }
    }
    glps_thread_mutex_unlock(&window_list_mutex);
    return found;
}
void InitializeAtoms(Display *display)
{
    atoms.net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    atoms.utf8_string = XInternAtom(display, "UTF8_STRING", False);
    atoms.wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    atoms.wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    atoms.net_wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    atoms.net_wm_window_type_desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    atoms.net_wm_window_type_dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atoms.net_wm_window_type_normal = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    atoms.net_wm_window_type_toolbar = XInternAtom(display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    atoms.net_wm_window_type_menu = XInternAtom(display, "_NET_WM_WINDOW_TYPE_MENU", False);
    atoms.net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    atoms.net_wm_state_below = XInternAtom(display, "_NET_WM_STATE_BELOW", False);
    atoms.net_wm_state_above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    atoms.net_wm_state_fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    atoms.net_wm_state_skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    atoms.net_wm_state_skip_pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False);
    atoms.net_wm_state_sticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False);
    atoms.net_wm_state_hidden = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);
    atoms.gooey_fullscreen_app = XInternAtom(display, "GOOEY_FULLSCREEN_APP", False);
    atoms.gooey_stay_on_top = XInternAtom(display, "GOOEY_STAY_ON_TOP", False);
    atoms.gooey_desktop_app = XInternAtom(display, "GOOEY_DESKTOP_APP", False);
    atoms.net_wm_window_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
}
int InitializeMultiMonitor(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return 0;
    int event_base, error_base;
    if (!XRRQueryExtension(state->display, &event_base, &error_base))
    {
        state->monitor_info.monitors = malloc(sizeof(Monitor));
        if (!state->monitor_info.monitors)
        {
            LogError("Failed to allocate monitor info");
            return 0;
        }
        state->monitor_info.num_monitors = 1;
        state->monitor_info.primary_monitor = 0;
        state->monitor_info.monitors[0].x = 0;
        state->monitor_info.monitors[0].y = 0;
        state->monitor_info.monitors[0].width = DisplayWidth(state->display, state->screen);
        state->monitor_info.monitors[0].height = DisplayHeight(state->display, state->screen);
        state->monitor_info.monitors[0].number = 0;
        return 1;
    }
    XRRScreenResources *resources = XRRGetScreenResources(state->display, state->root);
    if (!resources)
    {
        LogError("Failed to get screen resources");
        return 0;
    }
    state->monitor_info.monitors = malloc(sizeof(Monitor) * resources->noutput);
    if (!state->monitor_info.monitors)
    {
        XRRFreeScreenResources(resources);
        LogError("Failed to allocate monitors");
        return 0;
    }
    state->monitor_info.num_monitors = 0;
    state->monitor_info.primary_monitor = 0;
    for (int i = 0; i < resources->noutput; i++)
    {
        XRROutputInfo *output_info = XRRGetOutputInfo(state->display, resources, resources->outputs[i]);
        if (!output_info || output_info->connection != RR_Connected)
        {
            if (output_info)
                XRRFreeOutputInfo(output_info);
            continue;
        }
        if (output_info->crtc)
        {
            XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(state->display, resources, output_info->crtc);
            if (crtc_info && crtc_info->width > 0 && crtc_info->height > 0)
            {
                Monitor *mon = &state->monitor_info.monitors[state->monitor_info.num_monitors];
                mon->x = crtc_info->x;
                mon->y = crtc_info->y;
                mon->width = crtc_info->width;
                mon->height = crtc_info->height;
                mon->number = state->monitor_info.num_monitors;
                state->monitor_info.num_monitors++;
            }
            if (crtc_info)
                XRRFreeCrtcInfo(crtc_info);
        }
        XRRFreeOutputInfo(output_info);
    }
    XRRFreeScreenResources(resources);
    if (state->monitor_info.num_monitors == 0)
    {
        free(state->monitor_info.monitors);
        state->monitor_info.monitors = malloc(sizeof(Monitor));
        if (!state->monitor_info.monitors)
        {
            LogError("Failed to allocate fallback monitor");
            return 0;
        }
        state->monitor_info.num_monitors = 1;
        state->monitor_info.primary_monitor = 0;
        state->monitor_info.monitors[0].x = 0;
        state->monitor_info.monitors[0].y = 0;
        state->monitor_info.monitors[0].width = DisplayWidth(state->display, state->screen);
        state->monitor_info.monitors[0].height = DisplayHeight(state->display, state->screen);
        state->monitor_info.monitors[0].number = 0;
    }
    return 1;
}
void FreeMultiMonitor(GooeyShellState *state)
{
    if (state->monitor_info.monitors)
    {
        free(state->monitor_info.monitors);
        state->monitor_info.monitors = NULL;
    }
    state->monitor_info.num_monitors = 0;
}
int GetMonitorForWindow(GooeyShellState *state, int x, int y, int width, int height)
{
    if (!state->monitor_info.monitors || state->monitor_info.num_monitors == 0)
        return 0;
    int window_center_x = x + width / 2;
    int window_center_y = y + height / 2;
    for (int i = 0; i < state->monitor_info.num_monitors; i++)
    {
        Monitor *mon = &state->monitor_info.monitors[i];
        if (window_center_x >= mon->x && window_center_x < mon->x + mon->width &&
            window_center_y >= mon->y && window_center_y < mon->y + mon->height)
        {
            return i;
        }
    }
    int best_monitor = 0;
    int max_overlap = 0;
    for (int i = 0; i < state->monitor_info.num_monitors; i++)
    {
        Monitor *mon = &state->monitor_info.monitors[i];
        int overlap_x1 = (x > mon->x) ? x : mon->x;
        int overlap_y1 = (y > mon->y) ? y : mon->y;
        int overlap_x2 = ((x + width) < (mon->x + mon->width)) ? (x + width) : (mon->x + mon->width);
        int overlap_y2 = ((y + height) < (mon->y + mon->height)) ? (y + height) : (mon->y + mon->height);
        if (overlap_x2 > overlap_x1 && overlap_y2 > overlap_y1)
        {
            int overlap_area = (overlap_x2 - overlap_x1) * (overlap_y2 - overlap_y1);
            if (overlap_area > max_overlap)
            {
                max_overlap = overlap_area;
                best_monitor = i;
            }
        }
    }
    return best_monitor;
}
int GetCurrentMonitor(GooeyShellState *state)
{
    if (!state || state->monitor_info.num_monitors == 0)
        return 0;
    if (state->focused_window != None)
    {
        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
        if (node && node->monitor_number >= 0 &&
            node->monitor_number < state->monitor_info.num_monitors)
        {
            return node->monitor_number;
        }
    }
    if (state->focused_monitor >= 0 && state->focused_monitor < state->monitor_info.num_monitors)
    {
        return state->focused_monitor;
    }
    return 0;
}
void LaunchDesktopAppsForAllMonitors(GooeyShellState *state)
{
    if (!state || !state->monitor_info.monitors || state->monitor_info.num_monitors == 0)
        return;
    LogInfo("Launching desktop apps for %d monitors", state->monitor_info.num_monitors);
    const char *desktop_app_cmd = "/usr/local/bin/gooeyde_desktop";
    for (int i = 0; i < state->monitor_info.num_monitors; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            setenv("GOOEY_DESKTOP_APP", "1", 1);
            setenv("DISPLAY", DisplayString(state->display), 1);
            char monitor_env[32];
            snprintf(monitor_env, sizeof(monitor_env), "%d", i);
            setenv("GOOEY_MONITOR", monitor_env, 1);
            for (int fd = 3; fd < 1024; fd++)
                close(fd);
            execlp("sh", "sh", "-c", desktop_app_cmd, NULL);
            _exit(1);
        }
        else if (pid > 0)
        {
            LogInfo("Launched desktop app for monitor %d (PID: %d)", i, pid);
        }
        else
        {
            LogError("Failed to fork for desktop app on monitor %d", i);
        }
    }
}
void MoveWindowToMonitor(GooeyShellState *state, WindowNode *node, int monitor_number)
{
    if (!node || monitor_number < 0 || monitor_number >= state->monitor_info.num_monitors)
        return;
    if (node->monitor_number == monitor_number)
        return;
    LogInfo("Moving window '%s' from monitor %d to monitor %d",
            node->title ? node->title : "unknown", node->monitor_number, monitor_number);
    int old_monitor = node->monitor_number;
    int bar_height = monitor_number == 0 ? BAR_HEIGHT : 0;
    node->monitor_number = monitor_number;
    Monitor *mon = &state->monitor_info.monitors[monitor_number];
    if (node->is_floating)
    {
        node->x = mon->x + (mon->width - node->width) / 2;
        node->y = mon->y + bar_height + (mon->height - bar_height - node->height) / 2;
        if (node->x < mon->x)
            node->x = mon->x;
        if (node->y < mon->y + bar_height)
            node->y = mon->y + bar_height;
        if (node->x + node->width > mon->x + mon->width)
            node->x = mon->x + mon->width - node->width;
        if (node->y + node->height > mon->y + mon->height)
            node->y = mon->y + mon->height - node->height;
    }
    UpdateWindowGeometry(state, node);
    Workspace *ws = GetCurrentWorkspace(state);
    if (ws)
    {
        if (old_monitor >= 0 && old_monitor < state->monitor_info.num_monitors)
        {
            ArrangeWindowsTilingOnMonitor(state, ws, old_monitor);
        }
        ArrangeWindowsTilingOnMonitor(state, ws, monitor_number);
    }
}
void SetWindowOpacity(GooeyShellState *state, Window window, float opacity)
{
    if (!ValidateWindowState(state) || atoms.net_wm_window_opacity == None)
        return;
    unsigned long opacity_value = (unsigned long)(0xFFFFFFFFUL * opacity);
    XChangeProperty(state->display, window, atoms.net_wm_window_opacity,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&opacity_value, 1);
}
Atom GetOpacityAtom(GooeyShellState *state)
{
    return atoms.net_wm_window_opacity;
}
void InitializeTransparency(GooeyShellState *state)
{
    if (atoms.net_wm_window_opacity == None)
    {
        LogInfo("Window opacity not supported, transparency disabled");
        state->supports_opacity = False;
    }
    else
    {
        LogInfo("Window opacity supported, enabling transparency");
        state->supports_opacity = True;
    }
}
void InitializeWorkspaces(GooeyShellState *state)
{
    state->workspaces = malloc(sizeof(Workspace));
    if (!state->workspaces)
    {
        LogError("Failed to allocate workspace");
        return;
    }
    state->workspaces->number = 1;
    state->workspaces->layout = LAYOUT_TILING;
    state->workspaces->windows = NULL;
    state->workspaces->next = NULL;
    state->workspaces->master_ratio = 0.6;
    state->workspaces->stack_ratios = NULL;
    state->workspaces->stack_ratios_count = 0;
    state->workspaces->monitor_tiling_roots_count = state->monitor_info.num_monitors;
    state->workspaces->monitor_tiling_roots = calloc(state->monitor_info.num_monitors, sizeof(TilingNode *));
    if (!state->workspaces->monitor_tiling_roots)
    {
        LogError("Failed to allocate monitor tiling roots");
        free(state->workspaces);
        state->workspaces = NULL;
        return;
    }
    state->current_workspace = 1;
    state->current_layout = LAYOUT_TILING;
    state->mod_key = Mod1Mask;
    state->super_pressed = False;
    state->inner_gap = 8;
    state->outer_gap = 8;
}
Workspace *GetCurrentWorkspace(GooeyShellState *state)
{
    Workspace *ws = state->workspaces;
    while (ws)
    {
        if (ws->number == state->current_workspace)
        {
            return ws;
        }
        ws = ws->next;
    }
    return state->workspaces;
}
Workspace *GetWorkspace(GooeyShellState *state, int workspace_number)
{
    Workspace *ws = state->workspaces;
    while (ws)
    {
        if (ws->number == workspace_number)
        {
            return ws;
        }
        ws = ws->next;
    }
    if (workspace_number >= 1 && workspace_number <= 9)
    {
        return CreateWorkspace(state, workspace_number);
    }
    return NULL;
}
Workspace *CreateWorkspace(GooeyShellState *state, int workspace_number)
{
    Workspace *new_ws = malloc(sizeof(Workspace));
    if (!new_ws)
    {
        LogError("Failed to allocate new workspace");
        return NULL;
    }
    new_ws->number = workspace_number;
    new_ws->layout = LAYOUT_TILING;
    new_ws->windows = NULL;
    new_ws->next = NULL;
    new_ws->master_ratio = 0.6;
    new_ws->stack_ratios = NULL;
    new_ws->stack_ratios_count = 0;
    new_ws->monitor_tiling_roots_count = state->monitor_info.num_monitors;
    new_ws->monitor_tiling_roots = calloc(state->monitor_info.num_monitors, sizeof(TilingNode *));
    if (!new_ws->monitor_tiling_roots)
    {
        LogError("Failed to allocate monitor tiling roots for new workspace");
        free(new_ws);
        return NULL;
    }
    if (!state->workspaces)
    {
        state->workspaces = new_ws;
    }
    else
    {
        Workspace *ws = state->workspaces;
        while (ws->next)
        {
            ws = ws->next;
        }
        ws->next = new_ws;
    }
    return new_ws;
}
void AddWindowToWorkspace(GooeyShellState *state, WindowNode *node, int workspace)
{
    Workspace *ws = GetWorkspace(state, workspace);
    if (!ws)
    {
        ws = CreateWorkspace(state, workspace);
        if (!ws)
            return;
    }
    node->workspace = workspace;
    node->next = ws->windows;
    if (ws->windows)
    {
        ws->windows->prev = node;
    }
    node->prev = NULL;
    ws->windows = node;
}
void RemoveWindowFromWorkspace(GooeyShellState *state, WindowNode *node)
{
    Workspace *ws = GetWorkspace(state, node->workspace);
    if (!ws)
        return;
    if (node->prev)
    {
        node->prev->next = node->next;
    }
    else
    {
        ws->windows = node->next;
    }
    if (node->next)
    {
        node->next->prev = node->prev;
    }
    node->prev = NULL;
    node->next = NULL;
}
Cursor CreateCustomCursor(GooeyShellState *state)
{
    Display *display = state->display;
    Cursor cursor = None;
    const char *possible_paths[] = {
        "./assets/cursor.png",
        "~/.gooey_shell/cursor.png",
        "/usr/share/gooey_shell/cursor.png",
        NULL};
    for (int i = 0; possible_paths[i] != NULL; i++)
    {
        char expanded_path[1024];
        const char *path = possible_paths[i];
        if (path[0] == '~' && path[1] == '/')
        {
            const char *home = getenv("HOME");
            if (home)
            {
                snprintf(expanded_path, sizeof(expanded_path), "%s/%s", home, path + 2);
                path = expanded_path;
            }
        }
        if (access(path, R_OK) == 0)
        {
            cursor = XcursorFilenameLoadCursor(display, path);
            if (cursor != None)
            {
                return cursor;
            }
        }
    }
    return None;
}
void FreeWindowNode(WindowNode *node)
{
    if (!node)
        return;
    free(node->title);
    free(node);
}
char *StrDup(const char *str)
{
    if (!str)
        return NULL;
    char *new_str = strdup(str);
    if (!new_str)
    {
        LogError("Failed to duplicate string");
        return NULL;
    }
    return new_str;
}
void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node)
{
    if (!ValidateWindowState(state) || !node)
        return;
    XSetWindowBorder(state->display, node->frame, state->border_color);
    if (node->is_desktop_app || node->is_fullscreen_app)
    {
        Monitor *mon = &state->monitor_info.monitors[node->monitor_number];
        XMoveResizeWindow(state->display, node->frame, mon->x, mon->y, mon->width, mon->height);
        XMoveResizeWindow(state->display, node->client, 0, 0, mon->width, mon->height);
    }
    else
    {
        int frame_width = node->width + 2 * BORDER_WIDTH;
        int frame_height = node->height + (node->is_titlebar_disabled ? 0 : TITLE_BAR_HEIGHT) + 2 * BORDER_WIDTH;
        XMoveResizeWindow(state->display, node->frame, node->x, node->y, frame_width, frame_height);
        int client_x = BORDER_WIDTH;
        int client_y = node->is_titlebar_disabled ? BORDER_WIDTH : TITLE_BAR_HEIGHT + BORDER_WIDTH;
        XMoveWindow(state->display, node->client, client_x, client_y);
        XResizeWindow(state->display, node->client, node->width, node->height);
        if (!node->is_titlebar_disabled)
        {
            DrawTitleBar(state, node);
        }
    }
    if (state->supports_opacity)
    {
        SetWindowOpacity(state, node->frame, WINDOW_OPACITY);
    }
}
int IsDesktopAppByProperties(GooeyShellState *state, Window client)
{
    if (!ValidateWindowState(state))
        return 0;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL;
    int result = 0;
    if (atoms.gooey_desktop_app != None &&
        XGetWindowProperty(state->display, client, atoms.gooey_desktop_app, 0, 1,
                           False, XA_STRING, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop_data) == Success &&
        prop_data)
    {
        SafeXFree(prop_data);
        result = 1;
    }
    if (!result && XGetWindowProperty(state->display, client, atoms.net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop_data) == Success && prop_data)
    {
        Atom window_type = ((Atom *)prop_data)[0];
        SafeXFree(prop_data);
        if (window_type == atoms.net_wm_window_type_desktop)
        {
            result = 1;
        }
    }
    return result;
}
int IsFullscreenAppByProperties(GooeyShellState *state, Window client, int *stay_on_top)
{
    if (!ValidateWindowState(state))
        return 0;
    *stay_on_top = 0;
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_data = NULL;
    int result = 0;
    if (atoms.gooey_fullscreen_app != None &&
        XGetWindowProperty(state->display, client, atoms.gooey_fullscreen_app, 0, 1,
                           False, XA_STRING, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop_data) == Success &&
        prop_data)
    {
        SafeXFree(prop_data);
        if (atoms.gooey_stay_on_top != None &&
            XGetWindowProperty(state->display, client, atoms.gooey_stay_on_top, 0, 1,
                               False, XA_STRING, &actual_type, &actual_format,
                               &nitems, &bytes_after, &prop_data) == Success &&
            prop_data)
        {
            *stay_on_top = 1;
            SafeXFree(prop_data);
        }
        result = 1;
    }
    if (!result && XGetWindowProperty(state->display, client, atoms.net_wm_window_type, 0, 1, False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop_data) == Success && prop_data)
    {
        Atom window_type = ((Atom *)prop_data)[0];
        SafeXFree(prop_data);
        if (window_type == atoms.net_wm_window_type_dock ||
            window_type == atoms.net_wm_window_type_toolbar ||
            window_type == atoms.net_wm_window_type_menu)
        {
            *stay_on_top = 1;
            result = 1;
        }
    }
    return result;
}
int DetectAppTypeByTitleClass(GooeyShellState *state, Window client, int *is_desktop, int *is_fullscreen, int *stay_on_top)
{
    if (!ValidateWindowState(state))
        return 0;
    *is_desktop = 0;
    *is_fullscreen = 0;
    *stay_on_top = 0;
    XTextProperty text_prop;
    if (XGetWMName(state->display, client, &text_prop) && text_prop.value)
    {
        char *title = (char *)text_prop.value;
        char lower_title[256];
        int len = strlen(title);
        for (int i = 0; i < len && i < 255; i++)
        {
            lower_title[i] = tolower(title[i]);
        }
        lower_title[len < 255 ? len : 255] = '\0';
        if (strstr(lower_title, "desktop") || strstr(lower_title, "wallpaper") ||
            strstr(lower_title, "background"))
        {
            *is_desktop = 1;
            SafeXFree(text_prop.value);
            return 1;
        }
        else if (strstr(lower_title, "app menu") || strstr(lower_title, "application menu") ||
                 strstr(lower_title, "gooeyde_appmenu") || strstr(lower_title, "launcher") ||
                 strstr(lower_title, "menu"))
        {
            *is_fullscreen = 1;
            *stay_on_top = 1;
            SafeXFree(text_prop.value);
            return 1;
        }
        else if (strstr(lower_title, "dock") || strstr(lower_title, "taskbar") ||
                 strstr(lower_title, "panel") || strstr(lower_title, "toolbar"))
        {
            *is_fullscreen = 1;
            *stay_on_top = 1;
            SafeXFree(text_prop.value);
            return 1;
        }
        SafeXFree(text_prop.value);
    }
    XClassHint class_hint;
    if (XGetClassHint(state->display, client, &class_hint))
    {
        int detected = 0;
        if (class_hint.res_name)
        {
            char lower_class[256];
            int len = strlen(class_hint.res_name);
            for (int i = 0; i < len && i < 255; i++)
            {
                lower_class[i] = tolower(class_hint.res_name[i]);
            }
            lower_class[len < 255 ? len : 255] = '\0';
            if (strstr(lower_class, "desktop") || strstr(lower_class, "background"))
            {
                *is_desktop = 1;
                detected = 1;
            }
            else if (strstr(lower_class, "gooeyde_appmenu") || strstr(lower_class, "appmenu") ||
                     strstr(lower_class, "launcher") || strstr(lower_class, "menu"))
            {
                *is_fullscreen = 1;
                *stay_on_top = 1;
                detected = 1;
            }
            else if (strstr(lower_class, "dock") || strstr(lower_class, "taskbar") ||
                     strstr(lower_class, "panel") || strstr(lower_class, "toolbar"))
            {
                *is_fullscreen = 1;
                *stay_on_top = 1;
                detected = 1;
            }
        }
        SafeXFree(class_hint.res_name);
        SafeXFree(class_hint.res_class);
        if (detected)
        {
            return 1;
        }
    }
    return 0;
}
void SetWindowStateProperties(GooeyShellState *state, Window window, Atom *states, int count)
{
    if (!ValidateWindowState(state) || atoms.net_wm_state == None)
        return;
    XChangeProperty(state->display, window, atoms.net_wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, count);
}
void SetupDesktopApp(GooeyShellState *state, WindowNode *node)
{
    if (!ValidateWindowState(state) || !node)
        return;
    node->is_desktop_app = True;
    node->is_titlebar_disabled = True;
    node->is_fullscreen = True;
    Monitor *mon = &state->monitor_info.monitors[node->monitor_number];
    XSetWindowBorderWidth(state->display, node->frame, 0);
    XMoveResizeWindow(state->display, node->frame, mon->x, mon->y, mon->width, mon->height);
    XMoveResizeWindow(state->display, node->client, 0, 0, mon->width, mon->height);
    if (atoms.net_wm_window_type != None && atoms.net_wm_window_type_desktop != None)
    {
        XChangeProperty(state->display, node->client, atoms.net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&atoms.net_wm_window_type_desktop, 1);
        XChangeProperty(state->display, node->frame, atoms.net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&atoms.net_wm_window_type_desktop, 1);
    }
    if (atoms.net_wm_state != None && atoms.net_wm_state_below != None)
    {
        Atom states[] = {
            atoms.net_wm_state_below,
            atoms.net_wm_state_skip_taskbar,
            atoms.net_wm_state_skip_pager,
            atoms.net_wm_state_sticky};
        SetWindowStateProperties(state, node->client, states, 4);
        SetWindowStateProperties(state, node->frame, states, 4);
    }
    XLowerWindow(state->display, node->frame);
    state->desktop_app_window = node->frame;
    if (state->supports_opacity)
    {
        SetWindowOpacity(state, node->frame, WINDOW_OPACITY);
    }
}
void SetupFullscreenApp(GooeyShellState *state, WindowNode *node, int stay_on_top)
{
    if (!ValidateWindowState(state) || !node)
        return;
    node->is_fullscreen_app = True;
    node->is_titlebar_disabled = True;
    node->is_fullscreen = True;
    node->stay_on_top = stay_on_top;
    Monitor *mon = &state->monitor_info.monitors[node->monitor_number];
    XSetWindowBorderWidth(state->display, node->frame, 0);
    XMoveResizeWindow(state->display, node->frame, mon->x, mon->y, mon->width, mon->height);
    XMoveResizeWindow(state->display, node->client, 0, 0, mon->width, mon->height);
    if (atoms.net_wm_window_type != None && atoms.net_wm_window_type_dock != None)
    {
        XChangeProperty(state->display, node->client, atoms.net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&atoms.net_wm_window_type_dock, 1);
        XChangeProperty(state->display, node->frame, atoms.net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&atoms.net_wm_window_type_dock, 1);
    }
    if (stay_on_top)
    {
        Atom states[] = {
            atoms.net_wm_state_above,
            atoms.net_wm_state_sticky,
            atoms.net_wm_state_fullscreen,
            atoms.net_wm_state_skip_taskbar,
            atoms.net_wm_state_skip_pager};
        SetWindowStateProperties(state, node->client, states, 5);
        SetWindowStateProperties(state, node->frame, states, 5);
        XRaiseWindow(state->display, node->frame);
    }
    state->fullscreen_app_window = node->frame;
    if (state->supports_opacity)
    {
        SetWindowOpacity(state, node->frame, WINDOW_OPACITY);
    }
    FocusRootWindow(state);
}
void EnsureDesktopAppStaysInBackground(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;
    if (state->desktop_app_window != None)
    {
        XLowerWindow(state->display, state->desktop_app_window);
    }
}
void EnsureFullscreenAppStaysOnTop(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;
    if (state->fullscreen_app_window != None)
    {
        WindowNode *fullscreen_node = FindWindowNodeByFrame(state, state->fullscreen_app_window);
        if (fullscreen_node && fullscreen_node->stay_on_top)
        {
            XRaiseWindow(state->display, state->fullscreen_app_window);
        }
    }
}
int CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app)
{
    if (!ValidateWindowState(state))
        return 0;
    if (FindWindowNodeByClient(state, client) != NULL)
    {
        return 0;
    }
    XWindowAttributes attr;
    if (XGetWindowAttributes(state->display, client, &attr) == 0)
    {
        return 0;
    }
    int client_width = (attr.width > 0) ? attr.width : DEFAULT_WIDTH;
    int client_height = (attr.height > 0) ? attr.height : DEFAULT_HEIGHT;
    int frame_width, frame_height;
    Window frame;
    int monitor_number = 0;
    if (is_desktop_app)
    {
        monitor_number = 0;
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        frame_width = mon->width;
        frame_height = mon->height;
        frame = XCreateSimpleWindow(state->display, state->root,
                                    mon->x, mon->y, frame_width, frame_height,
                                    0, state->border_color, state->bg_color);
    }
    else
    {
        monitor_number = GetMonitorForWindow(state, attr.x, attr.y, client_width, client_height);
        int bar_height = monitor_number == 0 ? BAR_HEIGHT : 0;
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        int x = mon->x + (mon->width - client_width) / 2;
        int y = mon->y + bar_height + (mon->height - bar_height - client_height) / 2;
        frame_width = client_width + 2 * BORDER_WIDTH;
        frame_height = client_height + TITLE_BAR_HEIGHT + 2 * BORDER_WIDTH;
        frame = XCreateSimpleWindow(state->display, state->root,
                                    x, y, frame_width, frame_height,
                                    BORDER_WIDTH, state->border_color, state->bg_color);
    }
    if (frame == None)
    {
        return 0;
    }
    WindowNode *new_node = calloc(1, sizeof(WindowNode));
    if (!new_node)
    {
        XDestroyWindow(state->display, frame);
        return 0;
    }
    new_node->frame = frame;
    new_node->client = client;
    new_node->monitor_number = monitor_number;
    new_node->is_minimized = False;
    new_node->is_floating = False;
    new_node->is_tiled = True;
    new_node->tiling_x = -1;
    new_node->tiling_y = -1;
    new_node->tiling_width = -1;
    new_node->tiling_height = -1;
    new_node->floating_x = -1;
    new_node->floating_y = -1;
    new_node->floating_width = -1;
    new_node->floating_height = -1;
    if (is_desktop_app)
    {
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        new_node->x = mon->x;
        new_node->y = mon->y;
        new_node->width = mon->width;
        new_node->height = mon->height;
    }
    else
    {
        new_node->x = attr.x;
        new_node->y = attr.y;
        new_node->width = client_width;
        new_node->height = client_height;
    }
    new_node->title = StrDup("Untitled");
    new_node->is_fullscreen = is_desktop_app;
    new_node->is_titlebar_disabled = is_desktop_app;
    new_node->is_desktop_app = is_desktop_app;
    AddWindowToWorkspace(state, new_node, state->current_workspace);
    new_node->next = state->window_list;
    if (state->window_list)
    {
        state->window_list->prev = new_node;
    }
    state->window_list = new_node;
    XTextProperty text_prop;
    if (XGetWMName(state->display, client, &text_prop) && text_prop.value)
    {
        free(new_node->title);
        new_node->title = StrDup((char *)text_prop.value);
        SafeXFree(text_prop.value);
    }
    if (is_desktop_app)
    {
        XSelectInput(state->display, frame, ExposureMask | StructureNotifyMask);
        XSelectInput(state->display, client, PropertyChangeMask | StructureNotifyMask);
        XReparentWindow(state->display, client, frame, 0, 0);
        SetupDesktopApp(state, new_node);
    }
    else
    {
        XSelectInput(state->display, frame,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | StructureNotifyMask | EnterWindowMask | LeaveWindowMask);
        XSelectInput(state->display, client, PropertyChangeMask | StructureNotifyMask);
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        int client_x = BORDER_WIDTH;
        int client_y = TITLE_BAR_HEIGHT + BORDER_WIDTH;
        XReparentWindow(state->display, client, frame, client_x, client_y);
        XDefineCursor(state->display, frame, state->custom_cursor);
        EnsureDesktopAppStaysInBackground(state);
    }
    if (atoms.wm_protocols != None && atoms.wm_delete_window != None)
    {
        Atom protocols[] = {atoms.wm_delete_window};
        XSetWMProtocols(state->display, client, protocols, 1);
    }
    AddToOpenedWindows(frame);
    if (state->supports_opacity)
    {
        SetWindowOpacity(state, frame, WINDOW_OPACITY);
    }
    SendWindowStateThroughDBus(state, frame, "opened");
    XMapWindow(state->display, frame);
    XMapWindow(state->display, client);
    if (!is_desktop_app)
    {
        state->focused_window = frame;
        XRaiseWindow(state->display, frame);
        DrawTitleBar(state, new_node);
        Workspace *ws = GetCurrentWorkspace(state);
        if (ws)
        {
            int tiled_count = 0;
            WindowNode *node = ws->windows;
            while (node)
            {
                if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
                    !node->is_desktop_app && !node->is_fullscreen_app)
                {
                    tiled_count++;
                }
                node = node->next;
            }
            LogInfo("Tiling %d windows after new window creation", tiled_count);
            TileWindowsOnWorkspace(state, ws);
        }
    }
    RegrabKeys(state);
    return 1;
}
int CreateFullscreenAppWindow(GooeyShellState *state, Window client, int stay_on_top)
{
    if (!ValidateWindowState(state))
        return 0;
    if (FindWindowNodeByClient(state, client) != NULL)
    {
        return 0;
    }
    XWindowAttributes attr;
    if (XGetWindowAttributes(state->display, client, &attr) == 0)
    {
        return 0;
    }
    int monitor_number = GetMonitorForWindow(state, attr.x, attr.y, attr.width, attr.height);
    Monitor *mon = &state->monitor_info.monitors[monitor_number];
    Window frame = XCreateSimpleWindow(state->display, state->root,
                                       mon->x, mon->y, mon->width, mon->height,
                                       0, state->border_color, state->bg_color);
    if (frame == None)
    {
        return 0;
    }
    WindowNode *new_node = calloc(1, sizeof(WindowNode));
    if (!new_node)
    {
        XDestroyWindow(state->display, frame);
        return 0;
    }
    new_node->frame = frame;
    new_node->client = client;
    new_node->monitor_number = monitor_number;
    new_node->x = mon->x;
    new_node->y = mon->y;
    new_node->width = mon->width;
    new_node->height = mon->height;
    new_node->title = StrDup("Fullscreen App");
    new_node->is_fullscreen = True;
    new_node->is_titlebar_disabled = True;
    new_node->is_fullscreen_app = True;
    new_node->stay_on_top = stay_on_top;
    new_node->is_minimized = False;
    new_node->is_floating = True;
    new_node->is_tiled = False;
    new_node->tiling_x = -1;
    new_node->tiling_y = -1;
    new_node->tiling_width = -1;
    new_node->tiling_height = -1;
    new_node->floating_x = -1;
    new_node->floating_y = -1;
    new_node->floating_width = -1;
    new_node->floating_height = -1;
    AddWindowToWorkspace(state, new_node, state->current_workspace);
    new_node->next = state->window_list;
    if (state->window_list)
    {
        state->window_list->prev = new_node;
    }
    state->window_list = new_node;
    XTextProperty text_prop;
    if (XGetWMName(state->display, client, &text_prop) && text_prop.value)
    {
        free(new_node->title);
        new_node->title = StrDup((char *)text_prop.value);
        SafeXFree(text_prop.value);
    }
    XSelectInput(state->display, frame,
                 ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | EnterWindowMask | LeaveWindowMask);
    XSelectInput(state->display, client, PropertyChangeMask | StructureNotifyMask);
    XReparentWindow(state->display, client, frame, 0, 0);
    XResizeWindow(state->display, client, mon->width, mon->height);
    XDefineCursor(state->display, frame, state->custom_cursor);
    if (atoms.wm_protocols != None && atoms.wm_delete_window != None)
    {
        Atom protocols[] = {atoms.wm_delete_window};
        XSetWMProtocols(state->display, client, protocols, 1);
    }
    AddToOpenedWindows(frame);
    if (state->supports_opacity)
    {
        SetWindowOpacity(state, frame, WINDOW_OPACITY);
    }
    SendWindowStateThroughDBus(state, frame, "opened");
    XMapWindow(state->display, frame);
    XMapWindow(state->display, client);
    SetupFullscreenApp(state, new_node, stay_on_top);
    return 1;
}
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client)
{
    if (!ValidateWindowState(state))
        return;
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (node)
    {
        SetupDesktopApp(state, node);
    }
}
void RemoveWindow(GooeyShellState *state, Window client)
{
    if (!ValidateWindowState(state))
        return;
    WindowNode **current = &state->window_list;
    while (*current)
    {
        if ((*current)->client == client)
        {
            WindowNode *to_free = *current;
            *current = (*current)->next;
            if (*current)
            {
                (*current)->prev = to_free->prev;
            }
            RemoveWindowFromWorkspace(state, to_free);
            RemoveFromOpenedWindows(to_free->frame);
            SendWindowStateThroughDBus(state, to_free->frame, "closed");
            if (state->desktop_app_window == to_free->frame)
            {
                state->desktop_app_window = None;
            }
            if (state->fullscreen_app_window == to_free->frame)
            {
                state->fullscreen_app_window = None;
            }
            if (state->focused_window == to_free->frame)
            {
                FocusRootWindow(state);
            }
            XDestroyWindow(state->display, to_free->frame);
            FreeWindowNode(to_free);
            Workspace *ws = GetCurrentWorkspace(state);
            if (ws)
            {
                int tiled_count = 0;
                WindowNode *node = ws->windows;
                while (node)
                {
                    if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
                        !node->is_desktop_app && !node->is_fullscreen_app)
                    {
                        tiled_count++;
                    }
                    node = node->next;
                }
                LogInfo("Tiling %d windows after window removal", tiled_count);
                TileWindowsOnWorkspace(state, ws);
            }
            break;
        }
        current = &(*current)->next;
    }
    RegrabKeys(state);
}
WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame)
{
    if (!ValidateWindowState(state))
        return NULL;
    WindowNode *current = state->window_list;
    while (current)
    {
        if (current->frame == frame)
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}
WindowNode *FindWindowNodeByClient(GooeyShellState *state, Window client)
{
    if (!ValidateWindowState(state))
        return NULL;
    WindowNode *current = state->window_list;
    while (current)
    {
        if (current->client == client)
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}
void DrawTitleBar(GooeyShellState *state, WindowNode *node)
{
    if (!ValidateWindowState(state) || !node || node->is_titlebar_disabled ||
        node->is_desktop_app || node->is_fullscreen_app)
    {
        return;
    }
    unsigned long title_color = (state->focused_window == node->frame) ? state->titlebar_focused_color : state->titlebar_color;
    XSetForeground(state->display, state->titlebar_gc, title_color);
    XFillRectangle(state->display, node->frame, state->titlebar_gc,
                   BORDER_WIDTH, BORDER_WIDTH,
                   node->width, TITLE_BAR_HEIGHT);
    XSetForeground(state->display, state->text_gc, state->text_color);
    char display_title[256];
    strncpy(display_title, node->title, sizeof(display_title) - 1);
    display_title[sizeof(display_title) - 1] = '\0';
    if (strlen(node->title) > 40)
    {
        strncpy(display_title, node->title, 40);
        display_title[40] = '\0';
        strcat(display_title, "...");
    }
    XDrawString(state->display, node->frame, state->text_gc,
                BORDER_WIDTH + 5, BORDER_WIDTH + 15,
                display_title, strlen(display_title));
}
int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y)
{
    if (!node || node->is_titlebar_disabled || node->is_desktop_app || node->is_fullscreen_app)
        return 0;
    if (y < BORDER_WIDTH || y > BORDER_WIDTH + TITLE_BAR_HEIGHT)
        return 0;
    int close_button_x = node->width - BUTTON_SIZE - BUTTON_MARGIN;
    int maximize_button_x = close_button_x - BUTTON_SIZE - BUTTON_SPACING;
    int minimize_button_x = maximize_button_x - BUTTON_SIZE - BUTTON_SPACING;
    if (x >= close_button_x && x <= close_button_x + BUTTON_SIZE &&
        y >= BORDER_WIDTH + BUTTON_MARGIN && y <= BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE)
    {
        return 1;
    }
    if (x >= maximize_button_x && x <= maximize_button_x + BUTTON_SIZE &&
        y >= BORDER_WIDTH + BUTTON_MARGIN && y <= BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE)
    {
        return 2;
    }
    if (x >= minimize_button_x && x <= minimize_button_x + BUTTON_SIZE &&
        y >= BORDER_WIDTH + BUTTON_MARGIN && y <= BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE)
    {
        return 3;
    }
    return 0;
}
int GetResizeBorderArea(GooeyShellState *state, WindowNode *node, int x, int y)
{
    if (node->is_desktop_app || node->is_fullscreen || node->is_fullscreen_app)
        return 0;
    int frame_width = node->width + 2 * BORDER_WIDTH;
    int frame_height = node->height + (node->is_titlebar_disabled ? 0 : TITLE_BAR_HEIGHT) + 2 * BORDER_WIDTH;
    if (x >= frame_width - RESIZE_HANDLE_SIZE && x <= frame_width &&
        y >= frame_height - RESIZE_HANDLE_SIZE && y <= frame_height)
    {
        return 1;
    }
    if (x >= BORDER_WIDTH && x <= frame_width - BORDER_WIDTH &&
        y >= frame_height - BORDER_WIDTH && y <= frame_height)
    {
        return 2;
    }
    if (x >= frame_width - BORDER_WIDTH && x <= frame_width &&
        y >= (node->is_titlebar_disabled ? BORDER_WIDTH : TITLE_BAR_HEIGHT + BORDER_WIDTH) &&
        y <= frame_height - BORDER_WIDTH)
    {
        return 3;
    }
    return 0;
}
void CloseWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return;
    if (atoms.wm_protocols != None && atoms.wm_delete_window != None)
    {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = node->client;
        ev.xclient.message_type = atoms.wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = atoms.wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;
        if (XSendEvent(state->display, node->client, False, NoEventMask, &ev))
        {
            OptimizedXFlush(state);
            return;
        }
    }
    XUnmapWindow(state->display, node->frame);
    XUnmapWindow(state->display, node->client);
    RemoveWindow(state, node->client);
    if (state->focused_window == node->frame)
    {
        FocusRootWindow(state);
    }
}
void ToggleFullscreen(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    if (node->is_fullscreen)
    {
        XMoveResizeWindow(state->display, node->frame,
                          node->x, node->y,
                          node->width + 2 * BORDER_WIDTH,
                          node->height + TITLE_BAR_HEIGHT + 2 * BORDER_WIDTH);
        XResizeWindow(state->display, node->client, node->width, node->height);
        node->is_fullscreen = False;
        SendWindowStateThroughDBus(state, node->frame, "restored");
    }
    else
    {
        XWindowAttributes attr;
        if (XGetWindowAttributes(state->display, node->frame, &attr))
        {
            node->x = attr.x;
            node->y = attr.y;
        }
        int monitor_number = GetMonitorForWindow(state, node->x, node->y, node->width, node->height);
        int bar_height = monitor_number == 0 ? BAR_HEIGHT : 0;
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        XMoveResizeWindow(state->display, node->frame,
                          mon->x, mon->y + bar_height,
                          mon->width, mon->height - bar_height);
        XResizeWindow(state->display, node->client,
                      mon->width - 2 * BORDER_WIDTH,
                      mon->height - bar_height - TITLE_BAR_HEIGHT - 2 * BORDER_WIDTH);
        node->is_fullscreen = True;
        SendWindowStateThroughDBus(state, node->frame, "fullscreen");
    }
    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
}
void UpdateCursorForWindow(GooeyShellState *state, WindowNode *node, int x, int y)
{
    if (!node)
        return;
    if (!node->is_floating)
    {
        int tiling_resize_area = GetTilingResizeArea(state, node, x, y);
        if (tiling_resize_area > 0)
        {
            if (tiling_resize_area == 1)
            {
                XDefineCursor(state->display, node->frame, state->resize_cursor);
            }
            else
            {
                XDefineCursor(state->display, node->frame, state->v_resize_cursor);
            }
            return;
        }
    }
    if (node->is_floating)
    {
        int resize_area = GetResizeBorderArea(state, node, x, y);
        if (resize_area > 0)
        {
            XDefineCursor(state->display, node->frame, state->resize_cursor);
        }
        else if (y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
        {
            XDefineCursor(state->display, node->frame, state->move_cursor);
        }
        else
        {
            XDefineCursor(state->display, node->frame, state->normal_cursor);
        }
    }
    else
    {
        if (y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
        {
            XDefineCursor(state->display, node->frame, state->move_cursor);
        }
        else
        {
            XDefineCursor(state->display, node->frame, state->normal_cursor);
        }
    }
}
void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    FocusWindow(state, node);
    XRaiseWindow(state->display, ev->window);
    if (ev->button == Button1)
    {
        int relative_x = ev->x;
        int relative_y = ev->y;
        if (!node->is_floating)
        {
            int tiling_resize_area = GetTilingResizeArea(state, node, relative_x, relative_y);
            if (tiling_resize_area > 0)
            {
                state->is_tiling_resizing = True;
                state->drag_window = ev->window;
                state->drag_start_x = ev->x_root;
                state->drag_start_y = ev->y_root;
                state->tiling_resize_edge = tiling_resize_area;
                if (tiling_resize_area == 1)
                {
                    XDefineCursor(state->display, node->frame, state->resize_cursor);
                }
                else
                {
                    XDefineCursor(state->display, node->frame, state->v_resize_cursor);
                }
                XGrabPointer(state->display, ev->window, True,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync,
                             None, state->resize_cursor, CurrentTime);
                return;
            }
        }
        if (relative_y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
        {
            if (!node->is_floating)
            {
                GooeyShell_ToggleFloating(state, node->client);
                if (node->is_floating)
                {
                    state->is_dragging = True;
                    state->drag_window = ev->window;
                    state->drag_start_x = ev->x_root;
                    state->drag_start_y = ev->y_root;
                    state->original_x = node->x;
                    state->original_y = node->y;
                    XDefineCursor(state->display, node->frame, state->move_cursor);
                    XGrabPointer(state->display, ev->window, True,
                                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync,
                                 None, state->move_cursor, CurrentTime);
                }
            }
            else
            {
                state->is_dragging = True;
                state->drag_window = ev->window;
                state->drag_start_x = ev->x_root;
                state->drag_start_y = ev->y_root;
                state->original_x = node->x;
                state->original_y = node->y;
                XDefineCursor(state->display, node->frame, state->move_cursor);
                XGrabPointer(state->display, ev->window, True,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync,
                             None, state->move_cursor, CurrentTime);
            }
        }
        else if (node->is_floating)
        {
            int resize_area = GetResizeBorderArea(state, node, relative_x, relative_y);
            if (resize_area > 0)
            {
                state->is_resizing = True;
                state->drag_window = ev->window;
                state->drag_start_x = ev->x_root;
                state->drag_start_y = ev->y_root;
                state->original_width = node->width;
                state->original_height = node->height;
                state->resize_direction = resize_area;
                XDefineCursor(state->display, node->frame, state->resize_cursor);
                XGrabPointer(state->display, ev->window, True,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync,
                             None, state->resize_cursor, CurrentTime);
            }
        }
    }
    else if (ev->button == Button3)
    {
        GooeyShell_ToggleFloating(state, node->client);
    }
    EnsureDesktopAppStaysInBackground(state);
    EnsureFullscreenAppStaysOnTop(state);
}
void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev)
{
    if ((state->is_dragging || state->is_resizing || state->is_tiling_resizing) && ev->button == Button1)
    {
        state->is_dragging = False;
        state->is_resizing = False;
        state->is_tiling_resizing = False;
        WindowNode *node = FindWindowNodeByFrame(state, state->drag_window);
        if (node)
        {
            XDefineCursor(state->display, node->frame, state->custom_cursor);
        }
        state->drag_window = None;
        XUngrabPointer(state->display, CurrentTime);
    }
    EnsureDesktopAppStaysInBackground(state);
    EnsureFullscreenAppStaysOnTop(state);
}
void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    HandleMouseFocus(state, ev);
    if (state->is_tiling_resizing && ev->window == state->drag_window && !node->is_floating)
    {
        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;
        HandleTilingResize(state, node, state->tiling_resize_edge, delta_x, delta_y);
        state->drag_start_x = ev->x_root;
        state->drag_start_y = ev->y_root;
    }
    else if (state->is_dragging && ev->window == state->drag_window && node->is_floating)
    {
        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;
        int new_x = state->original_x + delta_x;
        int new_y = state->original_y + delta_y;
        Monitor *mon = &state->monitor_info.monitors[node->monitor_number];
        int frame_width = node->width + 2 * BORDER_WIDTH;
        int frame_height = node->height + TITLE_BAR_HEIGHT + 2 * BORDER_WIDTH;
        int bar_height = node->monitor_number == 0 ? BAR_HEIGHT : 0;

        if (new_x < mon->x)
            new_x = mon->x;
        if (new_y < mon->y + bar_height)
            new_y = mon->y + bar_height;
        if (new_x + frame_width > mon->x + mon->width)
            new_x = mon->x + mon->width - frame_width;
        if (new_y + frame_height > mon->y + mon->height)
            new_y = mon->y + mon->height - frame_height;
        if (new_x != node->x || new_y != node->y)
        {
            node->x = new_x;
            node->y = new_y;
            node->floating_x = node->x;
            node->floating_y = node->y;
            UpdateWindowGeometry(state, node);
        }
    }
    else if (state->is_resizing && ev->window == state->drag_window && node->is_floating)
    {
        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;
        switch (state->resize_direction)
        {
        case 1:
            node->width = state->original_width + delta_x;
            node->height = state->original_height + delta_y;
            break;
        case 2:
            node->height = state->original_height + delta_y;
            break;
        case 3:
            node->width = state->original_width + delta_x;
            break;
        }
        if (node->width < 100)
            node->width = 100;
        if (node->height < 100)
            node->height = 100;
        Monitor *mon = &state->monitor_info.monitors[node->monitor_number];
        int bar_height = node->monitor_number == 0 ? BORDER_WIDTH : 0;
        if (node->width > mon->width - 2 * bar_height)
            node->width = mon->width - 2 * bar_height;
        if (node->height > mon->height - TITLE_BAR_HEIGHT - 2 * bar_height - BAR_HEIGHT)
            node->height = mon->height - TITLE_BAR_HEIGHT - 2 * bar_height - BAR_HEIGHT;
        node->floating_width = node->width;
        node->floating_height = node->height;
        UpdateWindowGeometry(state, node);
    }
    else
    {
        UpdateCursorForWindow(state, node, ev->x, ev->y);
    }
}
void ReapZombieProcesses(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
}
void MinimizeWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    if (node->is_minimized)
        return;
    LogInfo("Minimizing window: %s", node->title ? node->title : "unknown");
    XWithdrawWindow(state->display, node->frame, state->screen);
    node->is_minimized = True;
    SendWindowStateThroughDBus(state, node->frame, "minimized");
    if (state->focused_window == node->frame)
    {
        FocusRootWindow(state);
    }
    OptimizedXFlush(state);
}
void RestoreWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    if (!node->is_minimized)
        return;
    LogInfo("Restoring window: %s", node->title ? node->title : "unknown");
    XMapRaised(state->display, node->frame);
    XMapWindow(state->display, node->client);
    node->is_minimized = False;
    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
    FocusWindow(state, node);
    SendWindowStateThroughDBus(state, node->frame, "restored");
    OptimizedXFlush(state);
}
void GooeyShell_RunEventLoop(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;
    XEvent ev;
    int reap_counter = 0;
    while (1)
    {
        if (++reap_counter >= 100)
        {
            ReapZombieProcesses();
            reap_counter = 0;
        }
        while (XPending(state->display))
        {
            XNextEvent(state->display, &ev);
            switch (ev.type)
            {
            case MapRequest:
            {
                Window client = ev.xmaprequest.window;
                if (FindWindowNodeByClient(state, client) != NULL)
                {
                    break;
                }
                XWindowAttributes attr;
                if (XGetWindowAttributes(state->display, client, &attr) == 0)
                {
                    break;
                }
                if (attr.override_redirect)
                {
                    XMapWindow(state->display, client);
                    break;
                }
                int is_desktop_app = 0;
                int is_fullscreen_app = 0;
                int stay_on_top = 0;
                is_desktop_app = IsDesktopAppByProperties(state, client);
                if (!is_desktop_app)
                {
                    is_fullscreen_app = IsFullscreenAppByProperties(state, client, &stay_on_top);
                }
                if (!is_desktop_app && !is_fullscreen_app)
                {
                    DetectAppTypeByTitleClass(state, client, &is_desktop_app, &is_fullscreen_app, &stay_on_top);
                }
                if (is_desktop_app)
                {
                    CreateFrameWindow(state, client, 1);
                }
                else if (is_fullscreen_app)
                {
                    CreateFullscreenAppWindow(state, client, stay_on_top);
                }
                else
                {
                    CreateFrameWindow(state, client, 0);
                }
                break;
            }
            case UnmapNotify:
                break;
            case DestroyNotify:
                if (ev.xdestroywindow.window != state->root)
                {
                    RemoveWindow(state, ev.xdestroywindow.window);
                }
                break;
            case ClientMessage:
            {
                if (ev.xclient.message_type == atoms.wm_protocols &&
                    (Atom)ev.xclient.data.l[0] == atoms.wm_delete_window)
                {
                    WindowNode *node = FindWindowNodeByClient(state, ev.xclient.window);
                    if (node)
                    {
                        CloseWindow(state, node);
                    }
                }
                break;
            }
            case ConfigureRequest:
            {
                WindowNode *node = FindWindowNodeByClient(state, ev.xconfigurerequest.window);
                if (node && !node->is_fullscreen && !node->is_desktop_app && !node->is_fullscreen_app)
                {
                    node->width = (ev.xconfigurerequest.width > 0) ? ev.xconfigurerequest.width : node->width;
                    node->height = (ev.xconfigurerequest.height > 0) ? ev.xconfigurerequest.height : node->height;
                    UpdateWindowGeometry(state, node);
                    if (!node->is_titlebar_disabled)
                        DrawTitleBar(state, node);
                }
                else if (!node)
                {
                    XWindowChanges changes;
                    changes.x = ev.xconfigurerequest.x;
                    changes.y = ev.xconfigurerequest.y;
                    changes.width = ev.xconfigurerequest.width;
                    changes.height = ev.xconfigurerequest.height;
                    changes.border_width = ev.xconfigurerequest.border_width;
                    changes.sibling = ev.xconfigurerequest.above;
                    changes.stack_mode = ev.xconfigurerequest.detail;
                    XErrorHandler old = XSetErrorHandler(IgnoreXError);
                    XConfigureWindow(state->display, ev.xconfigurerequest.window,
                                     ev.xconfigurerequest.value_mask, &changes);
                    XSync(state->display, False);
                    XSetErrorHandler(old);
                }
                break;
            }
            case Expose:
            {
                WindowNode *node = FindWindowNodeByFrame(state, ev.xexpose.window);
                if (node && ev.xexpose.count == 0 && !node->is_titlebar_disabled)
                {
                    DrawTitleBar(state, node);
                }
                break;
            }
            case EnterNotify:
            {
                WindowNode *node = FindWindowNodeByFrame(state, ev.xcrossing.window);
                if (node && !node->is_titlebar_disabled)
                {
                    XDefineCursor(state->display, node->frame, state->custom_cursor);
                }
                break;
            }
            case ButtonPress:
                HandleButtonPress(state, &ev.xbutton);
                break;
            case ButtonRelease:
                HandleButtonRelease(state, &ev.xbutton);
                break;
            case MotionNotify:
                HandleMotionNotify(state, &ev.xmotion);
                break;
            case KeyPress:
            {
                if (KeybindMatches(state, &ev.xkey, state->keybinds.launch_terminal))
                {
                    LogInfo("Launching terminal");
                    GooeyShell_AddWindow(state, "xterm", 0);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.close_window))
                {
                    LogInfo("Closing window");
                    if (state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                        {
                            CloseWindow(state, node);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.toggle_floating))
                {
                    LogInfo("Toggling floating");
                    if (state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                        {
                            GooeyShell_ToggleFloating(state, node->client);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.focus_next_window))
                {
                    LogInfo("Focusing next window");
                    GooeyShell_FocusNextWindow(state);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.focus_previous_window))
                {
                    LogInfo("Focusing previous window");
                    GooeyShell_FocusPreviousWindow(state);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.set_tiling_layout))
                {
                    LogInfo("Switching to tiling layout");
                    GooeyShell_SetLayout(state, LAYOUT_TILING);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.set_monocle_layout))
                {
                    LogInfo("Switching to monocle layout");
                    GooeyShell_SetLayout(state, LAYOUT_MONOCLE);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.shrink_width))
                {
                    LogInfo("Making window narrower");
                    Workspace *ws = GetCurrentWorkspace(state);
                    if (ws && ws->monitor_tiling_roots && state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_floating)
                        {
                            HandleTilingResize(state, node, 1, -30, 0);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.grow_width))
                {
                    LogInfo("Making window wider");
                    Workspace *ws = GetCurrentWorkspace(state);
                    if (ws && ws->monitor_tiling_roots && state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_floating)
                        {
                            HandleTilingResize(state, node, 1, 30, 0);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.shrink_height))
                {
                    LogInfo("Making window shorter");
                    Workspace *ws = GetCurrentWorkspace(state);
                    if (ws && ws->monitor_tiling_roots && state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_floating)
                        {
                            HandleTilingResize(state, node, 2, 0, -30);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.grow_height))
                {
                    LogInfo("Making window taller");
                    Workspace *ws = GetCurrentWorkspace(state);
                    if (ws && ws->monitor_tiling_roots && state->focused_window != None)
                    {
                        WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                        if (node && !node->is_floating)
                        {
                            HandleTilingResize(state, node, 2, 0, 30);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.toggle_layout))
                {
                    LogInfo("Toggling layout");
                    Workspace *ws = GetCurrentWorkspace(state);
                    if (ws)
                    {
                        if (ws->layout == LAYOUT_TILING)
                        {
                            GooeyShell_SetLayout(state, LAYOUT_MONOCLE);
                        }
                        else
                        {
                            GooeyShell_SetLayout(state, LAYOUT_TILING);
                        }
                    }
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.move_window_prev_monitor))
                {
                    LogInfo("Moving window to previous monitor");
                    GooeyShell_MoveWindowToPreviousMonitor(state);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.move_window_next_monitor))
                {
                    LogInfo("Moving window to next monitor");
                    GooeyShell_MoveWindowToNextMonitor(state);
                }
                else if (KeybindMatches(state, &ev.xkey, state->keybinds.logout))
                {
                    LogInfo("Logging out");
                    GooeyShell_Logout(state);
                }
                else
                {
                    for (int i = 0; i < 9; i++)
                    {
                        if (KeybindMatches(state, &ev.xkey, state->keybinds.switch_workspace[i]))
                        {
                            LogInfo("Switching to workspace %d", i + 1);
                            GooeyShell_SwitchWorkspace(state, i + 1);
                            break;
                        }
                    }
                }
                break;
            }
            }
        }
        if (!XPending(state->display))
        {
            usleep(5000);
        }
        EnsureDesktopAppStaysInBackground(state);
        EnsureFullscreenAppStaysOnTop(state);
        if (pending_x_flush)
        {
            XFlush(state->display);
            pending_x_flush = 0;
        }
    }
}
void GooeyShell_AddFullscreenApp(GooeyShellState *state, const char *command, int stay_on_top)
{
    if (!state || !command)
        return;
    pid_t pid = fork();
    if (pid == 0)
    {
        setenv("GOOEY_FULLSCREEN_APP", "1", 1);
        if (stay_on_top)
        {
            setenv("GOOEY_STAY_ON_TOP", "1", 1);
        }
        int max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd == -1)
        {
            max_fd = 1024;
        }
        for (int i = 3; i < max_fd; i++)
        {
            close(i);
        }
        execlp("sh", "sh", "-c", command, NULL);
        LogError("Failed to execute fullscreen app: %s", command);
        _exit(1);
    }
    else if (pid < 0)
    {
        LogError("Failed to fork for fullscreen app: %s", command);
    }
}
void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app)
{
    if (!state || !command)
        return;
    pid_t pid = fork();
    if (pid == 0)
    {
        if (desktop_app)
        {
            setenv("GOOEY_DESKTOP_APP", "1", 1);
        }
        int max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd == -1)
        {
            max_fd = 1024;
        }
        for (int i = 3; i < max_fd; i++)
        {
            close(i);
        }
        execlp("sh", "sh", "-c", command, NULL);
        LogError("Failed to execute command: %s", command);
        _exit(1);
    }
    else if (pid < 0)
    {
        LogError("Failed to fork for command: %s", command);
    }
}
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client)
{
    if (!ValidateWindowState(state))
        return;
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    node->is_titlebar_disabled = !node->is_titlebar_disabled;
    UpdateWindowGeometry(state, node);
    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
    else
    {
        XClearWindow(state->display, node->frame);
    }
}
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled)
{
    if (!ValidateWindowState(state))
        return;
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;
    if (node->is_titlebar_disabled == !enabled)
    {
        node->is_titlebar_disabled = !enabled;
        UpdateWindowGeometry(state, node);
        if (enabled)
        {
            DrawTitleBar(state, node);
        }
        else
        {
            XClearWindow(state->display, node->frame);
        }
    }
}
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client)
{
    if (!state)
        return 0;
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node)
        return 0;
    return !node->is_titlebar_disabled;
}
Window *GooeyShell_GetOpenedWindows(GooeyShellState *state, int *count)
{
    if (count)
    {
        *count = opened_windows_count;
    }
    return opened_windows;
}
int GooeyShell_IsWindowOpened(GooeyShellState *state, Window window)
{
    return IsWindowInOpenedWindows(window);
}
int GooeyShell_IsWindowMinimized(GooeyShellState *state, Window client)
{
    if (!state)
        return 0;
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node)
        return 0;
    return node->is_minimized;
}
void CleanupWorkspace(Workspace *ws)
{
    if (!ws)
        return;
    if (ws->monitor_tiling_roots)
    {
        for (int i = 0; i < ws->monitor_tiling_roots_count; i++)
        {
            if (ws->monitor_tiling_roots[i])
            {
                FreeTilingTree(ws->monitor_tiling_roots[i]);
            }
        }
        free(ws->monitor_tiling_roots);
    }
    if (ws->stack_ratios)
    {
        free(ws->stack_ratios);
    }
    free(ws);
}
void GooeyShell_Cleanup(GooeyShellState *state)
{
    if (!state)
        return;
    LogInfo("Cleaning up GooeyShell");
    dbus_thread_running = 0;
    if (state->is_dbus_init)
    {
        glps_thread_join(dbus_thread, NULL);
    }
    if (state->is_dragging || state->is_resizing || state->is_tiling_resizing)
    {
        XUngrabPointer(state->display, CurrentTime);
    }
    FocusRootWindow(state);
    Workspace *ws = state->workspaces;
    while (ws)
    {
        Workspace *next = ws->next;
        CleanupWorkspace(ws);
        ws = next;
    }
    WindowNode *current = state->window_list;
    while (current)
    {
        WindowNode *next = current->next;
        if (current->frame != None)
        {
            XUnmapWindow(state->display, current->frame);
        }
        if (current->client != None)
        {
            XUnmapWindow(state->display, current->client);
        }
        if (current->frame != None)
        {
            XDestroyWindow(state->display, current->frame);
        }
        FreeWindowNode(current);
        current = next;
    }
    state->window_list = NULL;
    FreeMultiMonitor(state);
    if (opened_windows)
    {
        free(opened_windows);
        opened_windows = NULL;
        opened_windows_count = 0;
        opened_windows_capacity = 0;
    }
    if (state->dbus_connection)
    {
        dbus_connection_unref(state->dbus_connection);
        state->dbus_connection = NULL;
    }
    FreeKeybinds(&state->keybinds);
    SAFE_FREE(state->config_file);
    SAFE_FREE(state->logout_command);
    if (state->text_gc)
    {
        XFreeGC(state->display, state->text_gc);
        state->text_gc = NULL;
    }
    if (state->titlebar_gc)
    {
        XFreeGC(state->display, state->titlebar_gc);
        state->titlebar_gc = NULL;
    }
    if (state->button_gc)
    {
        XFreeGC(state->display, state->button_gc);
        state->button_gc = NULL;
    }
    if (state->gc)
    {
        XFreeGC(state->display, state->gc);
        state->gc = NULL;
    }
    if (state->move_cursor)
    {
        XFreeCursor(state->display, state->move_cursor);
        state->move_cursor = None;
    }
    if (state->resize_cursor)
    {
        XFreeCursor(state->display, state->resize_cursor);
        state->resize_cursor = None;
    }
    if (state->v_resize_cursor)
    {
        XFreeCursor(state->display, state->v_resize_cursor);
        state->v_resize_cursor = None;
    }
    if (state->normal_cursor)
    {
        XFreeCursor(state->display, state->normal_cursor);
        state->normal_cursor = None;
    }
    if (state->custom_cursor && state->custom_cursor != state->normal_cursor)
    {
        XFreeCursor(state->display, state->custom_cursor);
        state->custom_cursor = None;
    }
    SAFE_CLOSE_DISPLAY(state->display);
    glps_thread_mutex_destroy(&window_list_mutex);
    glps_thread_mutex_destroy(&dbus_mutex);
    free(state);
}
int IgnoreXError(Display *d, XErrorEvent *e)
{
    return 0;
}
void FocusRootWindow(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;
    if (state->focused_window != None)
    {
        WindowNode *prev_node = FindWindowNodeByFrame(state, state->focused_window);
        if (prev_node)
        {
            XSetWindowBorder(state->display, prev_node->frame, state->border_color);
            if (!prev_node->is_titlebar_disabled)
            {
                DrawTitleBar(state, prev_node);
            }
        }
    }
    state->focused_window = None;
    XSetInputFocus(state->display, state->root, RevertToPointerRoot, CurrentTime);
}