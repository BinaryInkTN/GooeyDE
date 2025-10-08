#include "gooey_shell.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xrandr.h>
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

static PrecomputedAtoms atoms;

static Window *opened_windows = NULL;
static int opened_windows_count = 0;
static int opened_windows_capacity = 0;
static int dbus_thread_running = 0;
static gthread_t dbus_thread;
static gthread_mutex_t dbus_mutex;
static int window_list_update_pending = 0;
static int pending_x_flush = 0;

static void InitializeAtoms(Display *display);
static int InitializeMultiMonitor(GooeyShellState *state);
static void FreeMultiMonitor(GooeyShellState *state);
static int GetMonitorForWindow(GooeyShellState *state, int x, int y, int width, int height);
static int CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app);
static int CreateFullscreenAppWindow(GooeyShellState *state, Window client, int stay_on_top);
static void DrawTitleBar(GooeyShellState *state, WindowNode *node);
static void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev);
static void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev);
static void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev);
static WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame);
static WindowNode *FindWindowNodeByClient(GooeyShellState *state, Window client);
static void CloseWindow(GooeyShellState *state, WindowNode *node);
static void ToggleFullscreen(GooeyShellState *state, WindowNode *node);
static void MinimizeWindow(GooeyShellState *state, WindowNode *node);
static void RestoreWindow(GooeyShellState *state, WindowNode *node);
static int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y);
static int GetResizeBorderArea(GooeyShellState *state, WindowNode *node, int x, int y);
static void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node);
static void SetupDesktopApp(GooeyShellState *state, WindowNode *node);
static void SetupFullscreenApp(GooeyShellState *state, WindowNode *node, int stay_on_top);
static void EnsureDesktopAppStaysInBackground(GooeyShellState *state);
static void EnsureFullscreenAppStaysOnTop(GooeyShellState *state);
static Cursor CreateCustomCursor(GooeyShellState *state);
static void RemoveWindow(GooeyShellState *state, Window client);
static void FreeWindowNode(WindowNode *node);
static void ReapZombieProcesses(void);
static int IsDesktopAppByProperties(GooeyShellState *state, Window client);
static int IsFullscreenAppByProperties(GooeyShellState *state, Window client, int *stay_on_top);
static int DetectAppTypeByTitleClass(GooeyShellState *state, Window client, int *is_desktop, int *is_fullscreen, int *stay_on_top);
static void SetWindowStateProperties(GooeyShellState *state, Window window, Atom *states, int count);
static char *StrDup(const char *str);
static void SafeXFree(void *data);
static int ValidateWindowState(GooeyShellState *state);
static void LogError(const char *message, ...);
static void LogInfo(const char *message, ...);
static void AddToOpenedWindows(Window window);
static void RemoveFromOpenedWindows(Window window);
static int IsWindowInOpenedWindows(Window window);
static void SendWindowListThroughDBus(GooeyShellState *state);
static void SendWindowStateThroughDBus(GooeyShellState *state, Window window, const char *st);
static int ShouldExcludeFromTaskbar(GooeyShellState *state, WindowNode *node);
static void HandleDBusWindowCommand(GooeyShellState *state, DBusMessage *msg);
static void ScheduleWindowListUpdate(GooeyShellState *state);
static void OptimizedXFlush(GooeyShellState *state);
static void ProcessPendingEvents(GooeyShellState *state);

