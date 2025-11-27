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
#include <X11/keysym.h>

#define BAR_HEIGHT 50

static PrecomputedAtoms atoms;

static Window *opened_windows = NULL;
static int opened_windows_count = 0;
static int opened_windows_capacity = 0;
static int dbus_thread_running = 0;
static gthread_t dbus_thread;
static gthread_mutex_t dbus_mutex;
static int window_list_update_pending = 0;
static int pending_x_flush = 0;

int IgnoreXError(Display *d, XErrorEvent *e) { return 0; }

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
static void SendWindowStateThroughDBus(GooeyShellState *state, Window window, const char *st);
static void SendWorkspaceChangedThroughDBus(GooeyShellState *state, int old_workspace, int new_workspace);
static void HandleDBusWindowCommand(GooeyShellState *state, DBusMessage *msg);
static void ScheduleWindowListUpdate(GooeyShellState *state);
static void OptimizedXFlush(GooeyShellState *state);
static void ProcessPendingEvents(GooeyShellState *state);

static void InitializeWorkspaces(GooeyShellState *state);
static Workspace *GetCurrentWorkspace(GooeyShellState *state);
static void TileWindowsOnWorkspace(GooeyShellState *state, Workspace *workspace);
static void ArrangeWindowsTiling(GooeyShellState *state, Workspace *workspace);
static void ArrangeWindowsMonocle(GooeyShellState *state, Workspace *workspace);
static void FocusWindow(GooeyShellState *state, WindowNode *node);
static WindowNode *GetNextWindow(GooeyShellState *state, WindowNode *current);
static WindowNode *GetPreviousWindow(GooeyShellState *state, WindowNode *current);
static void AddWindowToWorkspace(GooeyShellState *state, WindowNode *node, int workspace);
static void RemoveWindowFromWorkspace(GooeyShellState *state, WindowNode *node);
static Workspace *GetWorkspace(GooeyShellState *state, int workspace_number);
static Workspace *CreateWorkspace(GooeyShellState *state, int workspace_number);

static int GetTilingResizeArea(GooeyShellState *state, WindowNode *node, int x, int y);
static void HandleTilingResize(GooeyShellState *state, WindowNode *node, int resize_edge, int delta_x, int delta_y);
static void ResizeMasterArea(GooeyShellState *state, Workspace *workspace, int delta_width);
static void ResizeStackWindow(GooeyShellState *state, Workspace *workspace, int window_index, int delta_height);
static void ResizeHorizontalSplit(GooeyShellState *state, Workspace *workspace, int split_index, int delta_height);

static TilingNode *CreateTilingNode(WindowNode *window, int x, int y, int width, int height);
static void FreeTilingTree(TilingNode *root);
static void BuildDynamicTilingTree(GooeyShellState *state, Workspace *workspace);
static void ArrangeTilingTree(GooeyShellState *state, TilingNode *node);
static SplitDirection ChooseSplitDirection(int width, int height, int window_count);
static TilingNode *BuildTreeRecursive(WindowNode **windows, int count, int x, int y, int width, int height, TilingNode *existing_root);
static void UpdateTilingNodeGeometry(TilingNode *node, int x, int y, int width, int height);
static void CleanupWorkspace(Workspace *ws);

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

        dbus_message_iter_init_append(reply, &args);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                         DBUS_TYPE_STRING_AS_STRING,
                                         &array_iter);
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

