#include "gooey_shell.h"
#include "gooey_shell_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <unistd.h>    
#include <pwd.h>      
#include <sys/stat.h>  
#include <stdbool.h>  

KeyCode ParseKeybind(GooeyShellState *state, const char *keybind_str, unsigned int *mod_mask) {
    if (!keybind_str || !keybind_str[0])
        return 0;

    *mod_mask = 0;
    char key_name[64] = {0};
    KeyCode keycode = 0;
    char *copy = NULL;

    copy = strdup(keybind_str);
    if (!copy) {
        LogError("Failed to allocate memory for keybind parsing");
        return 0;
    }

    char *token = strtok(copy, "+");
    int key_found = 0;

    while (token) {
        if (strcasecmp(token, "Alt") == 0) {
            *mod_mask |= Mod1Mask;
        }
        else if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0) {
            *mod_mask |= ControlMask;
        }
        else if (strcasecmp(token, "Shift") == 0) {
            *mod_mask |= ShiftMask;
        }
        else if (strcasecmp(token, "Super") == 0 || strcasecmp(token, "Win") == 0) {
            *mod_mask |= Mod4Mask;
        }
        else if (!key_found) {
            strncpy(key_name, token, sizeof(key_name) - 1);
            key_name[sizeof(key_name) - 1] = '\0';
            key_found = 1;
        }
        token = strtok(NULL, "+");
    }

    free(copy);

    if (!key_found) {
        LogError("No key found in keybind: %s", keybind_str);
        return 0;
    }

    KeySym keysym = 0;

    if (strcasecmp(key_name, "Return") == 0)
        keysym = XK_Return;
    else if (strcasecmp(key_name, "Escape") == 0)
        keysym = XK_Escape;
    else if (strcasecmp(key_name, "Space") == 0)
        keysym = XK_space;
    else if (strcasecmp(key_name, "Tab") == 0)
        keysym = XK_Tab;
    else if (strcasecmp(key_name, "Backspace") == 0)
        keysym = XK_BackSpace;
    else if (strcasecmp(key_name, "Delete") == 0)
        keysym = XK_Delete;
    else if (strcasecmp(key_name, "Home") == 0)
        keysym = XK_Home;
    else if (strcasecmp(key_name, "End") == 0)
        keysym = XK_End;
    else if (strcasecmp(key_name, "PageUp") == 0)
        keysym = XK_Page_Up;
    else if (strcasecmp(key_name, "PageDown") == 0)
        keysym = XK_Page_Down;
    else if (strcasecmp(key_name, "Up") == 0)
        keysym = XK_Up;
    else if (strcasecmp(key_name, "Down") == 0)
        keysym = XK_Down;
    else if (strcasecmp(key_name, "Left") == 0)
        keysym = XK_Left;
    else if (strcasecmp(key_name, "Right") == 0)
        keysym = XK_Right;
    else if (strcasecmp(key_name, "F1") == 0)
        keysym = XK_F1;
    else if (strcasecmp(key_name, "F2") == 0)
        keysym = XK_F2;
    else if (strcasecmp(key_name, "F3") == 0)
        keysym = XK_F3;
    else if (strcasecmp(key_name, "F4") == 0)
        keysym = XK_F4;
    else if (strcasecmp(key_name, "F5") == 0)
        keysym = XK_F5;
    else if (strcasecmp(key_name, "F6") == 0)
        keysym = XK_F6;
    else if (strcasecmp(key_name, "F7") == 0)
        keysym = XK_F7;
    else if (strcasecmp(key_name, "F8") == 0)
        keysym = XK_F8;
    else if (strcasecmp(key_name, "F9") == 0)
        keysym = XK_F9;
    else if (strcasecmp(key_name, "F10") == 0)
        keysym = XK_F10;
    else if (strcasecmp(key_name, "F11") == 0)
        keysym = XK_F11;
    else if (strcasecmp(key_name, "F12") == 0)
        keysym = XK_F12;
    else if (strcasecmp(key_name, "bracketleft") == 0)
        keysym = XK_bracketleft;
    else if (strcasecmp(key_name, "bracketright") == 0)
        keysym = XK_bracketright;
    else {
        if (strlen(key_name) == 1) {
            keysym = (KeySym)key_name[0];
        }
        else {
            keysym = XStringToKeysym(key_name);
        }
    }

    if (keysym == 0) {
        if (strlen(key_name) == 1) {
            keysym = (KeySym)key_name[0];
        }
        else {
            keysym = XStringToKeysym(key_name);
        }
    }

    if (keysym == 0) {
        LogError("Unknown key in keybind: %s", key_name);
        return 0;
    }

    keycode = XKeysymToKeycode(state->display, keysym);
    if (keycode == 0) {
        LogError("Failed to convert keysym to keycode: %s", key_name);
    }

    return keycode;
}

