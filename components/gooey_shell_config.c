#include "gooey_shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
static KeySym ParseSingleKeybind(GooeyShellState *state, const char *key_name);
static int ValidateKeybindString(const char *keybind_str);
static int SafeStringCopy(char *dest, size_t dest_size, const char *src);
static int IsValidWorkspaceIndex(int index);
KeyCode ParseKeybind(GooeyShellState *state, const char *keybind_str, unsigned int *mod_mask)
{
    char key_name[64] = {0};
    char *copy = NULL;
    char *token = NULL;
    char *saveptr = NULL;
    KeyCode keycode = 0U;
    KeySym keysym = 0U;
    int key_found = 0;
    if (state == NULL)
    {
        LogError("ParseKeybind: NULL state pointer");
        return 0U;
    }
    if (keybind_str == NULL)
    {
        LogError("ParseKeybind: NULL keybind string");
        return 0U;
    }
    if (mod_mask == NULL)
    {
        LogError("ParseKeybind: NULL mod_mask pointer");
        return 0U;
    }
    if (keybind_str[0] == '\0')
    {
        LogError("ParseKeybind: Empty keybind string");
        return 0U;
    }
    *mod_mask = 0U;
    if (ValidateKeybindString(keybind_str) != 0)
    {
        LogError("ParseKeybind: Invalid keybind format: %s", keybind_str);
        return 0U;
    }
    copy = strdup(keybind_str);
    if (copy == NULL)
    {
        LogError("ParseKeybind: Memory allocation failed for: %s", keybind_str);
        return 0U;
    }
    token = strtok_r(copy, "+", &saveptr);
    while (token != NULL)
    {
        if (strcasecmp(token, "Alt") == 0)
        {
            *mod_mask |= (unsigned int)Mod1Mask;
        }
        else if ((strcasecmp(token, "Ctrl") == 0) || (strcasecmp(token, "Control") == 0))
        {
            *mod_mask |= (unsigned int)ControlMask;
        }
        else if (strcasecmp(token, "Shift") == 0)
        {
            *mod_mask |= (unsigned int)ShiftMask;
        }
        else if ((strcasecmp(token, "Super") == 0) || (strcasecmp(token, "Win") == 0))
        {
            *mod_mask |= (unsigned int)Mod4Mask;
        }
        else if (key_found == 0)
        {
            if (SafeStringCopy(key_name, sizeof(key_name), token) != 0)
            {
                LogError("ParseKeybind: Key name too long: %s", token);
                free(copy);
                return 0U;
            }
            key_found = 1;
        }
        else
        {
            LogError("ParseKeybind: Multiple keys in keybind: %s", keybind_str);
            free(copy);
            return 0U;
        }
        token = strtok_r(NULL, "+", &saveptr);
    }
    free(copy);
    if (key_found == 0)
    {
        LogError("ParseKeybind: No key found in: %s", keybind_str);
        return 0U;
    }
    keysym = ParseSingleKeybind(state, key_name);
    if (keysym == 0U)
    {
        LogError("ParseKeybind: Unknown key: %s", key_name);
        return 0U;
    }
    keycode = XKeysymToKeycode(state->display, keysym);
    if (keycode == 0U)
    {
        LogError("ParseKeybind: Failed to convert keysym: %s (0x%lX)", key_name, keysym);
    }
    return keycode;
}
static KeySym ParseSingleKeybind(GooeyShellState *state, const char *key_name)
{
    KeySym keysym = 0U;
    size_t key_len;
    (void)state;
    if (key_name == NULL)
    {
        return 0U;
    }
    key_len = strlen(key_name);
    if (strcasecmp(key_name, "Return") == 0)
    {
        keysym = XK_Return;
    }
    else if (strcasecmp(key_name, "Escape") == 0)
    {
        keysym = XK_Escape;
    }
    else if (strcasecmp(key_name, "Space") == 0)
    {
        keysym = XK_space;
    }
    else if (strcasecmp(key_name, "Tab") == 0)
    {
        keysym = XK_Tab;
    }
    else if (strcasecmp(key_name, "Backspace") == 0)
    {
        keysym = XK_BackSpace;
    }
    else if (strcasecmp(key_name, "Delete") == 0)
    {
        keysym = XK_Delete;
    }
    else if (strcasecmp(key_name, "Home") == 0)
    {
        keysym = XK_Home;
    }
    else if (strcasecmp(key_name, "End") == 0)
    {
        keysym = XK_End;
    }
    else if (strcasecmp(key_name, "PageUp") == 0)
    {
        keysym = XK_Page_Up;
    }
    else if (strcasecmp(key_name, "PageDown") == 0)
    {
        keysym = XK_Page_Down;
    }
    else if (strcasecmp(key_name, "Up") == 0)
    {
        keysym = XK_Up;
    }
    else if (strcasecmp(key_name, "Down") == 0)
    {
        keysym = XK_Down;
    }
    else if (strcasecmp(key_name, "Left") == 0)
    {
        keysym = XK_Left;
    }
    else if (strcasecmp(key_name, "Right") == 0)
    {
        keysym = XK_Right;
    }
    else if (strcasecmp(key_name, "F1") == 0)
    {
        keysym = XK_F1;
    }
    else if (strcasecmp(key_name, "F2") == 0)
    {
        keysym = XK_F2;
    }
    else if (strcasecmp(key_name, "F3") == 0)
    {
        keysym = XK_F3;
    }
    else if (strcasecmp(key_name, "F4") == 0)
    {
        keysym = XK_F4;
    }
    else if (strcasecmp(key_name, "F5") == 0)
    {
        keysym = XK_F5;
    }
    else if (strcasecmp(key_name, "F6") == 0)
    {
        keysym = XK_F6;
    }
    else if (strcasecmp(key_name, "F7") == 0)
    {
        keysym = XK_F7;
    }
    else if (strcasecmp(key_name, "F8") == 0)
    {
        keysym = XK_F8;
    }
    else if (strcasecmp(key_name, "F9") == 0)
    {
        keysym = XK_F9;
    }
    else if (strcasecmp(key_name, "F10") == 0)
    {
        keysym = XK_F10;
    }
    else if (strcasecmp(key_name, "F11") == 0)
    {
        keysym = XK_F11;
    }
    else if (strcasecmp(key_name, "F12") == 0)
    {
        keysym = XK_F12;
    }
    else if (strcasecmp(key_name, "bracketleft") == 0)
    {
        keysym = XK_bracketleft;
    }
    else if (strcasecmp(key_name, "bracketright") == 0)
    {
        keysym = XK_bracketright;
    }
    else if (key_len == 1U)
    {
        keysym = (KeySym)key_name[0];
    }
    else
    {
        keysym = XStringToKeysym(key_name);
    }
    if (keysym == 0U)
    {
        if (key_len == 1U)
        {
            keysym = (KeySym)key_name[0];
        }
        else
        {
            keysym = XStringToKeysym(key_name);
        }
    }
    return keysym;
}
static int ValidateKeybindString(const char *keybind_str)
{
    size_t len;
    int plus_count = 0;
    size_t i;
    if (keybind_str == NULL)
    {
        return -1;
    }
    len = strlen(keybind_str);
    if ((len == 0U) || (len >= 256U))
    {
        return -1;
    }
    for (i = 0U; i < len; i++)
    {
        if (keybind_str[i] == '+')
        {
            plus_count++;
            if ((i == 0U) || (i == (len - 1U)) || (keybind_str[i + 1] == '+'))
            {
                return -1;
            }
        }
    }
    if (plus_count > 4)
    {
        return -1;
    }
    return 0;
}
void InitializeDefaultKeybinds(KeybindConfig *keybinds)
{
    const int MAX_WORKSPACES = 9;
    int i;
    if (keybinds == NULL)
    {
        LogError("InitializeDefaultKeybinds: NULL keybinds pointer");
        return;
    }
    (void)memset(keybinds, 0, sizeof(KeybindConfig));
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
    keybinds->launch_menu = strdup("Super+m");
    keybinds->logout = strdup("Alt+Escape");
    for (i = 0; i < MAX_WORKSPACES; i++)
    {
        char workspace_key[32];
        int result;
        result = snprintf(workspace_key, sizeof(workspace_key), "Alt+%d", i + 1);
        if ((result < 0) || ((size_t)result >= sizeof(workspace_key)))
        {
            LogError("InitializeDefaultKeybinds: Workspace key formatting error");
            keybinds->switch_workspace[i] = NULL;
        }
        else
        {
            keybinds->switch_workspace[i] = strdup(workspace_key);
        }
    }
    if ((keybinds->launch_terminal == NULL) ||
        (keybinds->close_window == NULL) ||
        (keybinds->toggle_floating == NULL))
    {
        LogError("InitializeDefaultKeybinds: Memory allocation failed");
    }
}
void FreeKeybinds(KeybindConfig *keybinds)
{
    int i;
    const int MAX_WORKSPACES = 9;
    if (keybinds == NULL)
    {
        return;
    }
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
    SAFE_FREE(keybinds->launch_menu);
    SAFE_FREE(keybinds->logout);
    for (i = 0; i < MAX_WORKSPACES; i++)
    {
        SAFE_FREE(keybinds->switch_workspace[i]);
    }
}
void GrabKeys(GooeyShellState *state)
{
    typedef struct
    {
        const char *keybind_str;
        const char *name;
    } KeybindMapping;
    const KeybindMapping keybinds[] = {
        {NULL, "launch_terminal"},
        {NULL, "close_window"},
        {NULL, "toggle_floating"},
        {NULL, "focus_next_window"},
        {NULL, "focus_previous_window"},
        {NULL, "set_tiling_layout"},
        {NULL, "set_monocle_layout"},
        {NULL, "shrink_width"},
        {NULL, "grow_width"},
        {NULL, "shrink_height"},
        {NULL, "grow_height"},
        {NULL, "toggle_layout"},
        {NULL, "move_window_prev_monitor"},
        {NULL, "move_window_next_monitor"},
        {NULL, "launch_menu"},
        {NULL, "logout"},
    };
    const int NUM_KEYBINDS = (int)(sizeof(keybinds) / sizeof(keybinds[0]));
    const int MAX_WORKSPACES = 9;
    int i;
    if (state == NULL)
    {
        LogError("GrabKeys: NULL state pointer");
        return;
    }
    if (!ValidateWindowState(state))
    {
        LogError("GrabKeys: Invalid window state");
        return;
    }
    KeybindMapping actual_keybinds[NUM_KEYBINDS];
    (void)memcpy(actual_keybinds, keybinds, sizeof(keybinds));
    actual_keybinds[0].keybind_str = state->keybinds.launch_terminal;
    actual_keybinds[1].keybind_str = state->keybinds.close_window;
    actual_keybinds[2].keybind_str = state->keybinds.toggle_floating;
    actual_keybinds[3].keybind_str = state->keybinds.focus_next_window;
    actual_keybinds[4].keybind_str = state->keybinds.focus_previous_window;
    actual_keybinds[5].keybind_str = state->keybinds.set_tiling_layout;
    actual_keybinds[6].keybind_str = state->keybinds.set_monocle_layout;
    actual_keybinds[7].keybind_str = state->keybinds.shrink_width;
    actual_keybinds[8].keybind_str = state->keybinds.grow_width;
    actual_keybinds[9].keybind_str = state->keybinds.shrink_height;
    actual_keybinds[10].keybind_str = state->keybinds.grow_height;
    actual_keybinds[11].keybind_str = state->keybinds.toggle_layout;
    actual_keybinds[12].keybind_str = state->keybinds.move_window_prev_monitor;
    actual_keybinds[13].keybind_str = state->keybinds.move_window_next_monitor;
    actual_keybinds[14].keybind_str = state->keybinds.launch_menu;
    actual_keybinds[15].keybind_str = state->keybinds.logout;
    XUngrabKey(state->display, AnyKey, AnyModifier, state->root);
    for (i = 0; i < MAX_WORKSPACES; i++)
    {
        unsigned int mod_mask = 0U;
        KeyCode keycode = 0U;
        if (state->keybinds.switch_workspace[i] != NULL)
        {
            keycode = ParseKeybind(state, state->keybinds.switch_workspace[i], &mod_mask);
            if (keycode != 0U)
            {
                (void)XGrabKey(state->display, keycode, mod_mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | LockMask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | Mod2Mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | LockMask | Mod2Mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    for (i = 0; i < NUM_KEYBINDS; i++)
    {
        if (actual_keybinds[i].keybind_str != NULL)
        {
            unsigned int mod_mask = 0U;
            KeyCode keycode = 0U;
            keycode = ParseKeybind(state, actual_keybinds[i].keybind_str, &mod_mask);
            if (keycode != 0U)
            {
                (void)XGrabKey(state->display, keycode, mod_mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | LockMask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | Mod2Mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
                (void)XGrabKey(state->display, keycode, mod_mask | LockMask | Mod2Mask, 
                             state->root, True, GrabModeAsync, GrabModeAsync);
            }
            else
            {
                LogError("GrabKeys: Failed to parse keybind: %s = %s", 
                        actual_keybinds[i].name, actual_keybinds[i].keybind_str);
            }
        }
    }
    (void)XFlush(state->display);
}
char *ExpandPath(const char *path)
{
    const char *home = NULL;
    struct passwd *pw = NULL;
    char *expanded = NULL;
    size_t path_len;
    size_t home_len;
    if (path == NULL)
    {
        return NULL;
    }
    path_len = strlen(path);
    if (path[0] == '~')
    {
        home = getenv("HOME");
        if (home == NULL)
        {
            pw = getpwuid(getuid());
            if (pw != NULL)
            {
                home = pw->pw_dir;
            }
            else
            {
                home = "";
            }
        }
        home_len = strlen(home);
        expanded = malloc(home_len + path_len + 1U);
        if (expanded == NULL)
        {
            LogError("ExpandPath: Memory allocation failed");
            return NULL;
        }
        (void)strcpy(expanded, home);
        (void)strcat(expanded, path + 1);
    }
    else
    {
        expanded = strdup(path);
        if (expanded == NULL)
        {
            LogError("ExpandPath: strdup failed");
        }
    }
    return expanded;
}
int ParseColor(const char *color_str)
{
    unsigned int color = 0U;
    int result = 0;
    if (color_str == NULL)
    {
        return 0x2196F3;
    }
    if (color_str[0] == '#')
    {
        result = sscanf(color_str + 1, "%x", &color);
        if (result == 1)
        {
            return (int)color;
        }
    }
    if (strcmp(color_str, "material_blue") == 0)
    {
        return 0x2196F3;
    }
    else if (strcmp(color_str, "red") == 0)
    {
        return 0xFF0000;
    }
    else if (strcmp(color_str, "green") == 0)
    {
        return 0x00FF00;
    }
    else if (strcmp(color_str, "blue") == 0)
    {
        return 0x0000FF;
    }
    else if (strcmp(color_str, "white") == 0)
    {
        return 0xFFFFFF;
    }
    else if (strcmp(color_str, "black") == 0)
    {
        return 0x000000;
    }
    else if (strcmp(color_str, "gray") == 0)
    {
        return 0x808080;
    }
    return 0x2196F3;
}void CreateDefaultConfig(const char *config_path)
{
    FILE *file = NULL;
    const mode_t CONFIG_FILE_MODE = 0644;
    if (config_path == NULL)
    {
        LogError("CreateDefaultConfig: NULL config path");
        return;
    }
    file = fopen(config_path, "w");
    if (file == NULL)
    {
        LogError("CreateDefaultConfig: Failed to create config file: %s (error: %d)", 
                config_path, errno);
        return;
    }
    (void)fprintf(file, "# Gooey Shell Configuration File\n");
    (void)fprintf(file, "# Colors can be specified as hex (#RRGGBB)\n\n");
    (void)fprintf(file, "# Wallpaper path (use ~ for home directory)\n");
    (void)fprintf(file, "wallpaper_path = /usr/local/share/gooeyde/assets/bg.png\n\n");
    (void)fprintf(file, "# Focus border color (Indigo by default)\n");
    (void)fprintf(file, "focused_border_color = #3F51B5\n\n");
    (void)fprintf(file, "# Logout command\n");
    (void)fprintf(file, "logout_command = killall gooey_shell\n\n");
    (void)fprintf(file, "# Window gaps\n");
    (void)fprintf(file, "inner_gap = 8\n");
    (void)fprintf(file, "outer_gap = 8\n\n");
    (void)fprintf(file, "# Window opacity (0.0 to 1.0)\n");
    (void)fprintf(file, "window_opacity = 0.95\n\n");
    (void)fprintf(file, "# Enable mouse focus (true/false)\n");
    (void)fprintf(file, "mouse_focus = true\n\n");
    (void)fprintf(file, "# Keybinds (Format: Mod+Key, Mod can be: Alt, Ctrl, Shift, Super)\n");
    (void)fprintf(file, "# Launch App Menu\n");
    (void)fprintf(file, "# keybind.launch_menu = Super+m\n");
    (void)fprintf(file, "# Window management\n");
    (void)fprintf(file, "keybind.launch_terminal = Alt+Return\n");
    (void)fprintf(file, "keybind.close_window = Alt+q\n");
    (void)fprintf(file, "keybind.toggle_floating = Alt+f\n");
    (void)fprintf(file, "keybind.focus_next_window = Alt+j\n");
    (void)fprintf(file, "keybind.focus_previous_window = Alt+k\n\n");
    (void)fprintf(file, "# Layout management\n");
    (void)fprintf(file, "keybind.set_tiling_layout = Alt+t\n");
    (void)fprintf(file, "keybind.set_monocle_layout = Alt+m\n");
    (void)fprintf(file, "keybind.toggle_layout = Alt+space\n\n");
    (void)fprintf(file, "# Window resizing\n");
    (void)fprintf(file, "keybind.shrink_width = Alt+h\n");
    (void)fprintf(file, "keybind.grow_width = Alt+l\n");
    (void)fprintf(file, "keybind.shrink_height = Alt+y\n");
    (void)fprintf(file, "keybind.grow_height = Alt+n\n\n");
    (void)fprintf(file, "# Window movement\n");
    (void)fprintf(file, "keybind.move_window_prev_monitor = Alt+bracketleft\n");
    (void)fprintf(file, "keybind.move_window_next_monitor = Alt+bracketright\n\n");
    (void)fprintf(file, "# Workspaces\n");
    for (int i = 1; i <= 9; i++)
    {
        (void)fprintf(file, "keybind.switch_workspace_%d = Alt+%d\n", i, i);
    }
    (void)fprintf(file, "\n");
    (void)fprintf(file, "# System\n");
    (void)fprintf(file, "keybind.logout = Alt+Escape\n");
    if (fclose(file) != 0)
    {
        LogError("CreateDefaultConfig: Failed to close config file: %s", config_path);
    }
    else
    {
        (void)chmod(config_path, CONFIG_FILE_MODE);
        LogInfo("CreateDefaultConfig: Created default config file: %s", config_path);
    }
}
int KeybindMatches(GooeyShellState *state, XKeyEvent *ev, const char *keybind_str)
{
    unsigned int expected_mod_mask = 0U;
    unsigned int actual_mod_mask = 0U;
    unsigned int clean_expected_mod_mask = 0U;
    KeyCode expected_keycode = 0U;
    if ((state == NULL) || (ev == NULL) || (keybind_str == NULL))
    {
        return 0;
    }
    expected_keycode = ParseKeybind(state, keybind_str, &expected_mod_mask);
    if (expected_keycode == 0U)
    {
        return 0;
    }
    if (ev->keycode != expected_keycode)
    {
        return 0;
    }
    actual_mod_mask = ev->state & (unsigned int)(~(LockMask | Mod2Mask));
    clean_expected_mod_mask = expected_mod_mask & (unsigned int)(~(LockMask | Mod2Mask));
    return (actual_mod_mask == clean_expected_mod_mask) ? 1 : 0;
}
int GooeyShell_LoadConfig(GooeyShellState *state, const char *config_path)
{
    char *expanded_path = NULL;
    char *config_dir = NULL;
    char *last_slash = NULL;
    FILE *file = NULL;
    char line[256];
    int result = 0;
    if ((state == NULL) || (config_path == NULL))
    {
        LogError("GooeyShell_LoadConfig: Invalid parameters");
        return 0;
    }
    state->wallpaper_path = NULL;
    expanded_path = ExpandPath(config_path);
    if (expanded_path == NULL)
    {
        LogError("GooeyShell_LoadConfig: Path expansion failed");
        return 0;
    }
    config_dir = strdup(expanded_path);
    if (config_dir != NULL)
    {
        last_slash = strrchr(config_dir, '/');
        if (last_slash != NULL)
        {
            *last_slash = '\0';
            (void)mkdir(config_dir, 0755);
        }
        free(config_dir);
    }
    if (access(expanded_path, F_OK) != 0)
    {
        CreateDefaultConfig(expanded_path);
    }
    file = fopen(expanded_path, "r");
    if (file == NULL)
    {
        LogError("GooeyShell_LoadConfig: Cannot open config file: %s", expanded_path);
        free(expanded_path);
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *equals_pos = strchr(line, '=');
        char *key_start = line;
        char *value_start = NULL;
        while (*key_start == ' ' || *key_start == '\t') {
            key_start++;
        }
        if (*key_start == '#' || *key_start == '\n' || *key_start == '\r' || *key_start == '\0') {
            continue;
        }
        if (equals_pos != NULL) {
            char key[128] = {0};
            size_t key_len = equals_pos - key_start;
            if (key_len > 0 && key_len < sizeof(key) - 1) {
                strncpy(key, key_start, key_len);
                key[key_len] = '\0';
                char *end = key + strlen(key) - 1;
                while (end >= key && (*end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                value_start = equals_pos + 1;
                while (*value_start == ' ' || *value_start == '\t') {
                    value_start++;
                }
                char *value_end = value_start + strlen(value_start) - 1;
                while (value_end >= value_start && (*value_end == '\n' || *value_end == '\r' || *value_end == ' ' || *value_end == '\t')) {
                    *value_end = '\0';
                    value_end--;
                }
                if (strcmp(key, "wallpaper_path") == 0)
                {
                    SAFE_FREE(state->wallpaper_path);
                    if (strlen(value_start) > 0)
                    {
                        state->wallpaper_path = strdup(value_start);
                        LogInfo("Config: wallpaper_path = %s", value_start);
                    }
                }
                else if (strcmp(key, "focused_border_color") == 0)
                {
                    state->focused_border_color = ParseColor(value_start);
                    LogInfo("Config: focused_border_color = %s (0x%06X)", 
                           value_start, state->focused_border_color);
                }
                else if (strcmp(key, "logout_command") == 0)
                {
                    SAFE_FREE(state->logout_command);
                    state->logout_command = strdup(value_start);
                    if (state->logout_command != NULL)
                    {
                        LogInfo("Config: logout_command = %s", value_start);
                    }
                }
                else if (strncmp(key, "keybind.", 8) == 0)
                {
                    const char *keybind_name = key + 8;
                    if (strcmp(keybind_name, "launch_terminal") == 0)
                    {
                        SAFE_FREE(state->keybinds.launch_terminal);
                        state->keybinds.launch_terminal = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "close_window") == 0)
                    {
                        SAFE_FREE(state->keybinds.close_window);
                        state->keybinds.close_window = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "toggle_floating") == 0)
                    {
                        SAFE_FREE(state->keybinds.toggle_floating);
                        state->keybinds.toggle_floating = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "focus_next_window") == 0)
                    {
                        SAFE_FREE(state->keybinds.focus_next_window);
                        state->keybinds.focus_next_window = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "focus_previous_window") == 0)
                    {
                        SAFE_FREE(state->keybinds.focus_previous_window);
                        state->keybinds.focus_previous_window = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "set_tiling_layout") == 0)
                    {
                        SAFE_FREE(state->keybinds.set_tiling_layout);
                        state->keybinds.set_tiling_layout = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "set_monocle_layout") == 0)
                    {
                        SAFE_FREE(state->keybinds.set_monocle_layout);
                        state->keybinds.set_monocle_layout = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "shrink_width") == 0)
                    {
                        SAFE_FREE(state->keybinds.shrink_width);
                        state->keybinds.shrink_width = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "grow_width") == 0)
                    {
                        SAFE_FREE(state->keybinds.grow_width);
                        state->keybinds.grow_width = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "shrink_height") == 0)
                    {
                        SAFE_FREE(state->keybinds.shrink_height);
                        state->keybinds.shrink_height = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "grow_height") == 0)
                    {
                        SAFE_FREE(state->keybinds.grow_height);
                        state->keybinds.grow_height = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "toggle_layout") == 0)
                    {
                        SAFE_FREE(state->keybinds.toggle_layout);
                        state->keybinds.toggle_layout = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "move_window_prev_monitor") == 0)
                    {
                        SAFE_FREE(state->keybinds.move_window_prev_monitor);
                        state->keybinds.move_window_prev_monitor = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "move_window_next_monitor") == 0)
                    {
                        SAFE_FREE(state->keybinds.move_window_next_monitor);
                        state->keybinds.move_window_next_monitor = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "launch_menu") == 0)
                    {
                        SAFE_FREE(state->keybinds.launch_menu);
                        state->keybinds.launch_menu = strdup(value_start);
                    }
                    else if (strcmp(keybind_name, "logout") == 0)
                    {
                        SAFE_FREE(state->keybinds.logout);
                        state->keybinds.logout = strdup(value_start);
                    }
                    else if (strncmp(keybind_name, "switch_workspace_", 17) == 0)
                    {
                        int workspace_num = atoi(keybind_name + 17);
                        if ((workspace_num >= 1) && (workspace_num <= 9))
                        {
                            SAFE_FREE(state->keybinds.switch_workspace[workspace_num - 1]);
                            state->keybinds.switch_workspace[workspace_num - 1] = strdup(value_start);
                        }
                    }
                }
            }
        }
    }
    (void)fclose(file);
    free(expanded_path);
    result = 1;
    return result;
}
void GooeyShell_Logout(GooeyShellState *state)
{
    if (state == NULL)
    {
        return;
    }
    LogInfo("Logging out...");
    if (state->logout_command != NULL)
    {
        int status = system(state->logout_command);
        (void)status;
    }
    else
    {
        exit(0);
    }
}
static int SafeStringCopy(char *dest, size_t dest_size, const char *src)
{
    size_t src_len;
    if ((dest == NULL) || (src == NULL) || (dest_size == 0U))
    {
        return -1;
    }
    src_len = strlen(src);
    if (src_len >= dest_size)
    {
        return -1;
    }
    (void)strcpy(dest, src);
    return 0;
}
static int IsValidWorkspaceIndex(int index)
{
    return ((index >= 0) && (index < 9)) ? 1 : 0;
}
void RegrabKeys(GooeyShellState *state)
{
    if (state == NULL)
    {
        LogError("RegrabKeys: NULL state pointer");
        return;
    }
    if (state->display == NULL)
    {
        LogError("RegrabKeys: NULL display");
        return;
    }
    if (state->root == None)
    {
        LogError("RegrabKeys: Invalid root window");
        return;
    }
    GrabKeys(state);
    LogInfo("RegrabKeys: Keyboard bindings refreshed");
}
int WriteConfigKey(const char *config_path, const char *key, const char *value)
{
    char *expanded_path = NULL;
    FILE *in_file = NULL;
    FILE *out_file = NULL;
    char line[256];
    char temp_path[256];
    int key_found = 0;
    int result = -1;
    if ((config_path == NULL) || (key == NULL) || (value == NULL))
    {
        LogError("WriteConfigKey: Invalid parameters");
        return -1;
    }
    expanded_path = ExpandPath(config_path);
    if (expanded_path == NULL)
    {
        LogError("WriteConfigKey: Path expansion failed");
        return -1;
    }
    if (access(expanded_path, F_OK) != 0)
    {
        CreateDefaultConfig(expanded_path);
    }
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", expanded_path);
    in_file = fopen(expanded_path, "r");
    if (in_file == NULL)
    {
        LogError("WriteConfigKey: Cannot open config file: %s", expanded_path);
        free(expanded_path);
        return -1;
    }
    out_file = fopen(temp_path, "w");
    if (out_file == NULL)
    {
        LogError("WriteConfigKey: Cannot create temp file: %s", temp_path);
        fclose(in_file);
        free(expanded_path);
        return -1;
    }
    while (fgets(line, sizeof(line), in_file) != NULL)
    {
        char *equals_pos = strchr(line, '=');
        char current_key[128] = {0};
        char *line_start = line;
        while (*line_start == ' ' || *line_start == '\t') {
            line_start++;
        }
        if (*line_start == '#' || *line_start == '\n' || *line_start == '\r' || *line_start == '\0') {
            fputs(line, out_file);
            continue;
        }
        if (equals_pos != NULL) {
            size_t key_len = equals_pos - line_start;
            if (key_len > 0 && key_len < sizeof(current_key) - 1) {
                strncpy(current_key, line_start, key_len);
                current_key[key_len] = '\0';
                char *end = current_key + strlen(current_key) - 1;
                while (end >= current_key && (*end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                if (strcmp(current_key, key) == 0) {
                    fprintf(out_file, "%s = %s\n", key, value);
                    key_found = 1;
                    continue;
                }
            }
        }
        fputs(line, out_file);
    }
    if (!key_found) {
        fprintf(out_file, "%s = %s\n", key, value);
    }
    fclose(in_file);
    fclose(out_file);
    if (rename(temp_path, expanded_path) == 0) {
        result = 0;
        LogInfo("WriteConfigKey: Updated %s = %s in %s", key, value, expanded_path);
    } else {
        LogError("WriteConfigKey: Failed to rename temp file: %s", strerror(errno));
        remove(temp_path);
    }
    free(expanded_path);
    return result;
}