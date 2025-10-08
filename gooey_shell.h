#ifndef GOOEY_SHELL_H
#define GOOEY_SHELL_H

#include <X11/Xlib.h>
#include <dbus-1.0/dbus/dbus.h>
#include <stdbool.h>

#define WINDOW_MANAGER_NAME "GooeyShell"
#define TITLE_BAR_HEIGHT 24
#define BORDER_WIDTH 2
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 6
#define BUTTON_SPACING 4
#define RESIZE_HANDLE_SIZE 8
#define MIN_WINDOW_WIDTH 100
#define MIN_WINDOW_HEIGHT 80
#define DEFAULT_WIDTH 400
#define DEFAULT_HEIGHT 300

typedef struct WindowNode WindowNode;
typedef struct GooeyShellState GooeyShellState;
typedef struct
{
    Atom net_wm_name;
    Atom utf8_string;
    Atom wm_protocols;
    Atom wm_delete_window;
    Atom net_wm_window_type;
    Atom net_wm_window_type_desktop;
    Atom net_wm_window_type_dock;
    Atom net_wm_window_type_normal;
    Atom net_wm_window_type_toolbar;
    Atom net_wm_window_type_menu;
    Atom net_wm_state;
    Atom net_wm_state_below;
    Atom net_wm_state_above;
    Atom net_wm_state_fullscreen;
    Atom net_wm_state_skip_taskbar;
    Atom net_wm_state_skip_pager;
    Atom net_wm_state_sticky;
    Atom net_wm_state_hidden;
    Atom gooey_fullscreen_app;
    Atom gooey_stay_on_top;
    Atom gooey_desktop_app;
} PrecomputedAtoms;

typedef struct
{
    int x, y;
    int width, height;
    int number;
} Monitor;

typedef struct
{
    Monitor *monitors;
    int num_monitors;
    int primary_monitor;
} MonitorInfo;
struct WindowNode
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
    Bool is_fullscreen_app;
    Bool is_minimized;
    Bool stay_on_top;
    int monitor_number;

    WindowNode *next;
};

struct GooeyShellState
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
    Cursor custom_cursor;
    WindowNode *window_list;
    Window focused_window;
    unsigned long bg_color;
    unsigned long titlebar_color;
    unsigned long titlebar_focused_color;
    unsigned long border_color;
    unsigned long text_color;
    unsigned long button_color;
    unsigned long close_button_color;
    unsigned long minimize_button_color;
    unsigned long maximize_button_color;

    Bool is_dragging;
    Bool is_resizing;
    Window drag_window;
    int drag_start_x;
    int drag_start_y;
    int window_start_x;
    int window_start_y;
    int window_start_width;
    int window_start_height;
    MonitorInfo monitor_info;

    Window desktop_app_window;
    Window fullscreen_app_window;

    bool is_dbus_init;
    // DBUS Connection
    DBusConnection *dbus_connection;
    struct DBusError dbus_error;
};

GooeyShellState *GooeyShell_Init(void);
void GooeyShell_RunEventLoop(GooeyShellState *state);
void GooeyShell_AddFullscreenApp(GooeyShellState *state, const char *command, int stay_on_top);
void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app);
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client);
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client);
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled);
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client);
void GooeyShell_Cleanup(GooeyShellState *state);

#endif