static void LogError(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void LogInfo(const char *message, ...)
{
    va_list args;
    va_start(args, message);
    printf("[INFO] ");
    vprintf(message, args);
    printf("\n");
    va_end(args);
}

static void SafeXFree(void *data)
{
    if (data)
        XFree(data);
}

static void ScheduleWindowListUpdate(GooeyShellState *state)
{
    if (!window_list_update_pending)
    {
        window_list_update_pending = 1;

        SendWindowListThroughDBus(state);
        window_list_update_pending = 0;
    }
}

static void OptimizedXFlush(GooeyShellState *state)
{
    if (!pending_x_flush)
    {
        pending_x_flush = 1;
        XFlush(state->display);
        pending_x_flush = 0;
    }
}

static void ProcessPendingEvents(GooeyShellState *state)
{

    while (XPending(state->display))
    {
        XEvent ev;
        XNextEvent(state->display, &ev);

        XPutBackEvent(state->display, &ev);
        break;
    }
}

static void ProcessDBusMessage(GooeyShellState *state, DBusMessage *msg)
{
    if (!state || !msg)
        return;

    if (dbus_message_is_method_call(msg, "dev.binaryink.gshell", "GetWindowList"))
    {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter args, array_iter;
        char window_id[64];
        int i;

        dbus_message_iter_init_append(reply, &args);

        if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                              DBUS_TYPE_STRING_AS_STRING,
                                              &array_iter))
        {
            dbus_message_unref(reply);
            return;
        }

        for (i = 0; i < opened_windows_count; i++)
        {
            WindowNode *node = FindWindowNodeByFrame(state, opened_windows[i]);
            if (node && !ShouldExcludeFromTaskbar(state, node))
            {
                snprintf(window_id, sizeof(window_id), "%lu", opened_windows[i]);
                const char *window_str = window_id;
                dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &window_str);
            }
        }

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
static void *DBusListenerThread(void *arg)
{
    GooeyShellState *state = (GooeyShellState *)arg;

    if (!state || !state->dbus_connection || !state->is_dbus_init)
    {
        return NULL;
    }

    LogInfo("DBus listener thread started");

    while (dbus_thread_running && state->is_dbus_init)
    {
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

static void SetupDBUS(GooeyShellState *state)
{
    int ret;

    dbus_error_init(&state->dbus_error);
    state->dbus_connection = dbus_bus_get(DBUS_BUS_SESSION, &state->dbus_error);
    if (dbus_error_is_set(&state->dbus_error))
    {
        dbus_error_free(&state->dbus_error);
    }
    if (!state->dbus_connection)
    {
        return;
    }

    ret = dbus_bus_request_name(state->dbus_connection, "dev.binaryink.gshell", DBUS_NAME_FLAG_REPLACE_EXISTING, &state->dbus_error);
    if (dbus_error_is_set(&state->dbus_error))
    {
        dbus_error_free(&state->dbus_error);
    }
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        return;
    }

    dbus_bus_add_match(state->dbus_connection,
                       "type='method_call',interface='dev.binaryink.gshell'",
                       &state->dbus_error);

    if (dbus_error_is_set(&state->dbus_error))
    {
        dbus_error_free(&state->dbus_error);
    }

    state->is_dbus_init = true;

    dbus_thread_running = 1;
    if (glps_thread_create(&dbus_thread, NULL, DBusListenerThread, state) != 0)
    {
        state->is_dbus_init = false;
    }
}

static int ShouldExcludeFromTaskbar(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return 0;

    if (node->is_desktop_app)
    {
        return 1;
    }

    if (node->is_fullscreen_app)
    {
        if (node->title)
        {
            char lower_title[256];
            for (int i = 0; node->title[i] && i < 255; i++)
            {
                lower_title[i] = tolower(node->title[i]);
            }
            lower_title[strlen(node->title) < 255 ? strlen(node->title) : 255] = '\0';

            if (strstr(lower_title, "menu") ||
                strstr(lower_title, "launcher") ||
                strstr(lower_title, "app menu") ||
                strstr(lower_title, "application menu") ||
                strstr(lower_title, "gooeyde_appmenu"))
            {
                return 1;
            }
        }
    }

    return 0;
}

static void SendWindowListThroughDBus(GooeyShellState *state)
{
    if (!state->dbus_connection || !state->is_dbus_init)
    {
        return;
    }

    DBusMessage *message;
    DBusMessageIter args, array_iter;
    char window_id[64];
    int i;

    message = dbus_message_new_signal("/dev/binaryink/gshell",
                                      "dev.binaryink.gshell",
                                      "WindowListUpdated");
    if (!message)
    {
        return;
    }

    dbus_message_iter_init_append(message, &args);

    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                          DBUS_TYPE_STRING_AS_STRING,
                                          &array_iter))
    {
        dbus_message_unref(message);
        return;
    }

    for (i = 0; i < opened_windows_count; i++)
    {
        WindowNode *node = FindWindowNodeByFrame(state, opened_windows[i]);
        if (node && !ShouldExcludeFromTaskbar(state, node))
        {
            snprintf(window_id, sizeof(window_id), "%lu", opened_windows[i]);
            const char *window_str = window_id;
            dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &window_str);
        }
    }

    dbus_message_iter_close_container(&args, &array_iter);

    dbus_connection_send(state->dbus_connection, message, NULL);
    dbus_connection_flush(state->dbus_connection);

    dbus_message_unref(message);
}