static void GrabKeys(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;

    XUngrabKey(state->display, AnyKey, AnyModifier, state->root);

    KeyCode keys[] = {
        XKeysymToKeycode(state->display, XK_Return),
        XKeysymToKeycode(state->display, XK_q),
        XKeysymToKeycode(state->display, XK_f),
        XKeysymToKeycode(state->display, XK_j),
        XKeysymToKeycode(state->display, XK_k),
        XKeysymToKeycode(state->display, XK_t),
        XKeysymToKeycode(state->display, XK_y),
        XKeysymToKeycode(state->display, XK_n),
        XKeysymToKeycode(state->display, XK_m),
        XKeysymToKeycode(state->display, XK_1),
        XKeysymToKeycode(state->display, XK_2),
        XKeysymToKeycode(state->display, XK_3),
        XKeysymToKeycode(state->display, XK_4),
        XKeysymToKeycode(state->display, XK_5),
        XKeysymToKeycode(state->display, XK_6),
        XKeysymToKeycode(state->display, XK_7),
        XKeysymToKeycode(state->display, XK_8),
        XKeysymToKeycode(state->display, XK_9),
        XKeysymToKeycode(state->display, XK_Left),
        XKeysymToKeycode(state->display, XK_Right),
        XKeysymToKeycode(state->display, XK_Up),
        XKeysymToKeycode(state->display, XK_Down),
        XKeysymToKeycode(state->display, XK_F4),
        XKeysymToKeycode(state->display, XK_h),
        XKeysymToKeycode(state->display, XK_l),
        XKeysymToKeycode(state->display, XK_space),
    };

    for (int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        if (keys[i] != 0)
        {
            XGrabKey(state->display, keys[i], Mod1Mask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keys[i], Mod1Mask | LockMask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keys[i], Mod1Mask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keys[i], Mod1Mask | LockMask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
        }
    }

    XFlush(state->display);
}

static void RegrabKeys(GooeyShellState *state)
{
    GrabKeys(state);
}

static int IsWindowInSubtree(TilingNode *root, WindowNode *target)
{
    if (!root)
        return 0;
    if (root->is_leaf)
        return root->window == target;
    return IsWindowInSubtree(root->left, target) || IsWindowInSubtree(root->right, target);
}

static TilingNode* findContainingSplit(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf) return NULL;

    int in_left = IsWindowInSubtree(root->left, target);
    int in_right = IsWindowInSubtree(root->right, target);

    if (in_left && in_right) {

        return NULL;
    }

    if (in_left || in_right) {
        return root;
    }

    return NULL;
}

static TilingNode* findVerticalSplitToLeft(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf) return NULL;

    if (root->split == SPLIT_VERTICAL) {

        if (IsWindowInSubtree(root->right, target)) {
            return root;
        }
    }

    TilingNode *found = findVerticalSplitToLeft(root->left, target);
    if (found) return found;
    return findVerticalSplitToLeft(root->right, target);
}

static TilingNode* findVerticalSplitToRight(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf) return NULL;

    if (root->split == SPLIT_VERTICAL) {

        if (IsWindowInSubtree(root->left, target)) {
            return root;
        }
    }

    TilingNode *found = findVerticalSplitToRight(root->left, target);
    if (found) return found;
    return findVerticalSplitToRight(root->right, target);
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

static void SendWorkspaceChangedThroughDBus(GooeyShellState *state, int old_workspace, int new_workspace)
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

static void InitializeWorkspaces(GooeyShellState *state)
{
    state->workspaces = malloc(sizeof(Workspace));
    state->workspaces->number = 1;
    state->workspaces->layout = LAYOUT_TILING;
    state->workspaces->windows = NULL;
    state->workspaces->next = NULL;
    state->workspaces->master_ratio = 0.6;
    state->workspaces->stack_ratios = NULL;
    state->workspaces->stack_ratios_count = 0;
    state->workspaces->tiling_root = NULL;

    state->current_workspace = 1;
    state->current_layout = LAYOUT_TILING;
    state->mod_key = Mod1Mask;
    state->super_pressed = False;

    state->inner_gap = 8;
    state->outer_gap = 8;
}

static Workspace *GetCurrentWorkspace(GooeyShellState *state)
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

static Workspace *GetWorkspace(GooeyShellState *state, int workspace_number)
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
    return NULL;
}