void InitializeDefaultKeybinds(KeybindConfig *keybinds) {
    if (!keybinds)
        return;

    memset(keybinds, 0, sizeof(KeybindConfig));

    keybinds->launch_terminal = strdup("Alt+Return");
    keybinds->close_window = strdup("Alt+q");
    keybinds->toggle_floating = strdup("Alt+f");
    keybinds->focus_next_window = strdup("Alt+j");
    keybinds->focus_previous_window = strdup("Alt+k");
    keybinds->set_tiling_layout = strdup("Alt+t");
    keybinds->set_monocle_layout = strdup("Alt+m");
    keybinds->shrink_width = strdup("Alt+h");
    keybinds->grow_width = strdup("Alt+l");
    keybinds->shrink_height = strdup("Alt+y");
    keybinds->grow_height = strdup("Alt+n");
    keybinds->toggle_layout = strdup("Alt+space");
    keybinds->move_window_prev_monitor = strdup("Alt+bracketleft");
    keybinds->move_window_next_monitor = strdup("Alt+bracketright");
    keybinds->logout = strdup("Alt+Escape");

    for (int i = 0; i < 9; i++) {
        char workspace_key[32];
        snprintf(workspace_key, sizeof(workspace_key), "Alt+%d", i + 1);
        keybinds->switch_workspace[i] = strdup(workspace_key);
    }
}

void FreeKeybinds(KeybindConfig *keybinds) {
    if (!keybinds)
        return;

    SAFE_FREE(keybinds->launch_terminal);
    SAFE_FREE(keybinds->close_window);
    SAFE_FREE(keybinds->toggle_floating);
    SAFE_FREE(keybinds->focus_next_window);
    SAFE_FREE(keybinds->focus_previous_window);
    SAFE_FREE(keybinds->set_tiling_layout);
    SAFE_FREE(keybinds->set_monocle_layout);
    SAFE_FREE(keybinds->shrink_width);
    SAFE_FREE(keybinds->grow_width);
    SAFE_FREE(keybinds->shrink_height);
    SAFE_FREE(keybinds->grow_height);
    SAFE_FREE(keybinds->toggle_layout);
    SAFE_FREE(keybinds->move_window_prev_monitor);
    SAFE_FREE(keybinds->move_window_next_monitor);
    SAFE_FREE(keybinds->logout);

    for (int i = 0; i < 9; i++) {
        SAFE_FREE(keybinds->switch_workspace[i]);
    }
}