static void SendWindowStateThroughDBus(GooeyShellState *state, Window window, const char *state_str)
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

static void HandleDBusWindowCommand(GooeyShellState *state, DBusMessage *msg)
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

static void ListenForDBUSSignals(GooeyShellState *state)
{
    if (!state->dbus_connection || !state->is_dbus_init)
    {
        return;
    }

    dbus_bus_add_match(state->dbus_connection,
                       "type='method_call',interface='dev.binaryink.gshell'",
                       &state->dbus_error);

    if (dbus_error_is_set(&state->dbus_error))
    {
        dbus_error_free(&state->dbus_error);
    }

    while (1)
    {
        dbus_connection_read_write(state->dbus_connection, 0);
        DBusMessage *msg = dbus_connection_pop_message(state->dbus_connection);
        if (!msg)
        {
            usleep(100000);
            continue;
        }

        if (dbus_message_is_method_call(msg, "dev.binaryink.gshell", "GetWindowList"))
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter args, array_iter;
            char window_id[64];
            int i;

            dbus_message_iter_init_append(reply, &args);

            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                             DBUS_TYPE_STRING_AS_STRING,
                                             &array_iter);

            for (i = 0; i < opened_windows_count; i++)
            {
                WindowNode *node = FindWindowNodeByFrame(state, opened_windows[i]);
                if (node && !ShouldExcludeFromTaskbar(state, node))
                {
                    snprintf(window_id, sizeof(window_id), "%lu", opened_windows[i]);
                    const char *window_str = window_id;
                    dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &window_str);
                }
            }

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
        else if (dbus_message_is_method_call(msg, "dev.binaryink.gshell", "NotifyWindowOpened"))
        {
            const char *window_id;
            DBusMessageIter args;
            dbus_message_iter_init(msg, &args);
            if (DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&args))
            {
                dbus_message_iter_get_basic(&args, &window_id);
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            dbus_connection_send(state->dbus_connection, reply, NULL);
            dbus_message_unref(reply);
        }

        dbus_message_unref(msg);
    }
}

static int ValidateWindowState(GooeyShellState *state)
{
    if (!state || !state->display)
    {
        return 0;
    }
    return 1;
}

static void AddToOpenedWindows(Window window)
{
    if (opened_windows_count >= opened_windows_capacity)
    {
        int new_capacity = opened_windows_capacity == 0 ? 16 : opened_windows_capacity * 2;
        Window *new_array = realloc(opened_windows, new_capacity * sizeof(Window));
        if (!new_array)
        {
            return;
        }
        opened_windows = new_array;
        opened_windows_capacity = new_capacity;
    }

    opened_windows[opened_windows_count++] = window;
}

static void RemoveFromOpenedWindows(Window window)
{
    for (int i = 0; i < opened_windows_count; i++)
    {
        if (opened_windows[i] == window)
        {
            for (int j = i; j < opened_windows_count - 1; j++)
            {
                opened_windows[j] = opened_windows[j + 1];
            }
            opened_windows_count--;
            return;
        }
    }
}

static int IsWindowInOpenedWindows(Window window)
{
    for (int i = 0; i < opened_windows_count; i++)
    {
        if (opened_windows[i] == window)
        {
            return 1;
        }
    }
    return 0;
}

static void InitializeAtoms(Display *display)
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
}

