#include "gooey_shell.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct WindowNode
{
    Window frame;
    Window client;
    int x, y;
    int width, height;
    char *title;
    Bool mapped;
    Bool is_fullscreen;
    Bool is_titlebar_disabled;
    Bool is_desktop_app;
    struct WindowNode *next;
} WindowNode;

typedef struct GooeyShellState
{
    Display *display;
    int screen;
    Window root;
    GC gc;
    GC titlebar_gc;
    GC text_gc;
    GC button_gc;
    Cursor move_cursor;
    Cursor resize_cursor;
    Cursor normal_cursor;
    WindowNode *window_list;
    Window focused_window;
    unsigned long bg_color;
    unsigned long titlebar_color;
    unsigned long titlebar_focused_color;
    unsigned long border_color;
    unsigned long text_color;
    unsigned long button_color;
    unsigned long close_button_color;

    Bool is_dragging;
    Bool is_resizing;
    Window drag_window;
    int drag_start_x;
    int drag_start_y;
    int window_start_x;
    int window_start_y;
    int window_start_width;
    int window_start_height;

    Window desktop_app_window;
} GooeyShellState;

static void CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app);
static void DrawTitleBar(GooeyShellState *state, WindowNode *node);
static void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev);
static void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev);
static void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev);
static WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame);
static WindowNode *FindWindowNodeByClient(GooeyShellState *state, Window client);
static void CloseWindow(GooeyShellState *state, WindowNode *node);
static void ToggleFullscreen(GooeyShellState *state, WindowNode *node);
static int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y);
static void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node);
static void SetupDesktopApp(GooeyShellState *state, WindowNode *node);
static void EnsureDesktopAppStaysInBackground(GooeyShellState *state);
static void ForceWindowAsDesktopApp(GooeyShellState *state, Window client);

GooeyShellState *GooeyShell_Init(void)
{
    GooeyShellState *state = malloc(sizeof(GooeyShellState));
    if (!state)
    {
        fprintf(stderr, "Unable to allocate memory for GooeyShellState.\n");
        return NULL;
    }

    state->display = XOpenDisplay(NULL);
    if (!state->display)
    {
        fprintf(stderr, "Unable to open X display.\n");
        free(state);
        return NULL;
    }

    state->screen = DefaultScreen(state->display);
    state->root = RootWindow(state->display, state->screen);
    state->gc = XCreateGC(state->display, state->root, 0, NULL);
    state->window_list = NULL;
    state->focused_window = None;
    state->desktop_app_window = None;

    state->is_dragging = False;
    state->is_resizing = False;
    state->drag_window = None;
    state->drag_start_x = 0;
    state->drag_start_y = 0;
    state->window_start_x = 0;
    state->window_start_y = 0;
    state->window_start_width = 0;
    state->window_start_height = 0;

    XGCValues gcv;
    state->titlebar_color = 0x212121;
    state->titlebar_focused_color = 0x424242;
    state->text_color = WhitePixel(state->display, state->screen);
    state->border_color = 0x666666;
    state->button_color = 0xDDDDDD;
    state->close_button_color = 0xFF4444;

    gcv.foreground = state->titlebar_color;
    state->titlebar_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    gcv.foreground = state->text_color;
    state->text_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    gcv.foreground = state->button_color;
    state->button_gc = XCreateGC(state->display, state->root, GCForeground, &gcv);

    state->move_cursor = XCreateFontCursor(state->display, XC_fleur);
    state->resize_cursor = XCreateFontCursor(state->display, XC_bottom_right_corner);
    state->normal_cursor = XCreateFontCursor(state->display, XC_left_ptr);

    state->bg_color = 0x333333;

    XSetWindowBackground(state->display, state->root, state->bg_color);

    XSelectInput(state->display, state->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     KeyPressMask | KeyReleaseMask | ExposureMask);

    XChangeProperty(state->display, state->root,
                    XInternAtom(state->display, "_NET_WM_NAME", False),
                    XInternAtom(state->display, "UTF8_STRING", False),
                    8, PropModeReplace,
                    (unsigned char *)WINDOW_MANAGER_NAME,
                    strlen(WINDOW_MANAGER_NAME));

    XClearWindow(state->display, state->root);
    XFlush(state->display);

    printf("GooeyShell initialized successfully\n");
    return state;
}