static Workspace *CreateWorkspace(GooeyShellState *state, int workspace_number)
{
    Workspace *new_ws = malloc(sizeof(Workspace));
    new_ws->number = workspace_number;
    new_ws->layout = LAYOUT_TILING;
    new_ws->windows = NULL;
    new_ws->next = NULL;
    new_ws->master_ratio = 0.6;
    new_ws->stack_ratios = NULL;
    new_ws->stack_ratios_count = 0;
    new_ws->tiling_root = NULL;

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

static void AddWindowToWorkspace(GooeyShellState *state, WindowNode *node, int workspace)
{
    Workspace *ws = GetWorkspace(state, workspace);
    if (!ws)
    {
        ws = CreateWorkspace(state, workspace);
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

static void RemoveWindowFromWorkspace(GooeyShellState *state, WindowNode *node)
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

static TilingNode *CreateTilingNode(WindowNode *window, int x, int y, int width, int height)
{
    TilingNode *node = malloc(sizeof(TilingNode));
    node->window = window;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    node->x = x;
    node->y = y;
    node->width = width;
    node->height = height;
    node->ratio = 0.5f;
    node->split = SPLIT_NONE;
    node->is_leaf = (window != NULL);
    return node;
}

static void FreeTilingTree(TilingNode *root)
{
    if (!root)
        return;
    FreeTilingTree(root->left);
    FreeTilingTree(root->right);
    free(root);
}

static SplitDirection ChooseSplitDirection(int width, int height, int window_count)
{
    if (window_count <= 1)
        return SPLIT_NONE;

    if (width > height * 1.2)
    {
        return SPLIT_VERTICAL;
    }
    else
    {
        return SPLIT_HORIZONTAL;
    }
}

static int CountLeaves(TilingNode *node)
{
    if (!node)
        return 0;
    if (node->is_leaf)
        return 1;
    return CountLeaves(node->left) + CountLeaves(node->right);
}

static TilingNode *BuildTreeRecursive(WindowNode **windows, int count, int x, int y, int width, int height, TilingNode *existing_root)
{
    if (count == 0)
        return NULL;

    if (count == 1)
    {
        TilingNode *node = CreateTilingNode(windows[0], x, y, width, height);

        if (existing_root)
        {
            TilingNode *findExistingNode(TilingNode * current, WindowNode * target)
            {
                if (!current)
                    return NULL;
                if (current->window == target)
                    return current;
                TilingNode *found = findExistingNode(current->left, target);
                if (found)
                    return found;
                return findExistingNode(current->right, target);
            }

            TilingNode *existing = findExistingNode(existing_root, windows[0]);
            if (existing && existing->parent)
            {

                node->parent = existing->parent;

            }
        }

        return node;
    }

    TilingNode *node = CreateTilingNode(NULL, x, y, width, height);
    node->split = ChooseSplitDirection(width, height, count);

    if (existing_root)
    {

        TilingNode *findSimilarSplit(TilingNode * current, int target_count)
        {
            if (!current || current->is_leaf)
                return NULL;
            if (CountLeaves(current) == target_count)
                return current;
            TilingNode *found = findSimilarSplit(current->left, target_count);
            if (found)
                return found;
            return findSimilarSplit(current->right, target_count);
        }

        TilingNode *similar = findSimilarSplit(existing_root, count);
        if (similar && similar->split == node->split)
        {
            node->ratio = similar->ratio;
        }
    }

    int left_count = count / 2;
    int right_count = count - left_count;

    if (node->split == SPLIT_VERTICAL)
    {
        int left_width = (int)(width * node->ratio);
        int right_width = width - left_width;

        node->left = BuildTreeRecursive(windows, left_count, x, y, left_width, height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count, x + left_width, y, right_width, height, existing_root);
    }
    else
    {
        int left_height = (int)(height * node->ratio);
        int right_height = height - left_height;

        node->left = BuildTreeRecursive(windows, left_count, x, y, width, left_height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count, x, y + left_height, width, right_height, existing_root);
    }

    if (node->left)
        node->left->parent = node;
    if (node->right)
        node->right->parent = node;

    return node;
}
static void BuildDynamicTilingTree(GooeyShellState *state, Workspace *workspace)
{
    TilingNode *old_root = workspace->tiling_root;

    WindowNode **tiled_windows = NULL;
    int tiled_count = 0;
    WindowNode *node = workspace->windows;

    while (node)
    {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app)
        {
            tiled_count++;
        }
        node = node->next;
    }

    if (tiled_count == 0)
    {
        if (old_root)
            FreeTilingTree(old_root);
        workspace->tiling_root = NULL;
        return;
    }

    tiled_windows = malloc(sizeof(WindowNode *) * tiled_count);
    node = workspace->windows;
    int index = 0;

    while (node && index < tiled_count)
    {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app)
        {
            tiled_windows[index++] = node;
        }
        node = node->next;
    }

    Monitor *mon = &state->monitor_info.monitors[0];
    int usable_width = mon->width - 2 * state->outer_gap;
    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;
    int usable_x = mon->x + state->outer_gap;
    int usable_y = mon->y + state->outer_gap + BAR_HEIGHT;

    workspace->tiling_root = BuildTreeRecursive(tiled_windows, tiled_count, usable_x, usable_y, usable_width, usable_height, old_root);

    if (old_root)
        FreeTilingTree(old_root);
    free(tiled_windows);
}
static void ArrangeTilingTree(GooeyShellState *state, TilingNode *node)
{
    if (!node)
        return;

    if (node->is_leaf && node->window)
    {
        int gap = state->inner_gap;
        node->window->x = node->x + gap;
        node->window->y = node->y + gap;
        node->window->width = node->width - 2 * gap;
        node->window->height = node->height - 2 * gap;

        node->window->tiling_x = node->window->x;
        node->window->tiling_y = node->window->y;
        node->window->tiling_width = node->window->width;
        node->window->tiling_height = node->window->height;

        if (node->window->width < 1)
            node->window->width = 1;
        if (node->window->height < 1)
            node->window->height = 1;

        UpdateWindowGeometry(state, node->window);
    }
    else
    {
        ArrangeTilingTree(state, node->left);
        ArrangeTilingTree(state, node->right);
    }
}

static void ArrangeWindowsTiling(GooeyShellState *state, Workspace *workspace)
{
    BuildDynamicTilingTree(state, workspace);
    ArrangeTilingTree(state, workspace->tiling_root);
}

static void ArrangeWindowsMonocle(GooeyShellState *state, Workspace *workspace)
{
    WindowNode *node = workspace->windows;
    Monitor *mon = &state->monitor_info.monitors[0];

    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;
    int usable_y = mon->y + state->outer_gap + BAR_HEIGHT;

    if (usable_height <= 0)
    {
        usable_height = 100;
    }

    while (node)
    {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app)
        {
            node->x = mon->x + state->outer_gap;
            node->y = usable_y + state->inner_gap;
            node->width = mon->width - 2 * state->outer_gap;
            node->height = usable_height - 2 * state->inner_gap;

            if (node->width < 1)
                node->width = 1;
            if (node->height < 1)
                node->height = 1;

            UpdateWindowGeometry(state, node);
        }
        node = node->next;
    }
}