static int InitializeMultiMonitor(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return 0;

    int event_base, error_base;
    if (!XRRQueryExtension(state->display, &event_base, &error_base))
    {
        state->monitor_info.monitors = malloc(sizeof(Monitor));
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
        return 0;
    }

    state->monitor_info.monitors = malloc(sizeof(Monitor) * resources->noutput);
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

static void FreeMultiMonitor(GooeyShellState *state)
{
    if (state->monitor_info.monitors)
    {
        free(state->monitor_info.monitors);
        state->monitor_info.monitors = NULL;
    }
    state->monitor_info.num_monitors = 0;
}

static int GetMonitorForWindow(GooeyShellState *state, int x, int y, int width, int height)
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

GooeyShellState *GooeyShell_Init(void)
{
    GooeyShellState *state = calloc(1, sizeof(GooeyShellState));
    if (!state)
    {
        return NULL;
    }
    glps_thread_mutex_init(&dbus_mutex, NULL);

    state->display = XOpenDisplay(NULL);
    if (!state->display)
    {
        free(state);
        return NULL;
    }

    state->screen = DefaultScreen(state->display);
    state->root = RootWindow(state->display, state->screen);

    state->desktop_app_window = None;
    state->fullscreen_app_window = None;
    state->focused_window = None;
    state->drag_window = None;

    state->monitor_info.monitors = NULL;
    state->monitor_info.num_monitors = 0;
    state->monitor_info.primary_monitor = 0;

    if (!InitializeMultiMonitor(state))
    {
        XCloseDisplay(state->display);
        free(state);
        return NULL;
    }

    InitializeAtoms(state->display);

    state->gc = XCreateGC(state->display, state->root, 0, NULL);
    if (!state->gc)
    {
        FreeMultiMonitor(state);
        XCloseDisplay(state->display);
        free(state);
        return NULL;
    }

    XGCValues gcv;
    state->titlebar_color = 0x212121;
    state->titlebar_focused_color = 0x424242;
    state->text_color = WhitePixel(state->display, state->screen);
    state->border_color = 0x666666;
    state->button_color = 0xDDDDDD;
    state->close_button_color = 0xFF4444;
    state->minimize_button_color = 0xFFCC00;
    state->maximize_button_color = 0x00CC44;
    state->bg_color = 0x333333;

    gcv.foreground = state->titlebar_color;
    state->titlebar_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    gcv.foreground = state->text_color;
    state->text_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    gcv.foreground = state->button_color;
    state->button_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    state->move_cursor = XCreateFontCursor(state->display, XC_fleur);
    state->resize_cursor = XCreateFontCursor(state->display, XC_bottom_right_corner);
    state->normal_cursor = XCreateFontCursor(state->display, XC_left_ptr);

    state->custom_cursor = CreateCustomCursor(state);
    if (state->custom_cursor == None)
    {
        state->custom_cursor = state->normal_cursor;
    }

    XSetWindowBackground(state->display, state->root, state->bg_color);

    XSelectInput(state->display, state->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     KeyPressMask | KeyReleaseMask | ExposureMask);

    XChangeProperty(state->display, state->root,
                    atoms.net_wm_name, atoms.utf8_string,
                    8, PropModeReplace,
                    (unsigned char *)WINDOW_MANAGER_NAME,
                    strlen(WINDOW_MANAGER_NAME));

    XDefineCursor(state->display, state->root, state->custom_cursor);
    XClearWindow(state->display, state->root);
    XFlush(state->display);

    opened_windows = NULL;
    opened_windows_count = 0;
    opened_windows_capacity = 0;
    state->is_dbus_init = false;

    SetupDBUS(state);

    return state;
}

static Cursor CreateCustomCursor(GooeyShellState *state)
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

static void FreeWindowNode(WindowNode *node)
{
    if (!node)
        return;
    free(node->title);
    free(node);
}

static char *StrDup(const char *str)
{
    if (!str)
        return NULL;
    char *new_str = strdup(str);
    if (!new_str)
    {
        return NULL;
    }
    return new_str;
}

static void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node)
{
    if (!ValidateWindowState(state) || !node)
        return;

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

        XResizeWindow(state->display, node->frame, frame_width, frame_height);

        int client_x = BORDER_WIDTH;
        int client_y = node->is_titlebar_disabled ? BORDER_WIDTH : TITLE_BAR_HEIGHT + BORDER_WIDTH;

        XMoveWindow(state->display, node->client, client_x, client_y);
        XResizeWindow(state->display, node->client, node->width, node->height);
    }
}