void GooeyShell_SetBackground(GooeyShellState *state, unsigned long color)
{
    if (!state || !state->display)
        return;

    state->bg_color = color;
    XSetWindowBackground(state->display, state->root, color);
    XClearWindow(state->display, state->root);
    XFlush(state->display);
}

static void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return;

    if (node->is_desktop_app)
    {

        int screen_width = DisplayWidth(state->display, state->screen);
        int screen_height = DisplayHeight(state->display, state->screen);

        XMoveResizeWindow(state->display, node->frame, 0, 0, screen_width, screen_height);
        XMoveResizeWindow(state->display, node->client, 0, 0, screen_width, screen_height);
    }
    else
    {
        int frame_width = node->width + 2 * BORDER_WIDTH;
        int frame_height = node->height + (node->is_titlebar_disabled ? 0 : TITLE_BAR_HEIGHT) + 2 * BORDER_WIDTH;

        XResizeWindow(state->display, node->frame, frame_width, frame_height);

        if (node->is_titlebar_disabled)
        {
            XMoveWindow(state->display, node->client, BORDER_WIDTH, BORDER_WIDTH);
        }
        else
        {
            XMoveWindow(state->display, node->client, BORDER_WIDTH, TITLE_BAR_HEIGHT + BORDER_WIDTH);
        }

        XResizeWindow(state->display, node->client, node->width, node->height);
    }
}

static void ForceWindowAsDesktopApp(GooeyShellState *state, Window client)
{

    Atom net_wm_window_type = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE", False);
    Atom net_wm_window_type_desktop = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    if (net_wm_window_type != None && net_wm_window_type_desktop != None)
    {
        XChangeProperty(state->display, client, net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&net_wm_window_type_desktop, 1);
        printf("Forced window %lu to be desktop app via property\n", client);
    }
}

static void SetupDesktopApp(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return;

    node->is_desktop_app = True;
    node->is_titlebar_disabled = True;
    node->is_fullscreen = True;

    int screen_width = DisplayWidth(state->display, state->screen);
    int screen_height = DisplayHeight(state->display, state->screen);

    XSetWindowBorderWidth(state->display, node->frame, 0);

    XMoveResizeWindow(state->display, node->frame, 0, 0, screen_width, screen_height);
    XMoveResizeWindow(state->display, node->client, 0, 0, screen_width, screen_height);

    Atom net_wm_window_type = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE", False);
    Atom net_wm_window_type_desktop = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

    if (net_wm_window_type != None && net_wm_window_type_desktop != None)
    {
        XChangeProperty(state->display, node->client, net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&net_wm_window_type_desktop, 1);
        XChangeProperty(state->display, node->frame, net_wm_window_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&net_wm_window_type_desktop, 1);
    }

    Atom net_wm_state = XInternAtom(state->display, "_NET_WM_STATE", False);
    Atom net_wm_state_below = XInternAtom(state->display, "_NET_WM_STATE_BELOW", False);
    Atom net_wm_state_skip_taskbar = XInternAtom(state->display, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom net_wm_state_skip_pager = XInternAtom(state->display, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom net_wm_state_sticky = XInternAtom(state->display, "_NET_WM_STATE_STICKY", False);

    if (net_wm_state != None && net_wm_state_below != None)
    {
        Atom states[5] = {net_wm_state_below, net_wm_state_skip_taskbar, net_wm_state_skip_pager, net_wm_state_sticky, None};
        XChangeProperty(state->display, node->client, net_wm_state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)states, 4);
        XChangeProperty(state->display, node->frame, net_wm_state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)states, 4);
    }

    XLowerWindow(state->display, node->frame);

    state->desktop_app_window = node->frame;

    printf("Setup desktop app: %s (frame=%lu) - fullscreen, no titlebar, locked to background\n", node->title, node->frame);
}

static void EnsureDesktopAppStaysInBackground(GooeyShellState *state)
{
    if (state->desktop_app_window != None)
    {
        WindowNode *desktop_node = FindWindowNodeByFrame(state, state->desktop_app_window);
        if (desktop_node)
        {

            XLowerWindow(state->display, state->desktop_app_window);
        }
    }
}