static void TileWindowsOnWorkspace(GooeyShellState *state, Workspace *workspace)
{
    if (workspace->layout == LAYOUT_TILING)
    {
        ArrangeWindowsTiling(state, workspace);
    }
    else if (workspace->layout == LAYOUT_MONOCLE)
    {
        ArrangeWindowsMonocle(state, workspace);
    }
}

static int GetTilingResizeArea(GooeyShellState *state, WindowNode *node, int x, int y)
{
    if (node->is_floating || node->is_fullscreen || node->is_desktop_app || node->is_fullscreen_app)
        return 0;

    Workspace *workspace = GetCurrentWorkspace(state);
    if (!workspace)
        return 0;

    int resize_handle_size = 6;

    if (x >= node->width - resize_handle_size && x <= node->width + resize_handle_size)
    {
        return 1;
    }

    if (y >= node->height - resize_handle_size && y <= node->height + resize_handle_size)
    {
        return 2;
    }

    return 0;
}
static void HandleTilingResize(GooeyShellState *state, WindowNode *node, int resize_edge, int delta_x, int delta_y)
{
    Workspace *workspace = GetCurrentWorkspace(state);
    if (!workspace || !workspace->tiling_root) return;

    typedef struct {
        TilingNode *split;
        int is_left_child; 
    } SplitInfo;

    SplitInfo splits[32]; 
    int split_count = 0;

    void collectSplits(TilingNode *current, WindowNode *target) {
        if (!current || current->is_leaf) return;

        int in_left = IsWindowInSubtree(current->left, target);
        int in_right = IsWindowInSubtree(current->right, target);

        if (in_left || in_right) {
            if (split_count < 32) {
                splits[split_count].split = current;
                splits[split_count].is_left_child = in_left ? 1 : 0;
                split_count++;
            }

            if (in_left) collectSplits(current->left, target);
            if (in_right) collectSplits(current->right, target);
        }
    }

    split_count = 0;
    collectSplits(workspace->tiling_root, node);

    if (split_count == 0) return;

    TilingNode *target_split = NULL;
    float sensitivity = 0.03f;

    for (int i = 0; i < split_count; i++) {
        TilingNode *split = splits[i].split;
        int is_left = splits[i].is_left_child;

        if (resize_edge == 1) { 
            if (split->split == SPLIT_VERTICAL) {

                if ((is_left && delta_x > 0) || (!is_left && delta_x < 0)) {

                    target_split = split;
                    break;
                }
            }
        } else if (resize_edge == 2) { 
            if (split->split == SPLIT_HORIZONTAL) {

                if ((is_left && delta_y > 0) || (!is_left && delta_y < 0)) {

                    target_split = split;
                    break;
                }
            }
        }
    }

    if (!target_split) {
        for (int i = 0; i < split_count; i++) {
            TilingNode *split = splits[i].split;
            if ((resize_edge == 1 && split->split == SPLIT_VERTICAL) ||
                (resize_edge == 2 && split->split == SPLIT_HORIZONTAL)) {
                target_split = split;
                break;
            }
        }
    }

    if (!target_split) return;

    int is_left = 0;
    for (int i = 0; i < split_count; i++) {
        if (splits[i].split == target_split) {
            is_left = splits[i].is_left_child;
            break;
        }
    }

    int direction = is_left ? 1 : -1;

    if (resize_edge == 1 && target_split->split == SPLIT_VERTICAL) {
        target_split->ratio += direction * (float)delta_x / target_split->width * sensitivity;
    } else if (resize_edge == 2 && target_split->split == SPLIT_HORIZONTAL) {
        target_split->ratio += direction * (float)delta_y / target_split->height * sensitivity;
    }

    if (target_split->ratio < 0.1f) target_split->ratio = 0.1f;
    if (target_split->ratio > 0.9f) target_split->ratio = 0.9f;

    printf("Resizing split: %s, ratio: %.2f, direction: %d\n", 
           target_split->split == SPLIT_VERTICAL ? "vertical" : "horizontal",
           target_split->ratio, direction);

    TileWindowsOnWorkspace(state, workspace);
}
static void ResizeMasterArea(GooeyShellState *state, Workspace *workspace, int delta_width)
{
    Monitor *mon = &state->monitor_info.monitors[0];
    int usable_width = mon->width - 2 * state->outer_gap;

    float ratio_change = (float)delta_width / usable_width;
    workspace->master_ratio += ratio_change;

    if (workspace->master_ratio < 0.1)
        workspace->master_ratio = 0.1;
    if (workspace->master_ratio > 0.9)
        workspace->master_ratio = 0.9;
}