static int IsDesktopAppByProperties(GooeyShellState *state, Window client)
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

static int IsFullscreenAppByProperties(GooeyShellState *state, Window client, int *stay_on_top)
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

static int DetectAppTypeByTitleClass(GooeyShellState *state, Window client, int *is_desktop, int *is_fullscreen, int *stay_on_top)
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

        for (int i = 0; title[i] && i < 255; i++)
        {
            lower_title[i] = tolower(title[i]);
        }
        lower_title[strlen(title) < 255 ? strlen(title) : 255] = '\0';

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
            for (int i = 0; class_hint.res_name[i] && i < 255; i++)
            {
                lower_class[i] = tolower(class_hint.res_name[i]);
            }
            lower_class[strlen(class_hint.res_name) < 255 ? strlen(class_hint.res_name) : 255] = '\0';

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

static void SetWindowStateProperties(GooeyShellState *state, Window window, Atom *states, int count)
{
    if (!ValidateWindowState(state) || atoms.net_wm_state == None)
        return;

    XChangeProperty(state->display, window, atoms.net_wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, count);
}

static void SetupDesktopApp(GooeyShellState *state, WindowNode *node)
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
}

static void SetupFullscreenApp(GooeyShellState *state, WindowNode *node, int stay_on_top)
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
}

static void EnsureDesktopAppStaysInBackground(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;

    if (state->desktop_app_window != None)
    {
        XLowerWindow(state->display, state->desktop_app_window);
    }
}

static void EnsureFullscreenAppStaysOnTop(GooeyShellState *state)
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

static int CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app)
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
        Monitor *mon = &state->monitor_info.monitors[monitor_number];

        int x = mon->x + (mon->width - client_width) / 2;
        int y = mon->y + (mon->height - client_height) / 2;

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

    new_node->next = state->window_list;
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

    SendWindowStateThroughDBus(state, frame, "opened");

    XMapWindow(state->display, frame);
    XMapWindow(state->display, client);

    if (!is_desktop_app)
    {
        state->focused_window = frame;
        XRaiseWindow(state->display, frame);
        DrawTitleBar(state, new_node);
    }

    ScheduleWindowListUpdate(state);

    return 1;
}

static int CreateFullscreenAppWindow(GooeyShellState *state, Window client, int stay_on_top)
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
    new_node->next = state->window_list;
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

    SendWindowStateThroughDBus(state, frame, "opened");

    XMapWindow(state->display, frame);
    XMapWindow(state->display, client);
    SetupFullscreenApp(state, new_node, stay_on_top);

    ScheduleWindowListUpdate(state);

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

static void RemoveWindow(GooeyShellState *state, Window client)
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

            XDestroyWindow(state->display, to_free->frame);
            FreeWindowNode(to_free);

            ScheduleWindowListUpdate(state);
            break;
        }
        current = &(*current)->next;
    }
}

static WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame)
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

static WindowNode *FindWindowNodeByClient(GooeyShellState *state, Window client)
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