static void CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app)
{
    XWindowAttributes attr;
    if (XGetWindowAttributes(state->display, client, &attr) == 0)
    {
        fprintf(stderr, "Failed to get window attributes for client %lu\n", client);
        return;
    }

    int client_width = (attr.width > 0) ? attr.width : DEFAULT_WIDTH;
    int client_height = (attr.height > 0) ? attr.height : DEFAULT_HEIGHT;

    int frame_width, frame_height;
    Window frame;

    if (is_desktop_app)
    {

        int screen_width = DisplayWidth(state->display, state->screen);
        int screen_height = DisplayHeight(state->display, state->screen);

        frame_width = screen_width;
        frame_height = screen_height;

        frame = XCreateSimpleWindow(state->display, state->root,
                                    0, 0,
                                    frame_width, frame_height,
                                    0,
                                    state->border_color, state->bg_color);

        ForceWindowAsDesktopApp(state, client);
    }
    else
    {

        frame_width = client_width + 2 * BORDER_WIDTH;
        frame_height = client_height + TITLE_BAR_HEIGHT + 2 * BORDER_WIDTH;

        frame = XCreateSimpleWindow(state->display, state->root,
                                    100, 100,
                                    frame_width, frame_height,
                                    BORDER_WIDTH,
                                    state->border_color, state->bg_color);
    }

    WindowNode *new_node = malloc(sizeof(WindowNode));
    if (!new_node)
    {
        fprintf(stderr, "Failed to allocate memory for WindowNode\n");
        XDestroyWindow(state->display, frame);
        return;
    }

    new_node->frame = frame;
    new_node->client = client;
    new_node->x = is_desktop_app ? 0 : 100;
    new_node->y = is_desktop_app ? 0 : 100;
    new_node->width = is_desktop_app ? DisplayWidth(state->display, state->screen) : client_width;
    new_node->height = is_desktop_app ? DisplayHeight(state->display, state->screen) : client_height;
    new_node->title = strdup("Untitled");
    new_node->mapped = False;
    new_node->is_fullscreen = is_desktop_app;
    new_node->is_titlebar_disabled = is_desktop_app;
    new_node->is_desktop_app = is_desktop_app;
    new_node->next = state->window_list;
    state->window_list = new_node;

    XTextProperty text_prop;
    if (XGetWMName(state->display, client, &text_prop) && text_prop.value)
    {
        free(new_node->title);
        new_node->title = strdup((char *)text_prop.value);
        XFree(text_prop.value);
    }

    if (is_desktop_app)
    {

        XSelectInput(state->display, frame, ExposureMask | StructureNotifyMask);
        XSelectInput(state->display, client, PropertyChangeMask | StructureNotifyMask);

        XReparentWindow(state->display, client, frame, 0, 0);
        XResizeWindow(state->display, client, new_node->width, new_node->height);

        SetupDesktopApp(state, new_node);
    }
    else
    {

        XSelectInput(state->display, frame,
                     ExposureMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | StructureNotifyMask | EnterWindowMask | LeaveWindowMask);
        XSelectInput(state->display, client, PropertyChangeMask | StructureNotifyMask);

        XReparentWindow(state->display, client, frame, BORDER_WIDTH, TITLE_BAR_HEIGHT);
        XResizeWindow(state->display, client, client_width, client_height);

        EnsureDesktopAppStaysInBackground(state);
    }

    XSetWindowBackground(state->display, frame, state->bg_color);

    Atom wm_protocols = XInternAtom(state->display, "WM_PROTOCOLS", False);
    Atom wm_delete_window = XInternAtom(state->display, "WM_DELETE_WINDOW", False);
    if (wm_protocols != None && wm_delete_window != None)
    {
        Atom protocols[] = {wm_delete_window};
        XSetWMProtocols(state->display, client, protocols, 1);
    }

    XMapWindow(state->display, frame);
    XMapWindow(state->display, client);

    if (!is_desktop_app)
    {
        state->focused_window = frame;
        XRaiseWindow(state->display, frame);
        DrawTitleBar(state, new_node);
    }

    printf("Created %s window: %s (titlebar: %s, fullscreen: %s)\n",
           is_desktop_app ? "desktop" : "regular",
           new_node->title,
           new_node->is_titlebar_disabled ? "disabled" : "enabled",
           new_node->is_fullscreen ? "yes" : "no");
}