static void ResizeHorizontalSplit(GooeyShellState *state, Workspace *workspace, int split_index, int delta_height)
{
    if (!workspace->stack_ratios || split_index < 0 || split_index >= workspace->stack_ratios_count - 1)
        return;

    Monitor *mon = &state->monitor_info.monitors[0];
    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;

    float ratio_change = (float)delta_height / usable_height;

    workspace->stack_ratios[split_index] += ratio_change;
    workspace->stack_ratios[split_index + 1] -= ratio_change;

    float min_ratio = 0.1f;
    if (workspace->stack_ratios[split_index] < min_ratio)
    {
        workspace->stack_ratios[split_index + 1] += (workspace->stack_ratios[split_index] - min_ratio);
        workspace->stack_ratios[split_index] = min_ratio;
    }
    if (workspace->stack_ratios[split_index + 1] < min_ratio)
    {
        workspace->stack_ratios[split_index] += (workspace->stack_ratios[split_index + 1] - min_ratio);
        workspace->stack_ratios[split_index + 1] = min_ratio;
    }
}

static void FocusWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return;

    if (state->focused_window != None && state->focused_window != node->frame)
    {
        WindowNode *prev_node = FindWindowNodeByFrame(state, state->focused_window);
        if (prev_node && !prev_node->is_titlebar_disabled)
        {
            DrawTitleBar(state, prev_node);
        }
    }

    state->focused_window = node->frame;
    XRaiseWindow(state->display, node->frame);

    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }

    XSetInputFocus(state->display, node->client, RevertToParent, CurrentTime);

    XWindowAttributes attr;
    if (XGetWindowAttributes(state->display, node->client, &attr))
    {
        if (attr.map_state == IsViewable)
        {
            XEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.xfocus.type = FocusIn;
            ev.xfocus.window = node->client;
            ev.xfocus.mode = NotifyNormal;
            ev.xfocus.detail = NotifyAncestor;
            XSendEvent(state->display, node->client, False, FocusChangeMask, &ev);
        }
    }
}

static void FocusRootWindow(GooeyShellState *state)
{
    if (!ValidateWindowState(state))
        return;

    state->focused_window = None;
    XSetInputFocus(state->display, state->root, RevertToPointerRoot, CurrentTime);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xfocus.type = FocusIn;
    ev.xfocus.window = state->root;
    ev.xfocus.mode = NotifyNormal;
    ev.xfocus.detail = NotifyAncestor;
    XSendEvent(state->display, state->root, False, FocusChangeMask, &ev);
}