static void DrawTitleBar(GooeyShellState *state, WindowNode *node)
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

    int close_button_x = node->width - BUTTON_SIZE - BUTTON_MARGIN;
    int maximize_button_x = close_button_x - BUTTON_SIZE - BUTTON_SPACING;
    int minimize_button_x = maximize_button_x - BUTTON_SIZE - BUTTON_SPACING;

    XSetForeground(state->display, state->button_gc, state->minimize_button_color);
    XFillRectangle(state->display, node->frame, state->button_gc,
                   minimize_button_x, BORDER_WIDTH + BUTTON_MARGIN,
                   BUTTON_SIZE, BUTTON_SIZE);
    XSetForeground(state->display, state->text_gc, state->text_color);
    XDrawLine(state->display, node->frame, state->text_gc,
              minimize_button_x + 3, BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE / 2,
              minimize_button_x + BUTTON_SIZE - 3, BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE / 2);

    XSetForeground(state->display, state->button_gc, state->maximize_button_color);
    XFillRectangle(state->display, node->frame, state->button_gc,
                   maximize_button_x, BORDER_WIDTH + BUTTON_MARGIN,
                   BUTTON_SIZE, BUTTON_SIZE);
    XSetForeground(state->display, state->text_gc, state->text_color);
    XDrawRectangle(state->display, node->frame, state->text_gc,
                   maximize_button_x + 3, BORDER_WIDTH + BUTTON_MARGIN + 3,
                   BUTTON_SIZE - 6, BUTTON_SIZE - 6);

    XSetForeground(state->display, state->button_gc, state->close_button_color);
    XFillRectangle(state->display, node->frame, state->button_gc,
                   close_button_x, BORDER_WIDTH + BUTTON_MARGIN,
                   BUTTON_SIZE, BUTTON_SIZE);
    XSetForeground(state->display, state->text_gc, state->text_color);
    XDrawLine(state->display, node->frame, state->text_gc,
              close_button_x + 4, BORDER_WIDTH + BUTTON_MARGIN + 4,
              close_button_x + BUTTON_SIZE - 4, BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE - 4);
    XDrawLine(state->display, node->frame, state->text_gc,
              close_button_x + BUTTON_SIZE - 4, BORDER_WIDTH + BUTTON_MARGIN + 4,
              close_button_x + 4, BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE - 4);

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

static int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y)
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

static int GetResizeBorderArea(GooeyShellState *state, WindowNode *node, int x, int y)
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

static void CloseWindow(GooeyShellState *state, WindowNode *node)
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
        state->focused_window = None;
    }
}

static void ToggleFullscreen(GooeyShellState *state, WindowNode *node)
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
        Monitor *mon = &state->monitor_info.monitors[monitor_number];

        XMoveResizeWindow(state->display, node->frame, mon->x, mon->y, mon->width, mon->height);
        XResizeWindow(state->display, node->client,
                      mon->width - 2 * BORDER_WIDTH,
                      mon->height - TITLE_BAR_HEIGHT - 2 * BORDER_WIDTH);
        node->is_fullscreen = True;

        SendWindowStateThroughDBus(state, node->frame, "fullscreen");
    }

    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
}

static void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

    state->focused_window = ev->window;
    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }

    XRaiseWindow(state->display, ev->window);

    int button_area = GetTitleBarButtonArea(state, node, ev->x, ev->y);
    int resize_area = GetResizeBorderArea(state, node, ev->x, ev->y);

    if (ev->button == Button1)
    {
        if (button_area == 1)
        {
            CloseWindow(state, node);
            return;
        }
        else if (button_area == 2)
        {
            ToggleFullscreen(state, node);
            return;
        }
        else if (button_area == 3)
        {
            if (!node->is_minimized)
            {
                MinimizeWindow(state, node);
            }
            return;
        }

        if (resize_area > 0)
        {
            state->is_resizing = True;
            state->drag_window = ev->window;
            state->drag_start_x = ev->x_root;
            state->drag_start_y = ev->y_root;
            state->window_start_width = node->width;
            state->window_start_height = node->height;

            Cursor resize_cursor = state->resize_cursor;
            if (resize_area == 2)
            {
                resize_cursor = XCreateFontCursor(state->display, XC_bottom_side);
            }
            else if (resize_area == 3)
            {
                resize_cursor = XCreateFontCursor(state->display, XC_right_side);
            }

            XDefineCursor(state->display, ev->window, resize_cursor);

            if (XGrabPointer(state->display, state->root, False,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync,
                             None, resize_cursor, CurrentTime) != GrabSuccess)
            {
                state->is_resizing = False;
            }

            if (resize_area != 1)
            {
                XFreeCursor(state->display, resize_cursor);
            }
        }
        else if (ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH && button_area == 0)
        {
            state->is_dragging = True;
            state->drag_window = ev->window;
            state->drag_start_x = ev->x_root;
            state->drag_start_y = ev->y_root;

            XWindowAttributes attr;
            if (XGetWindowAttributes(state->display, ev->window, &attr))
            {
                state->window_start_x = attr.x;
                state->window_start_y = attr.y;
            }

            XDefineCursor(state->display, ev->window, state->move_cursor);

            if (XGrabPointer(state->display, state->root, False,
                             ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync,
                             None, state->move_cursor, CurrentTime) != GrabSuccess)
            {
                state->is_dragging = False;
            }
        }
    }
    else if (ev->button == Button3 && ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
    {
        ToggleFullscreen(state, node);
    }

    EnsureDesktopAppStaysInBackground(state);
    EnsureFullscreenAppStaysOnTop(state);
}