void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client)
{
    if (!state || !state->display)
        return;

    WindowNode *node = FindWindowNodeByClient(state, client);
    if (node)
    {
        SetupDesktopApp(state, node);
    }
    else
    {
        printf("Window %lu will be marked as desktop app when it maps\n", client);
    }
}

static void RemoveWindow(GooeyShellState *state, Window client)
{
    WindowNode **current = &state->window_list;
    while (*current)
    {
        if ((*current)->client == client)
        {
            WindowNode *to_free = *current;
            *current = (*current)->next;

            if (state->desktop_app_window == to_free->frame)
            {
                state->desktop_app_window = None;
            }

            XDestroyWindow(state->display, to_free->frame);

            free(to_free->title);
            free(to_free);
            break;
        }
        current = &(*current)->next;
    }
}

static WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame)
{
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
    if (!node)
        return;

    if (node->is_titlebar_disabled || node->is_desktop_app)
        return;

    unsigned long title_color = (state->focused_window == node->frame) ? state->titlebar_focused_color : state->titlebar_color;

    XSetForeground(state->display, state->titlebar_gc, title_color);
    XFillRectangle(state->display, node->frame, state->titlebar_gc,
                   BORDER_WIDTH, BORDER_WIDTH,
                   node->width, TITLE_BAR_HEIGHT);

    int close_button_x = node->width - BUTTON_SIZE - BUTTON_MARGIN;
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
    XDrawString(state->display, node->frame, state->text_gc,
                BORDER_WIDTH + 5, BORDER_WIDTH + 15,
                node->title, strlen(node->title));
}

static int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y)
{
    if (node->is_titlebar_disabled || node->is_desktop_app)
        return 0;

    if (y < BORDER_WIDTH || y > BORDER_WIDTH + TITLE_BAR_HEIGHT)
        return 0;

    int close_button_x = node->width - BUTTON_SIZE - BUTTON_MARGIN;

    if (x >= close_button_x && x <= close_button_x + BUTTON_SIZE &&
        y >= BORDER_WIDTH + BUTTON_MARGIN && y <= BORDER_WIDTH + BUTTON_MARGIN + BUTTON_SIZE)
    {
        return 1;
    }

    if (x >= node->width - 15 && x <= node->width &&
        y >= node->height + TITLE_BAR_HEIGHT - 15 && y <= node->height + TITLE_BAR_HEIGHT)
    {
        return 2;
    }

    return 0;
}

static void CloseWindow(GooeyShellState *state, WindowNode *node)
{
    if (!node)
        return;

    printf("Closing window: %s (client=%lu, frame=%lu)\n", node->title, node->client, node->frame);

    Atom wm_protocols = XInternAtom(state->display, "WM_PROTOCOLS", False);
    Atom wm_delete_window = XInternAtom(state->display, "WM_DELETE_WINDOW", False);

    if (wm_protocols != None && wm_delete_window != None)
    {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = node->client;
        ev.xclient.message_type = wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;

        if (XSendEvent(state->display, node->client, False, NoEventMask, &ev))
        {
            printf("Sent WM_DELETE_WINDOW to client %lu\n", node->client);
            XFlush(state->display);
            return;
        }
    }

    printf("Force destroying window\n");

    WindowNode **current = &state->window_list;
    while (*current)
    {
        if (*current == node)
        {
            *current = node->next;
            break;
        }
        current = &(*current)->next;
    }

    XUnmapWindow(state->display, node->frame);
    XUnmapWindow(state->display, node->client);

    XDestroyWindow(state->display, node->client);
    XDestroyWindow(state->display, node->frame);

    if (state->focused_window == node->frame)
    {
        state->focused_window = None;
    }

    free(node->title);
    free(node);
}