static WindowNode *GetNextWindow(GooeyShellState *state, WindowNode *current)
{
    if (!current)
    {
        Workspace *ws = GetCurrentWorkspace(state);
        WindowNode *node = ws->windows;
        while (node)
        {
            if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app)
            {
                return node;
            }
            node = node->next;
        }
        return NULL;
    }

    WindowNode *node = current->next;
    while (node)
    {
        if (!node->is_minimized && node->workspace == state->current_workspace &&
            !node->is_desktop_app && !node->is_fullscreen_app)
        {
            return node;
        }
        node = node->next;
    }

    Workspace *ws = GetCurrentWorkspace(state);
    node = ws->windows;
    while (node && node != current)
    {
        if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app)
        {
            return node;
        }
        node = node->next;
    }

    return current;
}

static WindowNode *GetPreviousWindow(GooeyShellState *state, WindowNode *current)
{
    if (!current)
    {
        Workspace *ws = GetCurrentWorkspace(state);
        WindowNode *node = ws->windows;
        WindowNode *last = NULL;
        while (node)
        {
            if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app)
            {
                last = node;
            }
            node = node->next;
        }
        return last;
    }

    WindowNode *node = current->prev;
    while (node)
    {
        if (!node->is_minimized && node->workspace == state->current_workspace &&
            !node->is_desktop_app && !node->is_fullscreen_app)
        {
            return node;
        }
        node = node->prev;
    }

    Workspace *ws = GetCurrentWorkspace(state);
    WindowNode *last = NULL;
    node = ws->windows;
    while (node)
    {
        if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app)
        {
            last = node;
        }
        node = node->next;
    }

    return last ? last : current;
}

void GooeyShell_TileWindows(GooeyShellState *state)
{
    printf("GooeyShell_TileWindows called\n");
    Workspace *workspace = GetCurrentWorkspace(state);
    if (workspace)
    {
        int tiled_count = 0;
        WindowNode *node = workspace->windows;
        while (node)
        {
            if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
                !node->is_desktop_app && !node->is_fullscreen_app)
            {
                tiled_count++;
            }
            node = node->next;
        }
        printf("Tiling %d windows on workspace %d\n", tiled_count, workspace->number);
        TileWindowsOnWorkspace(state, workspace);
    }
}

void GooeyShell_ToggleFloating(GooeyShellState *state, Window client)
{
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

    int old_x = node->x;
    int old_y = node->y;
    int old_width = node->width;
    int old_height = node->height;

    node->is_floating = !node->is_floating;

    if (!node->is_floating)
    {
        printf("Window returning to tiling layout\n");

        if (node->tiling_x != -1 && node->tiling_y != -1 &&
            node->tiling_width != -1 && node->tiling_height != -1)
        {
            node->x = node->tiling_x;
            node->y = node->tiling_y;
            node->width = node->tiling_width;
            node->height = node->tiling_height;
        }

        GooeyShell_TileWindows(state);
    }
    else
    {
        printf("Window becoming floating\n");

        node->tiling_x = old_x;
        node->tiling_y = old_y;
        node->tiling_width = old_width;
        node->tiling_height = old_height;

        Monitor *mon = &state->monitor_info.monitors[node->monitor_number];

        if (node->width < 100 || node->height < 100)
        {
            node->width = mon->width / 2;
            node->height = mon->height / 2;
        }

        node->x = mon->x + (mon->width - node->width) / 2;
        node->y = mon->y + BAR_HEIGHT + (mon->height - BAR_HEIGHT - node->height) / 2;

        if (node->y + node->height > mon->y + mon->height)
        {
            node->y = mon->y + mon->height - node->height;
        }
        if (node->y < mon->y + BAR_HEIGHT)
        {
            node->y = mon->y + BAR_HEIGHT;
        }

        UpdateWindowGeometry(state, node);
        XRaiseWindow(state->display, node->frame);
    }
}

void GooeyShell_FocusNextWindow(GooeyShellState *state)
{
    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    WindowNode *next = GetNextWindow(state, current);
    if (next)
    {
        FocusWindow(state, next);
    }
}

void GooeyShell_FocusPreviousWindow(GooeyShellState *state)
{
    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    WindowNode *prev = GetPreviousWindow(state, current);
    if (prev)
    {
        FocusWindow(state, prev);
    }
}