void GrabKeys(GooeyShellState *state) {
    if (!ValidateWindowState(state))
        return;

    XUngrabKey(state->display, AnyKey, AnyModifier, state->root);

    typedef struct {
        const char *keybind_str;
        const char *name;
    } KeybindMapping;

    KeybindMapping keybinds[] = {
        {state->keybinds.launch_terminal, "launch_terminal"},
        {state->keybinds.close_window, "close_window"},
        {state->keybinds.toggle_floating, "toggle_floating"},
        {state->keybinds.focus_next_window, "focus_next_window"},
        {state->keybinds.focus_previous_window, "focus_previous_window"},
        {state->keybinds.set_tiling_layout, "set_tiling_layout"},
        {state->keybinds.set_monocle_layout, "set_monocle_layout"},
        {state->keybinds.shrink_width, "shrink_width"},
        {state->keybinds.grow_width, "grow_width"},
        {state->keybinds.shrink_height, "shrink_height"},
        {state->keybinds.grow_height, "grow_height"},
        {state->keybinds.toggle_layout, "toggle_layout"},
        {state->keybinds.move_window_prev_monitor, "move_window_prev_monitor"},
        {state->keybinds.move_window_next_monitor, "move_window_next_monitor"},
        {state->keybinds.logout, "logout"},
    };

    for (int i = 0; i < 9; i++) {
        unsigned int mod_mask;
        KeyCode keycode = ParseKeybind(state, state->keybinds.switch_workspace[i], &mod_mask);
        if (keycode != 0) {
            XGrabKey(state->display, keycode, mod_mask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keycode, mod_mask | LockMask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keycode, mod_mask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(state->display, keycode, mod_mask | LockMask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
        }
    }

    for (int i = 0; i < sizeof(keybinds) / sizeof(keybinds[0]); i++) {
        if (keybinds[i].keybind_str) {
            unsigned int mod_mask;
            KeyCode keycode = ParseKeybind(state, keybinds[i].keybind_str, &mod_mask);
            if (keycode != 0) {
                XGrabKey(state->display, keycode, mod_mask, state->root, True, GrabModeAsync, GrabModeAsync);
                XGrabKey(state->display, keycode, mod_mask | LockMask, state->root, True, GrabModeAsync, GrabModeAsync);
                XGrabKey(state->display, keycode, mod_mask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
                XGrabKey(state->display, keycode, mod_mask | LockMask | Mod2Mask, state->root, True, GrabModeAsync, GrabModeAsync);
            }
            else {
                LogError("Failed to parse keybind: %s = %s", keybinds[i].name, keybinds[i].keybind_str);
            }
        }
    }

    XFlush(state->display);
}

void RegrabKeys(GooeyShellState *state) {
    GrabKeys(state);
}

void HandleMouseFocus(GooeyShellState *state, XMotionEvent *ev) {
    if (state->is_dragging || state->is_resizing || state->is_tiling_resizing)
        return;

    Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;

    if (XQueryPointer(state->display, state->root, &root, &child,
                      &root_x, &root_y, &win_x, &win_y, &mask)) {
        if (child != None && child != state->root) {
            WindowNode *node = FindWindowNodeByFrame(state, child);
            if (!node) {
                node = FindWindowNodeByClient(state, child);
            }

            if (node && node->frame != state->focused_window &&
                !node->is_desktop_app && !node->is_fullscreen_app &&
                !node->is_minimized) {
                FocusWindow(state, node);
            }
        }
    }
}

char *ExpandPath(const char *path) {
    if (!path)
        return NULL;

    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "";
        }

        char *expanded = malloc(strlen(home) + strlen(path) + 1);
        if (expanded) {
            strcpy(expanded, home);
            strcat(expanded, path + 1);
            return expanded;
        }
    }
    return strdup(path);
}

int ParseColor(const char *color_str) {
    if (!color_str)
        return 0x2196F3;

    if (color_str[0] == '#') {
        unsigned int color;
        if (sscanf(color_str + 1, "%x", &color) == 1) {
            return (int)color;
        }
    }

    if (strcmp(color_str, "material_blue") == 0)
        return 0x2196F3;
    if (strcmp(color_str, "red") == 0)
        return 0xFF0000;
    if (strcmp(color_str, "green") == 0)
        return 0x00FF00;
    if (strcmp(color_str, "blue") == 0)
        return 0x0000FF;
    if (strcmp(color_str, "white") == 0)
        return 0xFFFFFF;
    if (strcmp(color_str, "black") == 0)
        return 0x000000;
    if (strcmp(color_str, "gray") == 0)
        return 0x808080;

    return 0x2196F3;
}

void CreateDefaultConfig(const char *config_path) {
    FILE *file = fopen(config_path, "w");
    if (!file) {
        LogError("Failed to create config file: %s", config_path);
        return;
    }

    fprintf(file, "# Gooey Shell Configuration File\n");
    fprintf(file, "# Colors can be specified as hex (#RRGGBB) or common names\n\n");

    fprintf(file, "# Focus border color (Material Blue by default)\n");
    fprintf(file, "focused_border_color = #2196F3\n\n");

    fprintf(file, "# Logout command\n");
    fprintf(file, "logout_command = killall gooey_shell\n\n");

    fprintf(file, "# Window gaps\n");
    fprintf(file, "inner_gap = 8\n");
    fprintf(file, "outer_gap = 8\n\n");

    fprintf(file, "# Window opacity (0.0 to 1.0)\n");
    fprintf(file, "window_opacity = 0.95\n\n");

    fprintf(file, "# Enable mouse focus (true/false)\n");
    fprintf(file, "mouse_focus = true\n\n");

    fprintf(file, "# Keybinds (Format: Mod+Key, Mod can be: Alt, Ctrl, Shift, Super)\n");
    fprintf(file, "# Available keys: https://www.x.org/releases/X11R7.7/doc/libX11/X11/keysymdef.html\n\n");

    fprintf(file, "# Window management\n");
    fprintf(file, "keybind.launch_terminal = Alt+Return\n");
    fprintf(file, "keybind.close_window = Alt+q\n");
    fprintf(file, "keybind.toggle_floating = Alt+f\n");
    fprintf(file, "keybind.focus_next_window = Alt+j\n");
    fprintf(file, "keybind.focus_previous_window = Alt+k\n\n");

    fprintf(file, "# Layout management\n");
    fprintf(file, "keybind.set_tiling_layout = Alt+t\n");
    fprintf(file, "keybind.set_monocle_layout = Alt+m\n");
    fprintf(file, "keybind.toggle_layout = Alt+space\n\n");

    fprintf(file, "# Window resizing\n");
    fprintf(file, "keybind.shrink_width = Alt+h\n");
    fprintf(file, "keybind.grow_width = Alt+l\n");
    fprintf(file, "keybind.shrink_height = Alt+y\n");
    fprintf(file, "keybind.grow_height = Alt+n\n\n");

    fprintf(file, "# Window movement\n");
    fprintf(file, "keybind.move_window_prev_monitor = Alt+bracketleft\n");
    fprintf(file, "keybind.move_window_next_monitor = Alt+bracketright\n\n");

    fprintf(file, "# Workspaces\n");
    fprintf(file, "keybind.switch_workspace_1 = Alt+1\n");
    fprintf(file, "keybind.switch_workspace_2 = Alt+2\n");
    fprintf(file, "keybind.switch_workspace_3 = Alt+3\n");
    fprintf(file, "keybind.switch_workspace_4 = Alt+4\n");
    fprintf(file, "keybind.switch_workspace_5 = Alt+5\n");
    fprintf(file, "keybind.switch_workspace_6 = Alt+6\n");
    fprintf(file, "keybind.switch_workspace_7 = Alt+7\n");
    fprintf(file, "keybind.switch_workspace_8 = Alt+8\n");
    fprintf(file, "keybind.switch_workspace_9 = Alt+9\n\n");

    fprintf(file, "# System\n");
    fprintf(file, "keybind.logout = Alt+Escape\n");

    fclose(file);
    LogInfo("Created default config file: %s", config_path);
}

int KeybindMatches(GooeyShellState *state, XKeyEvent *ev, const char *keybind_str) {
    unsigned int expected_mod_mask;
    KeyCode expected_keycode = ParseKeybind(state, keybind_str, &expected_mod_mask);

    if (expected_keycode == 0)
        return 0;

    if (ev->keycode != expected_keycode)
        return 0;

    unsigned int actual_mod_mask = ev->state & ~(LockMask | Mod2Mask);
    unsigned int clean_expected_mod_mask = expected_mod_mask & ~(LockMask | Mod2Mask);

    return (actual_mod_mask == clean_expected_mod_mask);
}

int GooeyShell_LoadConfig(GooeyShellState *state, const char *config_path) {
    char *expanded_path = ExpandPath(config_path);
    if (!expanded_path) {
        return 0;
    }

    char *config_dir = strdup(expanded_path);
    char *last_slash = strrchr(config_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(config_dir, 0755);
    }
    free(config_dir);

    if (access(expanded_path, F_OK) != 0) {
        CreateDefaultConfig(expanded_path);
    }

    FILE *file = fopen(expanded_path, "r");
    if (!file) {
        free(expanded_path);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        char *key = strtok(line, " =");
        char *value = strtok(NULL, " =\n");

        if (!key || !value)
            continue;

        if (strcmp(key, "focused_border_color") == 0) {
            state->focused_border_color = ParseColor(value);
            LogInfo("Config: focused_border_color = %s (0x%06X)", value, state->focused_border_color);
        }
        else if (strcmp(key, "logout_command") == 0) {
            free(state->logout_command);
            state->logout_command = strdup(value);
            LogInfo("Config: logout_command = %s", value);
        }
        else if (strcmp(key, "inner_gap") == 0) {
            state->inner_gap = atoi(value);
            LogInfo("Config: inner_gap = %d", state->inner_gap);
        }
        else if (strcmp(key, "outer_gap") == 0) {
            state->outer_gap = atoi(value);
            LogInfo("Config: outer_gap = %d", state->outer_gap);
        }
        else if (strncmp(key, "keybind.", 8) == 0) {
            const char *keybind_name = key + 8;

            if (strcmp(keybind_name, "launch_terminal") == 0) {
                free(state->keybinds.launch_terminal);
                state->keybinds.launch_terminal = strdup(value);
                LogInfo("Config: keybind.launch_terminal = %s", value);
            }
            else if (strcmp(keybind_name, "close_window") == 0) {
                free(state->keybinds.close_window);
                state->keybinds.close_window = strdup(value);
                LogInfo("Config: keybind.close_window = %s", value);
            }
            else if (strcmp(keybind_name, "toggle_floating") == 0) {
                free(state->keybinds.toggle_floating);
                state->keybinds.toggle_floating = strdup(value);
                LogInfo("Config: keybind.toggle_floating = %s", value);
            }
            else if (strcmp(keybind_name, "focus_next_window") == 0) {
                free(state->keybinds.focus_next_window);
                state->keybinds.focus_next_window = strdup(value);
                LogInfo("Config: keybind.focus_next_window = %s", value);
            }
            else if (strcmp(keybind_name, "focus_previous_window") == 0) {
                free(state->keybinds.focus_previous_window);
                state->keybinds.focus_previous_window = strdup(value);
                LogInfo("Config: keybind.focus_previous_window = %s", value);
            }
            else if (strcmp(keybind_name, "set_tiling_layout") == 0) {
                free(state->keybinds.set_tiling_layout);
                state->keybinds.set_tiling_layout = strdup(value);
                LogInfo("Config: keybind.set_tiling_layout = %s", value);
            }
            else if (strcmp(keybind_name, "set_monocle_layout") == 0) {
                free(state->keybinds.set_monocle_layout);
                state->keybinds.set_monocle_layout = strdup(value);
                LogInfo("Config: keybind.set_monocle_layout = %s", value);
            }
            else if (strcmp(keybind_name, "shrink_width") == 0) {
                free(state->keybinds.shrink_width);
                state->keybinds.shrink_width = strdup(value);
                LogInfo("Config: keybind.shrink_width = %s", value);
            }
            else if (strcmp(keybind_name, "grow_width") == 0) {
                free(state->keybinds.grow_width);
                state->keybinds.grow_width = strdup(value);
                LogInfo("Config: keybind.grow_width = %s", value);
            }
            else if (strcmp(keybind_name, "shrink_height") == 0) {
                free(state->keybinds.shrink_height);
                state->keybinds.shrink_height = strdup(value);
                LogInfo("Config: keybind.shrink_height = %s", value);
            }
            else if (strcmp(keybind_name, "grow_height") == 0) {
                free(state->keybinds.grow_height);
                state->keybinds.grow_height = strdup(value);
                LogInfo("Config: keybind.grow_height = %s", value);
            }
            else if (strcmp(keybind_name, "toggle_layout") == 0) {
                free(state->keybinds.toggle_layout);
                state->keybinds.toggle_layout = strdup(value);
                LogInfo("Config: keybind.toggle_layout = %s", value);
            }
            else if (strcmp(keybind_name, "move_window_prev_monitor") == 0) {
                free(state->keybinds.move_window_prev_monitor);
                state->keybinds.move_window_prev_monitor = strdup(value);
                LogInfo("Config: keybind.move_window_prev_monitor = %s", value);
            }
            else if (strcmp(keybind_name, "move_window_next_monitor") == 0) {
                free(state->keybinds.move_window_next_monitor);
                state->keybinds.move_window_next_monitor = strdup(value);
                LogInfo("Config: keybind.move_window_next_monitor = %s", value);
            }
            else if (strcmp(keybind_name, "logout") == 0) {
                free(state->keybinds.logout);
                state->keybinds.logout = strdup(value);
                LogInfo("Config: keybind.logout = %s", value);
            }
            else if (strncmp(keybind_name, "switch_workspace_", 17) == 0) {
                int workspace_num = atoi(keybind_name + 17);
                if (workspace_num >= 1 && workspace_num <= 9) {
                    free(state->keybinds.switch_workspace[workspace_num - 1]);
                    state->keybinds.switch_workspace[workspace_num - 1] = strdup(value);
                    LogInfo("Config: keybind.switch_workspace_%d = %s", workspace_num, value);
                }
            }
        }
    }

    fclose(file);
    free(expanded_path);
    return 1;
}

void GooeyShell_Logout(GooeyShellState *state) {
    LogInfo("Logging out...");

    if (state->logout_command) {
        system(state->logout_command);
    }
    else {
        exit(0);
    }
}

GooeyShellState *GooeyShell_Init(void) {
    GooeyShellState *state = calloc(1, sizeof(GooeyShellState));
    if (!state) {
        LogError("Failed to allocate GooeyShellState");
        return NULL;
    }

    // Initialize mutexes
    if (glps_thread_mutex_init(&dbus_mutex, NULL) != 0) {
        LogError("Failed to initialize DBus mutex");
        free(state);
        return NULL;
    }

    if (glps_thread_mutex_init(&window_list_mutex, NULL) != 0) {
        LogError("Failed to initialize window list mutex");
        glps_thread_mutex_destroy(&dbus_mutex);
        free(state);
        return NULL;
    }

    state->display = XOpenDisplay(NULL);
    if (!state->display) {
        LogError("Failed to open X display");
        glps_thread_mutex_destroy(&window_list_mutex);
        glps_thread_mutex_destroy(&dbus_mutex);
        free(state);
        return NULL;
    }

    state->screen = DefaultScreen(state->display);
    state->root = RootWindow(state->display, state->screen);

    // Initialize all pointers to NULL
    state->desktop_app_window = None;
    state->fullscreen_app_window = None;
    state->focused_window = None;
    state->drag_window = None;
    state->is_tiling_resizing = False;
    state->window_list = NULL;
    state->workspaces = NULL;

    state->monitor_info.monitors = NULL;
    state->monitor_info.num_monitors = 0;
    state->monitor_info.primary_monitor = 0;

    if (!InitializeMultiMonitor(state)) {
        LogError("Failed to initialize multi-monitor support");
        GooeyShell_Cleanup(state);
        return NULL;
    }

    InitializeAtoms(state->display);
    InitializeTransparency(state);

    state->gc = XCreateGC(state->display, state->root, 0, NULL);
    if (!state->gc) {
        LogError("Failed to create graphics context");
        GooeyShell_Cleanup(state);
        return NULL;
    }

    XGCValues gcv;

    state->config_file = strdup(CONFIG_FILE);
    if (!state->config_file) {
        LogError("Failed to allocate config file path");
        goto cleanup;
    }

    InitializeDefaultKeybinds(&state->keybinds);
    GooeyShell_LoadConfig(state, state->config_file);

    state->titlebar_color = 0x212121;
    state->titlebar_focused_color = 0x424242;
    state->text_color = WhitePixel(state->display, state->screen);
    state->border_color = 0x666666;
    state->focused_border_color = 0x2196F3;
    state->button_color = 0xDDDDDD;
    state->close_button_color = 0xFF4444;
    state->minimize_button_color = 0xFFCC00;
    state->maximize_button_color = 0x00CC44;
    state->bg_color = 0x333333;
    state->logout_command = strdup("killall gooey_shell");

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
    if (state->custom_cursor == None) {
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

    InitializeWorkspaces(state);

    GrabKeys(state);

    LaunchDesktopAppsForAllMonitors(state);

    XFlush(state->display);

    opened_windows = NULL;
    opened_windows_count = 0;
    opened_windows_capacity = 0;
    state->is_dbus_init = false;

    SetupDBUS(state);

    return state;

cleanup:
    GooeyShell_Cleanup(state);
    return NULL;
}