static void ToggleFullscreen(GooeyShellState *state, WindowNode *node)
{
    if (!node || node->is_desktop_app)
        return;

    if (node->is_fullscreen)
    {
        XMoveResizeWindow(state->display, node->frame,
                          node->x, node->y,
                          node->width + 2 * BORDER_WIDTH,
                          node->height + (node->is_titlebar_disabled ? 0 : TITLE_BAR_HEIGHT) + 2 * BORDER_WIDTH);
        XResizeWindow(state->display, node->client, node->width, node->height);
        node->is_fullscreen = False;
        printf("Window %s restored from fullscreen\n", node->title);
    }
    else
    {
        XWindowAttributes attr;
        if (XGetWindowAttributes(state->display, node->frame, &attr))
        {
            node->x = attr.x;
            node->y = attr.y;
        }

        int screen_width = DisplayWidth(state->display, state->screen);
        int screen_height = DisplayHeight(state->display, state->screen);

        XMoveResizeWindow(state->display, node->frame, 0, 0, screen_width, screen_height);
        XResizeWindow(state->display, node->client,
                      screen_width - 2 * BORDER_WIDTH,
                      screen_height - (node->is_titlebar_disabled ? 0 : TITLE_BAR_HEIGHT) - 2 * BORDER_WIDTH);
        node->is_fullscreen = True;
        printf("Window %s set to fullscreen\n", node->title);
    }

    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }
}

void GooeyShell_RunEventLoop(GooeyShellState *state)
{
    if (!state || !state->display)
        return;

    XEvent ev;
    while (1)
    {
        XNextEvent(state->display, &ev);

        switch (ev.type)
        {
        case MapRequest:
        {
            Window client = ev.xmaprequest.window;

            XWindowAttributes attr;
            XGetWindowAttributes(state->display, client, &attr);
            if (attr.override_redirect)
            {
                XMapWindow(state->display, client);
                break;
            }

            printf("MapRequest for window %lu\n", client);

            int is_desktop_app = 0;

            Atom net_wm_window_type = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE", False);
            if (net_wm_window_type != None)
            {
                Atom actual_type;
                int actual_format;
                unsigned long nitems, bytes_after;
                unsigned char *prop_data = NULL;

                if (XGetWindowProperty(state->display, client, net_wm_window_type, 0, 1,
                                       False, XA_ATOM, &actual_type, &actual_format,
                                       &nitems, &bytes_after, &prop_data) == Success &&
                    prop_data)
                {
                    Atom window_type = ((Atom *)prop_data)[0];
                    Atom desktop_type = XInternAtom(state->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);

                    if (window_type == desktop_type)
                    {
                        is_desktop_app = 1;
                        printf("Detected desktop app via _NET_WM_WINDOW_TYPE_DESKTOP property\n");
                    }
                    XFree(prop_data);
                }
            }

            if (!is_desktop_app)
            {
                XTextProperty text_prop;
                if (XGetWMName(state->display, client, &text_prop) && text_prop.value)
                {
                    char *title = (char *)text_prop.value;
                    printf("Window title: %s\n", title);

                    if (strstr(title, "desktop") || strstr(title, "Desktop") ||
                        strstr(title, "wallpaper") || strstr(title, "Wallpaper") ||
                        strstr(title, "background") || strstr(title, "Background") ||
                        strstr(title, "taskbar") || strstr(title, "Taskbar"))
                    {
                        is_desktop_app = 1;
                        printf("Auto-detected desktop app by title: %s\n", title);
                    }
                    XFree(text_prop.value);
                }
            }

            if (!is_desktop_app)
            {
                XClassHint class_hint;
                if (XGetClassHint(state->display, client, &class_hint))
                {
                    if (class_hint.res_name && (strstr(class_hint.res_name, "desktop") ||
                                                strstr(class_hint.res_name, "taskbar")))
                    {
                        is_desktop_app = 1;
                        printf("Detected desktop app by class: %s\n", class_hint.res_name);
                    }
                    XFree(class_hint.res_name);
                    XFree(class_hint.res_class);
                }
            }

            CreateFrameWindow(state, client, is_desktop_app);
            break;
        }

        case UnmapNotify:
            printf("UnmapNotify for window %lu\n", ev.xunmap.window);
            break;

        case DestroyNotify:
            if (ev.xdestroywindow.window != state->root)
            {
                printf("DestroyNotify for window %lu\n", ev.xdestroywindow.window);
                RemoveWindow(state, ev.xdestroywindow.window);
            }
            break;

        case ClientMessage:
        {
            Atom wm_protocols = XInternAtom(state->display, "WM_PROTOCOLS", False);
            Atom wm_delete_window = XInternAtom(state->display, "WM_DELETE_WINDOW", False);

            if (ev.xclient.message_type == wm_protocols &&
                (Atom)ev.xclient.data.l[0] == wm_delete_window)
            {
                WindowNode *node = FindWindowNodeByClient(state, ev.xclient.window);
                if (node)
                {
                    printf("Received WM_DELETE_WINDOW for client %lu\n", ev.xclient.window);
                    CloseWindow(state, node);
                }
            }
            break;
        }

        case ConfigureRequest:
        {
            WindowNode *node = FindWindowNodeByClient(state, ev.xconfigurerequest.window);
            if (node && !node->is_fullscreen && !node->is_desktop_app)
            {
                node->width = (ev.xconfigurerequest.width > 0) ? ev.xconfigurerequest.width : node->width;
                node->height = (ev.xconfigurerequest.height > 0) ? ev.xconfigurerequest.height : node->height;

                UpdateWindowGeometry(state, node);

                if (!node->is_titlebar_disabled)
                {
                    DrawTitleBar(state, node);
                }

                printf("ConfigureRequest: resized window to %dx%d\n", node->width, node->height);
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

                XConfigureWindow(state->display,
                                 ev.xconfigurerequest.window,
                                 ev.xconfigurerequest.value_mask,
                                 &changes);
            }
            break;
        }

        case Expose:
        {
            WindowNode *node = FindWindowNodeByFrame(state, ev.xexpose.window);
            if (node && ev.xexpose.count == 0)
            {
                printf("Expose event for frame %lu\n", ev.xexpose.window);
                if (!node->is_titlebar_disabled && !node->is_desktop_app)
                {
                    DrawTitleBar(state, node);
                }
            }
            break;
        }

        case EnterNotify:
        {
            WindowNode *node = FindWindowNodeByFrame(state, ev.xcrossing.window);
            if (node && !node->is_titlebar_disabled && !node->is_desktop_app)
            {
                XDefineCursor(state->display, node->frame, state->normal_cursor);
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
                        if (node && !node->is_desktop_app)
                        {
                            printf("Closing window via Alt+F4\n");
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
                        if (node && !node->is_desktop_app)
                        {
                            printf("Toggling fullscreen via Alt+F11 or Alt+Enter\n");
                            ToggleFullscreen(state, node);
                        }
                    }
                }
            }
            break;
        }

        EnsureDesktopAppStaysInBackground(state);
    }
}