void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace)
{
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node)
        return;

    RemoveWindowFromWorkspace(state, node);
    AddWindowToWorkspace(state, node, workspace);

    if (node->workspace == state->current_workspace)
    {
        XUnmapWindow(state->display, node->frame);
    }

    if (workspace == state->current_workspace)
    {
        XMapWindow(state->display, node->frame);
        if (node->is_minimized)
        {
            RestoreWindow(state, node);
        }
    }

    Workspace *old_ws = GetWorkspace(state, node->workspace);
    if (old_ws)
    {
        TileWindowsOnWorkspace(state, old_ws);
    }
    GooeyShell_TileWindows(state);
}

void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace)
{
    if (workspace == state->current_workspace)
        return;

    int old_workspace = state->current_workspace;

    WindowNode *node = state->window_list;
    while (node)
    {
        if (node->workspace == state->current_workspace && !node->is_desktop_app)
        {
            XUnmapWindow(state->display, node->frame);
        }
        node = node->next;
    }

    state->current_workspace = workspace;

    node = state->window_list;
    while (node)
    {
        if (node->workspace == state->current_workspace && !node->is_desktop_app)
        {
            XMapWindow(state->display, node->frame);
            if (node->is_minimized)
            {
                RestoreWindow(state, node);
            }
        }
        node = node->next;
    }

    SendWorkspaceChangedThroughDBus(state, old_workspace, workspace);

    GooeyShell_TileWindows(state);
    RegrabKeys(state);
}

void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout)
{
    Workspace *workspace = GetCurrentWorkspace(state);
    workspace->layout = layout;
    state->current_layout = layout;

    TileWindowsOnWorkspace(state, workspace);
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

    InitializeWorkspaces(state);

    state->desktop_app_window = None;
    state->fullscreen_app_window = None;
    state->focused_window = None;
    state->drag_window = None;
    state->is_tiling_resizing = False;

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
    state->resize_cursor = XCreateFontCursor(state->display, XC_sb_h_double_arrow);
    state->v_resize_cursor = XCreateFontCursor(state->display, XC_sb_v_double_arrow);
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

    GrabKeys(state);

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

    FocusRootWindow(state);
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
        int y = mon->y + BAR_HEIGHT + (mon->height - BAR_HEIGHT - client_height) / 2;

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

            printf("Tiling %d windows after new window creation\n", tiled_count);
            TileWindowsOnWorkspace(state, ws);
        }
    }

    RegrabKeys(state);
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

                printf("Tiling %d windows after window removal\n", tiled_count);
                TileWindowsOnWorkspace(state, ws);
            }

            break;
        }
        current = &(*current)->next;
    }
    RegrabKeys(state);
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
        FocusRootWindow(state);
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

        XMoveResizeWindow(state->display, node->frame,
                          mon->x, mon->y + BAR_HEIGHT,
                          mon->width, mon->height - BAR_HEIGHT);
        XResizeWindow(state->display, node->client,
                      mon->width - 2 * BORDER_WIDTH,
                      mon->height - BAR_HEIGHT - TITLE_BAR_HEIGHT - 2 * BORDER_WIDTH);
        node->is_fullscreen = True;

        SendWindowStateThroughDBus(state, node->frame, "fullscreen");
    }

    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
}

static void UpdateCursorForWindow(GooeyShellState *state, WindowNode *node, int x, int y)
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

static void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev)
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

static void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev)
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

static void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

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

        if (new_x < mon->x)
            new_x = mon->x;
        if (new_y < mon->y + BAR_HEIGHT)
            new_y = mon->y + BAR_HEIGHT;
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
        int delta_y = ev->x_root - state->drag_start_y;

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
        if (node->width > mon->width - 2 * BORDER_WIDTH)
            node->width = mon->width - 2 * BORDER_WIDTH;
        if (node->height > mon->height - TITLE_BAR_HEIGHT - 2 * BORDER_WIDTH - BAR_HEIGHT)
            node->height = mon->height - TITLE_BAR_HEIGHT - 2 * BORDER_WIDTH - BAR_HEIGHT;

        node->floating_width = node->width;
        node->floating_height = node->height;

        UpdateWindowGeometry(state, node);
    }
    else
    {
        UpdateCursorForWindow(state, node, ev->x, ev->y);
    }
}

