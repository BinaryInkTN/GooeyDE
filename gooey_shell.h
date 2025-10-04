#ifndef GOOEY_SHELL_H
#define GOOEY_SHELL_H

#include <X11/Xlib.h>

typedef struct GooeyShellState GooeyShellState;

/* Public API */
GooeyShellState *GooeyShell_Init(void);
void GooeyShell_RunEventLoop(GooeyShellState *state);
void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app);
void GooeyShell_SetBackground(GooeyShellState *state, unsigned long color);
void GooeyShell_Cleanup(GooeyShellState *state);

/* Titlebar control API */
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client);
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled);
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client);

/* Desktop app management */
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client);

/* Constants */
#define WINDOW_MANAGER_NAME "GooeyShell"
#define TITLE_BAR_HEIGHT 25
#define BORDER_WIDTH 2
#define DEFAULT_WIDTH 400
#define DEFAULT_HEIGHT 300
#define BUTTON_SIZE 15
#define BUTTON_MARGIN 5

#endif /* GOOEY_SHELL_H */