static void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node)
        return;

    if (node->is_desktop_app)
        return;

    if (node->is_titlebar_disabled)
        return;

    state->focused_window = ev->window;
    if (!node->is_titlebar_disabled)
    {
        DrawTitleBar(state, node);
    }

    XRaiseWindow(state->display, ev->window);

    int button_area = GetTitleBarButtonArea(state, node, ev->x, ev->y);

    if (button_area == 1 && ev->button == Button1)
    {
        printf("Close button clicked on window %lu\n", ev->window);
        CloseWindow(state, node);
        return;
    }

    if (ev->button == Button1)
    {
        if (button_area == 2)
        {
            printf("Resize corner clicked on window %lu\n", ev->window);
            state->is_resizing = True;
            state->drag_window = ev->window;
            state->drag_start_x = ev->x_root;
            state->drag_start_y = ev->y_root;
            state->window_start_width = node->width;
            state->window_start_height = node->height;

            XDefineCursor(state->display, ev->window, state->resize_cursor);

            int result = XGrabPointer(state->display, ev->window, False,
                                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                      GrabModeAsync, GrabModeAsync,
                                      None, state->resize_cursor, CurrentTime);

            if (result != GrabSuccess)
            {
                printf("Failed to grab pointer for resize: %d\n", result);
                state->is_resizing = False;
            }
        }
        else if (ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
        {
            printf("Titlebar click on window %lu\n", ev->window);

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

            int result = XGrabPointer(state->display, ev->window, False,
                                      ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                      GrabModeAsync, GrabModeAsync,
                                      None, state->move_cursor, CurrentTime);

            if (result != GrabSuccess)
            {
                printf("Failed to grab pointer for move: %d\n", result);
                state->is_dragging = False;
            }
        }
    }
    else if (ev->button == Button3 && ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH)
    {
        printf("Titlebar right-click on window %lu - toggling fullscreen\n", ev->window);
        ToggleFullscreen(state, node);
    }

    EnsureDesktopAppStaysInBackground(state);
}