static void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev)
{
    if ((state->is_dragging || state->is_resizing) && ev->button == Button1)
    {
        state->is_dragging = False;
        state->is_resizing = False;

        WindowNode *node = FindWindowNodeByFrame(state, state->drag_window);
        if (node && !node->is_titlebar_disabled)
        {
            XDefineCursor(state->display, node->frame, state->custom_cursor);
        }

        state->drag_window = None;
        XUngrabPointer(state->display, CurrentTime);
    }

    EnsureDesktopAppStaysInBackground(state);
    EnsureFullscreenAppStaysOnTop(state);
}

static void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev)
{
    if (state->is_dragging && state->drag_window != None)
    {
        WindowNode *node = FindWindowNodeByFrame(state, state->drag_window);
        if (!node || node->is_fullscreen)
            return;

        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;

        int new_x = state->window_start_x + delta_x;
        int new_y = state->window_start_y + delta_y;

        int monitor_number = GetMonitorForWindow(state, new_x, new_y, node->width, node->height);
        Monitor *mon = &state->monitor_info.monitors[monitor_number];

        new_x = (new_x < mon->x) ? mon->x : new_x;
        new_y = (new_y < mon->y) ? mon->y : new_y;
        new_x = (new_x > mon->x + mon->width - 50) ? mon->x + mon->width - 50 : new_x;
        new_y = (new_y > mon->y + mon->height - 50) ? mon->y + mon->height - 50 : new_y;

        XMoveWindow(state->display, state->drag_window, new_x, new_y);
    }
    else if (state->is_resizing && state->drag_window != None)
    {
        WindowNode *node = FindWindowNodeByFrame(state, state->drag_window);
        if (!node || node->is_fullscreen)
            return;

        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;

        int new_width = state->window_start_width + delta_x;
        int new_height = state->window_start_height + delta_y;

        new_width = (new_width < MIN_WINDOW_WIDTH) ? MIN_WINDOW_WIDTH : new_width;
        new_height = (new_height < MIN_WINDOW_HEIGHT) ? MIN_WINDOW_HEIGHT : new_height;

        int monitor_number = GetMonitorForWindow(state, node->x, node->y, new_width, new_height);
        Monitor *mon = &state->monitor_info.monitors[monitor_number];
        int max_width = mon->width - 50;
        int max_height = mon->height - 50;
        new_width = (new_width > max_width) ? max_width : new_width;
        new_height = (new_height > max_height) ? max_height : new_height;

        node->width = new_width;
        node->height = new_height;

        UpdateWindowGeometry(state, node);

        if (!node->is_titlebar_disabled)
        {
            DrawTitleBar(state, node);
        }
    }
    else
    {
        WindowNode *node = FindWindowNodeByFrame(state, ev->window);
        if (!node || node->is_desktop_app || node->is_fullscreen_app)
            return;

        if (!node->is_titlebar_disabled)
        {
            int button_area = GetTitleBarButtonArea(state, node, ev->x, ev->y);
            int resize_area = GetResizeBorderArea(state, node, ev->x, ev->y);

            if (resize_area > 0)
            {
                Cursor cursor = state->resize_cursor;
                if (resize_area == 2)
                {
                    cursor = XCreateFontCursor(state->display, XC_bottom_side);
                }
                else if (resize_area == 3)
                {
                    cursor = XCreateFontCursor(state->display, XC_right_side);
                }

                XDefineCursor(state->display, ev->window, cursor);

                if (resize_area != 1)
                {
                    XFreeCursor(state->display, cursor);
                }
            }
            else if (ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH && button_area == 0)
            {
                XDefineCursor(state->display, ev->window, state->move_cursor);
            }
            else
            {
                XDefineCursor(state->display, ev->window, state->custom_cursor);
            }
        }
    }
}

