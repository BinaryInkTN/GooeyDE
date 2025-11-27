#ifndef GOOEY_SHELL_H
#define GOOEY_SHELL_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dbus/dbus.h>
#include <GLPS/glps_thread.h>

#define WINDOW_MANAGER_NAME "GooeyShell"
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define TITLE_BAR_HEIGHT 30
#define BORDER_WIDTH 2
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 6
#define BUTTON_SPACING 8
#define RESIZE_HANDLE_SIZE 6

typedef enum {
    LAYOUT_TILING,
    LAYOUT_MONOCLE,
    LAYOUT_FLOATING
} LayoutMode;

typedef enum {
    SPLIT_HORIZONTAL,
    SPLIT_VERTICAL,
    SPLIT_NONE
} SplitDirection;

typedef struct {
    int x, y;
    int width, height;
    int number;
} Monitor;

typedef struct {
    Monitor *monitors;
    int num_monitors;
    int primary_monitor;
} MonitorInfo;

typedef struct WindowNode {
    Window frame;
    Window client;
    char *title;
    int x, y;
    int width, height;
    int monitor_number;
    int is_minimized;
    int is_fullscreen;
    int is_floating;
    int is_tiled;
    int is_titlebar_disabled;
    int is_desktop_app;
    int is_fullscreen_app;
    int stay_on_top;
    int workspace;
    int stack_index;
    
    int tiling_x;
    int tiling_y;
    int tiling_width;
    int tiling_height;
    int floating_x;
    int floating_y;
    int floating_width;
    int floating_height;
    
    struct WindowNode *next;
    struct WindowNode *prev;
} WindowNode;

typedef struct TilingNode {
    WindowNode *window;
    struct TilingNode *left;
    struct TilingNode *right;
    struct TilingNode *parent;
    int x, y, width, height;
    float ratio;
    SplitDirection split;
    int is_leaf;
} TilingNode;

typedef struct Workspace {
    int number;
    LayoutMode layout;
    float master_ratio;
    float *stack_ratios;
    int stack_ratios_count;
    WindowNode *windows;
    TilingNode *tiling_root;
    struct Workspace *next;
} Workspace;

typedef struct {
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

typedef struct {
    Display *display;
    int screen;
    Window root;
    GC gc;
    GC titlebar_gc;
    GC text_gc;
    GC button_gc;

    unsigned long titlebar_color;
    unsigned long titlebar_focused_color;
    unsigned long text_color;
    unsigned long border_color;
    unsigned long button_color;
    unsigned long close_button_color;
    unsigned long minimize_button_color;
    unsigned long maximize_button_color;
    unsigned long bg_color;

    Cursor move_cursor;
    Cursor resize_cursor;
    Cursor v_resize_cursor;
    Cursor normal_cursor;
    Cursor custom_cursor;

    WindowNode *window_list;
    Window focused_window;
    Window desktop_app_window;
    Window fullscreen_app_window;

    MonitorInfo monitor_info;

    Workspace *workspaces;
    int current_workspace;
    LayoutMode current_layout;

    Window drag_window;
    int is_dragging;
    int is_resizing;
    int is_tiling_resizing;
    int drag_start_x, drag_start_y;
    int original_x, original_y;
    int original_width, original_height;
    int resize_direction;
    int tiling_resize_edge;

    unsigned int mod_key;
    int super_pressed;

    int inner_gap;
    int outer_gap;

    DBusConnection *dbus_connection;
    DBusError dbus_error;
    int is_dbus_init;

    PrecomputedAtoms atoms;
} GooeyShellState;

GooeyShellState *GooeyShell_Init(void);
void GooeyShell_RunEventLoop(GooeyShellState *state);
void GooeyShell_Cleanup(GooeyShellState *state);
void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app);
void GooeyShell_AddFullscreenApp(GooeyShellState *state, const char *command, int stay_on_top);
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client);
void GooeyShell_ToggleFloating(GooeyShellState *state, Window client);
void GooeyShell_TileWindows(GooeyShellState *state);
void GooeyShell_FocusNextWindow(GooeyShellState *state);
void GooeyShell_FocusPreviousWindow(GooeyShellState *state);
void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace);
void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace);
void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout);
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client);
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled);
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client);
Window *GooeyShell_GetOpenedWindows(GooeyShellState *state, int *count);
int GooeyShell_IsWindowOpened(GooeyShellState *state, Window window);
int GooeyShell_IsWindowMinimized(GooeyShellState *state, Window client);

#endif