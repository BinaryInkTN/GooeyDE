#ifndef GOOEY_SHELL_CONFIG_H
#define GOOEY_SHELL_CONFIG_H
#include "gooey_shell.h"
void GrabKeys(GooeyShellState *state);
void RegrabKeys(GooeyShellState *state);
KeyCode ParseKeybind(GooeyShellState *state, const char *keybind_str, unsigned int *mod_mask);
void InitializeDefaultKeybinds(KeybindConfig *keybinds);
void FreeKeybinds(KeybindConfig *keybinds);
int KeybindMatches(GooeyShellState *state, XKeyEvent *ev, const char *keybind_str);
void HandleMouseFocus(GooeyShellState *state, XMotionEvent *ev);
char *ExpandPath(const char *path);
int ParseColor(const char *color_str);
void CreateDefaultConfig(const char *config_path);
int GooeyShell_LoadConfig(GooeyShellState *state, const char *config_path);
void GooeyShell_Logout(GooeyShellState *state);
#endif