static void ReapZombieProcesses(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
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
                    {
                        DrawTitleBar(state, node);
                    }
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

                    XConfigureWindow(state->display, ev.xconfigurerequest.window,
                                     ev.xconfigurerequest.value_mask, &changes);
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
                if (ev.xkey.state & Mod1Mask)
                {
                    if (ev.xkey.keycode == XKeysymToKeycode(state->display, XK_F4))
                    {
                        if (state->focused_window != None)
                        {
                            WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                            if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                            {
                                CloseWindow(state, node);
                            }
                        }
                    }
                    else if (ev.xkey.keycode == XKeysymToKeycode(state->display, XK_F11) ||
                             ev.xkey.keycode == XKeysymToKeycode(state->display, XK_Return))
                    {
                        if (state->focused_window != None)
                        {
                            WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                            if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                            {
                                ToggleFullscreen(state, node);
                            }
                        }
                    }
                }
                break;
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

        for (int i = 3; i < 1024; i++)
            close(i);

        execlp(command, command, NULL);
        exit(1);
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

        for (int i = 3; i < 1024; i++)
            close(i);

        execlp(command, command, NULL);
        exit(1);
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
static void MinimizeWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

    if (node->is_minimized)
        return;

    LogInfo("Minimizing window: %s", node->title);

    XWithdrawWindow(state->display, node->frame, state->screen);

    node->is_minimized = True;

    SendWindowStateThroughDBus(state, node->frame, "minimized");

    OptimizedXFlush(state);
}

static void RestoreWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

    if (!node->is_minimized)
        return;

    LogInfo("Restoring window: %s", node->title);

    XMapRaised(state->display, node->frame);
    XMapWindow(state->display, node->client);

    node->is_minimized = False;

    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }

    SendWindowStateThroughDBus(state, node->frame, "restored");

    OptimizedXFlush(state);
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

void GooeyShell_SendWindowList(GooeyShellState *state)
{
    SendWindowListThroughDBus(state);
}

void GooeyShell_Cleanup(GooeyShellState *state)
{
    if (!state)
        return;

    dbus_thread_running = 0;
    if (state->is_dbus_init)
    {
        glps_thread_join(dbus_thread, NULL);
    }
    if (state->is_dragging || state->is_resizing)
    {
        XUngrabPointer(state->display, CurrentTime);
    }

    WindowNode *current = state->window_list;
    while (current)
    {
        WindowNode *next = current->next;

        XUnmapWindow(state->display, current->frame);
        XUnmapWindow(state->display, current->client);

        XDestroyWindow(state->display, current->frame);
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
    }

    if (state->text_gc)
        XFreeGC(state->display, state->text_gc);
    if (state->titlebar_gc)
        XFreeGC(state->display, state->titlebar_gc);
    if (state->button_gc)
        XFreeGC(state->display, state->button_gc);
    if (state->gc)
        XFreeGC(state->display, state->gc);

    if (state->move_cursor)
        XFreeCursor(state->display, state->move_cursor);
    if (state->resize_cursor)
        XFreeCursor(state->display, state->resize_cursor);
    if (state->normal_cursor)
        XFreeCursor(state->display, state->normal_cursor);
    if (state->custom_cursor && state->custom_cursor != state->normal_cursor)
    {
        XFreeCursor(state->display, state->custom_cursor);
    }

    if (state->display)
        XCloseDisplay(state->display);
    free(state);
    glps_thread_mutex_destroy(&dbus_mutex);
}