static void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev)
{
    if ((state->is_dragging || state->is_resizing) && ev->button == Button1)
    {
        printf("Ending drag/resize on window %lu\n", ev->window);
        state->is_dragging = False;
        state->is_resizing = False;
        state->drag_window = None;
        XUngrabPointer(state->display, CurrentTime);

        WindowNode *node = FindWindowNodeByFrame(state, ev->window);
        if (node && !node->is_titlebar_disabled && !node->is_desktop_app)
        {
            XDefineCursor(state->display, ev->window, state->normal_cursor);
        }
    }

    EnsureDesktopAppStaysInBackground(state);
}

static void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev)
{
    WindowNode *node = FindWindowNodeByFrame(state, ev->window);
    if (!node)
        return;

    if (node->is_desktop_app)
        return;

    if (node->is_titlebar_disabled)
        return;

    int button_area = GetTitleBarButtonArea(state, node, ev->x, ev->y);
    if (button_area == 2)
    {
        XDefineCursor(state->display, ev->window, state->resize_cursor);
    }
    else if (ev->y < TITLE_BAR_HEIGHT + BORDER_WIDTH && button_area == 0)
    {
        XDefineCursor(state->display, ev->window, state->move_cursor);
    }
    else
    {
        XDefineCursor(state->display, ev->window, state->normal_cursor);
    }

    if (state->is_dragging && state->drag_window == ev->window && !node->is_fullscreen)
    {
        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->y_root - state->drag_start_y;

        int new_x = state->window_start_x + delta_x;
        int new_y = state->window_start_y + delta_y;

        int screen_width = DisplayWidth(state->display, state->screen);
        int screen_height = DisplayHeight(state->display, state->screen);

        if (new_x < 0)
            new_x = 0;
        if (new_y < 0)
            new_y = 0;
        if (new_x > screen_width - 50)
            new_x = screen_width - 50;
        if (new_y > screen_height - 50)
            new_y = screen_height - 50;

        XMoveWindow(state->display, ev->window, new_x, new_y);
        printf("Moving window to %d, %d\n", new_x, new_y);
    }
    else if (state->is_resizing && state->drag_window == ev->window && !node->is_fullscreen)
    {
        int delta_x = ev->x_root - state->drag_start_x;
        int delta_y = ev->x_root - state->drag_start_y;

        int new_width = state->window_start_width + delta_x;
        int new_height = state->window_start_height + delta_y;

        if (new_width < 100)
            new_width = 100;
        if (new_height < 100)
            new_height = 100;

        node->width = new_width;
        node->height = new_height;

        UpdateWindowGeometry(state, node);

        if (!node->is_titlebar_disabled)
        {
            DrawTitleBar(state, node);
        }

        printf("Resizing window to %dx%d\n", node->width, node->height);
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
        fprintf(stderr, "Failed to execute: %s\n", command);
        exit(1);
    }

    printf("Launched %s: %s (PID: %d)\n",
           desktop_app ? "desktop app" : "regular app", command, pid);
}

void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client)
{
    if (!state || !state->display)
        return;

    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app)
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

    printf("Titlebar %s for window %s\n",
           node->is_titlebar_disabled ? "disabled" : "enabled",
           node->title);
}

void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled)
{
    if (!state || !state->display)
        return;

    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app)
        return;

    if (node->is_titlebar_disabled == !enabled)
    {
        return;
    }

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

    printf("Titlebar %s for window %s\n",
           enabled ? "enabled" : "disabled",
           node->title);
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

void GooeyShell_Cleanup(GooeyShellState *state)
{
    if (!state)
        return;

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

        XDestroyWindow(state->display, current->client);
        XDestroyWindow(state->display, current->frame);

        free(current->title);
        free(current);
        current = next;
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
    if (state->display)
        XCloseDisplay(state->display);

    free(state);
}