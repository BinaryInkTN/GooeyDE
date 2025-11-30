#ifndef GOOEY_SHELL_H
#define GOOEY_SHELL_H

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dbus/dbus.h>
#include <GLPS/glps_thread.h>

// Constants and configuration
#define WINDOW_MANAGER_NAME "GooeyShell"
#define CONFIG_FILE "~/.config/gooey_shell/config"
#define BAR_HEIGHT 24
#define TITLE_BAR_HEIGHT 24
#define BORDER_WIDTH 2
#define BUTTON_SIZE 12
#define BUTTON_MARGIN 6
#define BUTTON_SPACING 4
#define RESIZE_HANDLE_SIZE 6
#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define WINDOW_OPACITY 0.95f

// Enums
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

// Structure definitions
typedef struct WindowNode {
    Window frame;
    Window client;
    char *title;
    int x, y;
    int width, height;
    int monitor_number;
    int workspace;
    int is_minimized;
    int is_fullscreen;
    int is_floating;
    int is_tiled;
    int is_desktop_app;
    int is_fullscreen_app;
    int is_titlebar_disabled;
    int stay_on_top;
    
    // Tiling state
    int tiling_x, tiling_y;
    int tiling_width, tiling_height;
    
    // Floating state
    int floating_x, floating_y;
    int floating_width, floating_height;
    
    struct WindowNode *next;
    struct WindowNode *prev;
} WindowNode;

typedef struct TilingNode {
    WindowNode *window;
    struct TilingNode *left;
    struct TilingNode *right;
    struct TilingNode *parent;
    int x, y;
    int width, height;
    float ratio;
    SplitDirection split;
    int is_leaf;
    int ref_count;
} TilingNode;

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
    char *logout;
    char *switch_workspace[9];
} KeybindConfig;

typedef struct Workspace {
    int number;
    LayoutMode layout;
    WindowNode *windows;
    struct Workspace *next;
    
    // Tiling state
    float master_ratio;
    float *stack_ratios;
    int stack_ratios_count;
    
    // Multi-monitor tiling
    TilingNode **monitor_tiling_roots;
    int monitor_tiling_roots_count;
} Workspace;

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

typedef struct GooeyShellState {
    Display *display;
    int screen;
    Window root;
    
    // Window management
    WindowNode *window_list;
    Window focused_window;
    Window desktop_app_window;
    Window fullscreen_app_window;
    Window drag_window;
    
    // State flags
    int is_dragging;
    int is_resizing;
    int is_tiling_resizing;
    int tiling_resize_edge;
    int resize_direction;
    int drag_start_x, drag_start_y;
    int original_x, original_y;
    int original_width, original_height;
    
    // Workspace management
    Workspace *workspaces;
    int current_workspace;
    LayoutMode current_layout;
    
    // Multi-monitor support
    MonitorInfo monitor_info;
    int focused_monitor;
    
    // Graphics
    GC gc;
    GC titlebar_gc;
    GC text_gc;
    GC button_gc;
    
    // Colors
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
    
    // Cursors
    Cursor move_cursor;
    Cursor resize_cursor;
    Cursor v_resize_cursor;
    Cursor normal_cursor;
    Cursor custom_cursor;
    
    // Configuration
    char *config_file;
    KeybindConfig keybinds;
    char *logout_command;
    int inner_gap;
    int outer_gap;
    
    // Input
    unsigned int mod_key;
    int super_pressed;
    
    // DBus
    DBusConnection *dbus_connection;
    DBusError dbus_error;
    int is_dbus_init;
    
    // Features
    int supports_opacity;
} GooeyShellState;

// Include the split headers
#include "gooey_shell_core.h"
#include "gooey_shell_tiling.h"
#include "gooey_shell_config.h"

#endif // GOOEY_SHELL_H