static void ReapZombieProcesses(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
    }
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

    if (state->focused_window == node->frame)
    {
        FocusRootWindow(state);
    }

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
                KeySym keysym = XKeycodeToKeysym(state->display, ev.xkey.keycode, 0);

                if (ev.xkey.state & Mod1Mask)
                {
                    printf("Alt key combination detected\n");
                    switch (keysym)
                    {
                    case XK_Return:
                        printf("Alt+Enter: Launching terminal\n");
                        GooeyShell_AddWindow(state, "xterm", 0);
                        break;
                    case XK_q:
                        printf("Alt+Q: Closing window\n");
                        if (state->focused_window != None)
                        {
                            WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                            if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                            {
                                CloseWindow(state, node);
                            }
                        }
                        break;
                    case XK_f:
                        printf("Alt+F: Toggling floating\n");
                        if (state->focused_window != None)
                        {
                            WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                            if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                            {
                                GooeyShell_ToggleFloating(state, node->client);
                            }
                        }
                        break;
                    case XK_j:
                        printf("Alt+J: Focusing next window\n");
                        GooeyShell_FocusNextWindow(state);
                        break;
                    case XK_k:
                        printf("Alt+K: Focusing previous window\n");
                        GooeyShell_FocusPreviousWindow(state);
                        break;
                    case XK_t:
                        printf("Alt+T: Switching to tiling layout\n");
                        GooeyShell_SetLayout(state, LAYOUT_TILING);
                        break;
                    case XK_m:
                        printf("Alt+M: Switching to monocle layout\n");
                        GooeyShell_SetLayout(state, LAYOUT_MONOCLE);
                        break;
                    case XK_h:
                        printf("Alt+H: Make window narrower\n");
                        {
                            Workspace *ws = GetCurrentWorkspace(state);
                            if (ws && ws->tiling_root && state->focused_window != None)
                            {
                                WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                                if (node && !node->is_floating)
                                {

                                    HandleTilingResize(state, node, 1, -30, 0);
                                }
                            }
                        }
                        break;
                    case XK_l:
                        printf("Alt+L: Make window wider\n");
                        {
                            Workspace *ws = GetCurrentWorkspace(state);
                            if (ws && ws->tiling_root && state->focused_window != None)
                            {
                                WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                                if (node && !node->is_floating)
                                {

                                    HandleTilingResize(state, node, 1, 30, 0);
                                }
                            }
                        }
                        break;
                    case XK_y:
                        printf("Alt+J: Make window shorter\n");
                        {
                            Workspace *ws = GetCurrentWorkspace(state);
                            if (ws && ws->tiling_root && state->focused_window != None)
                            {
                                WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                                if (node && !node->is_floating)
                                {

                                    HandleTilingResize(state, node, 2, 0, -30);
                                }
                            }
                        }
                        break;
                    case XK_n:
                        printf("Alt+K: Make window taller\n");
                        {
                            Workspace *ws = GetCurrentWorkspace(state);
                            if (ws && ws->tiling_root && state->focused_window != None)
                            {
                                WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                                if (node && !node->is_floating)
                                {

                                    HandleTilingResize(state, node, 2, 0, 30);
                                }
                            }
                        }
                        break;
                    case XK_space:
                        printf("Alt+Space: Toggle layout\n");
                        {
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
                        break;
                    case XK_1:
                    case XK_2:
                    case XK_3:
                    case XK_4:
                    case XK_5:
                    case XK_6:
                    case XK_7:
                    case XK_8:
                    case XK_9:
                        printf("Alt+%c: Switching to workspace %ld\n",
                               (char)keysym, keysym - XK_1 + 1);
                        GooeyShell_SwitchWorkspace(state, keysym - XK_1 + 1);
                        break;
                    case XK_F4:
                        printf("Alt+F4: Closing window\n");
                        if (state->focused_window != None)
                        {
                            WindowNode *node = FindWindowNodeByFrame(state, state->focused_window);
                            if (node && !node->is_desktop_app && !node->is_fullscreen_app)
                            {
                                CloseWindow(state, node);
                            }
                        }
                        break;
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

int GooeyShell_IsWindowMinimized(GooeyShellState *state, Window client)
{
    if (!state)
        return 0;

    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node)
        return 0;

    return node->is_minimized;
}

static void CleanupWorkspace(Workspace *ws)
{
    if (!ws)
        return;
    if (ws->stack_ratios)
    {
        free(ws->stack_ratios);
    }
    if (ws->tiling_root)
    {
        FreeTilingTree(ws->tiling_root);
    }
    free(ws);
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
    if (state->v_resize_cursor)
        XFreeCursor(state->display, state->v_resize_cursor);
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