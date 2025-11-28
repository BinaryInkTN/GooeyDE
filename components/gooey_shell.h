#ifndef GOOEY_SHELL_H
#define GOOEY_SHELL_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dbus/dbus.h>
#include <stdbool.h>

#define WINDOW_MANAGER_NAME "GooeyShell"
#define BAR_HEIGHT 30
#define TITLE_BAR_HEIGHT 24
#define BORDER_WIDTH 3
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 6
#define BUTTON_SPACING 4
#define RESIZE_HANDLE_SIZE 6
#define DEFAULT_WIDTH 400
#define DEFAULT_HEIGHT 300
#define WINDOW_OPACITY 0.95f
#define CONFIG_FILE "~/.config/gooey_shell/config"

typedef enum {
    LAYOUT_TILING,
    LAYOUT_MONOCLE,
    LAYOUT_FLOATING
} LayoutMode;

typedef enum {
    SPLIT_NONE,
    SPLIT_VERTICAL,
    SPLIT_HORIZONTAL
} SplitDirection;

typedef struct Monitor {
    int x, y;
    int width, height;
    int number;
} Monitor;

typedef struct MonitorInfo {
    Monitor *monitors;
    int num_monitors;
    int primary_monitor;
} MonitorInfo;

typedef struct WindowNode WindowNode;

typedef struct TilingNode {
    WindowNode *window;
    struct TilingNode *left;
    struct TilingNode *right;
    struct TilingNode *parent;
    int x, y, width, height;
    float ratio;
    SplitDirection split;
    bool is_leaf;
} TilingNode;

typedef struct Workspace {
    int number;
    LayoutMode layout;
    WindowNode *windows;
    struct Workspace *next;
    float master_ratio;
    float *stack_ratios;
    int stack_ratios_count;
    TilingNode **monitor_tiling_roots;
    int monitor_tiling_roots_count;
} Workspace;

struct WindowNode {
    Window frame;
    Window client;
    char *title;
    int x, y;
    int width, height;
    int monitor_number;
    int workspace;
    bool is_minimized;
    bool is_fullscreen;
    bool is_floating;
    bool is_tiled;
    bool is_titlebar_disabled;
    bool is_desktop_app;
    bool is_fullscreen_app;
    bool stay_on_top;

    int tiling_x, tiling_y, tiling_width, tiling_height;
    int floating_x, floating_y, floating_width, floating_height;

    WindowNode *next;
    WindowNode *prev;
};

typedef struct PrecomputedAtoms {
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
    Atom net_wm_window_opacity;
} PrecomputedAtoms;

typedef struct KeybindConfig {
    char *launch_terminal;
    char *close_window;
    char *toggle_floating;
    char *focus_next_window;
    char *focus_previous_window;
    char *set_tiling_layout;
    char *set_monocle_layout;
    char *shrink_width;
    char *grow_width;
    char *shrink_height;
    char *grow_height;
    char *toggle_layout;
    char *move_window_prev_monitor;
    char *move_window_next_monitor;
    char *switch_workspace[9]; 
    char *logout;
} KeybindConfig;

typedef struct GooeyShellState {
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
    unsigned long focused_border_color;
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
    Window drag_window;
    Window desktop_app_window;
    Window fullscreen_app_window;

    bool is_dragging;
    bool is_resizing;
    bool is_tiling_resizing;
    int drag_start_x, drag_start_y;
    int original_x, original_y;
    int original_width, original_height;
    int resize_direction;
    int tiling_resize_edge;

    MonitorInfo monitor_info;

    Workspace *workspaces;
    int current_workspace;
    LayoutMode current_layout;
    int mod_key;
    bool super_pressed;
    int focused_monitor;

    int inner_gap;
    int outer_gap;

    PrecomputedAtoms atoms;

    DBusConnection *dbus_connection;
    DBusError dbus_error;
    bool is_dbus_init;

    bool supports_opacity;

    char *config_file;
    char *logout_command;
    KeybindConfig keybinds;  
} GooeyShellState;

GooeyShellState *GooeyShell_Init(void);
void GooeyShell_RunEventLoop(GooeyShellState *state);
void GooeyShell_Cleanup(GooeyShellState *state);

void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app);
void GooeyShell_AddFullscreenApp(GooeyShellState *state, const char *command, int stay_on_top);
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client);
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client);
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled);
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client);
int GooeyShell_IsWindowMinimized(GooeyShellState *state, Window client);

void GooeyShell_TileWindows(GooeyShellState *state);
void GooeyShell_TileWindowsOnMonitor(GooeyShellState *state, int monitor_number);
void GooeyShell_RetileAllMonitors(GooeyShellState *state);
void GooeyShell_ToggleFloating(GooeyShellState *state, Window client);
void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout);

void GooeyShell_FocusNextWindow(GooeyShellState *state);
void GooeyShell_FocusPreviousWindow(GooeyShellState *state);
void GooeyShell_FocusNextMonitor(GooeyShellState *state);
void GooeyShell_FocusPreviousMonitor(GooeyShellState *state);

void GooeyShell_MoveWindowToNextMonitor(GooeyShellState *state);
void GooeyShell_MoveWindowToPreviousMonitor(GooeyShellState *state);
void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace);
void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace);

Window *GooeyShell_GetOpenedWindows(GooeyShellState *state, int *count);
int GooeyShell_IsWindowOpened(GooeyShellState *state, Window window);

void GooeyShell_Logout(GooeyShellState *state);
int GooeyShell_LoadConfig(GooeyShellState *state, const char *config_path);

#endif