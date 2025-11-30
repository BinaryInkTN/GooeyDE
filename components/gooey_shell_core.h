#ifndef GOOEY_SHELL_CORE_H
#define GOOEY_SHELL_CORE_H

#include "gooey_shell.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dbus/dbus.h>
#include <GLPS/glps_thread.h>

// Safe memory management macros
#define SAFE_FREE(ptr)  \
    do                  \
    {                   \
        if (ptr)        \
        {               \
            free(ptr);  \
            ptr = NULL; \
        }               \
    } while (0)
#define SAFE_XFREE(ptr) \
    do                  \
    {                   \
        if (ptr)        \
        {               \
            XFree(ptr); \
        }               \
    } while (0)
#define SAFE_CLOSE_DISPLAY(dpy) \
    do                          \
    {                           \
        if (dpy)                \
        {                       \
            XCloseDisplay(dpy); \
        }                       \
    } while (0)
#define SAFE_DESTROY_WINDOW(dpy, win) \
    do                                \
    {                                 \
        if (dpy && win)               \
        {                             \
            XDestroyWindow(dpy, win); \
        }                             \
    } while (0)

// External declarations
extern PrecomputedAtoms atoms;
extern Window *opened_windows;
extern int opened_windows_count;
extern int opened_windows_capacity;
extern volatile int dbus_thread_running;
extern gthread_t dbus_thread;
extern gthread_mutex_t dbus_mutex;
extern gthread_mutex_t window_list_mutex;
extern int window_list_update_pending;
extern int pending_x_flush;

// Function declarations
int IgnoreXError(Display *d, XErrorEvent *e);
void InitializeAtoms(Display *display);
int InitializeMultiMonitor(GooeyShellState *state);
void FreeMultiMonitor(GooeyShellState *state);
int GetMonitorForWindow(GooeyShellState *state, int x, int y, int width, int height);
int CreateFrameWindow(GooeyShellState *state, Window client, int is_desktop_app);
int CreateFullscreenAppWindow(GooeyShellState *state, Window client, int stay_on_top);
void DrawTitleBar(GooeyShellState *state, WindowNode *node);
void HandleButtonPress(GooeyShellState *state, XButtonEvent *ev);
void HandleButtonRelease(GooeyShellState *state, XButtonEvent *ev);
void HandleMotionNotify(GooeyShellState *state, XMotionEvent *ev);
void HandleMouseFocus(GooeyShellState *state, XMotionEvent *ev);
WindowNode *FindWindowNodeByFrame(GooeyShellState *state, Window frame);
WindowNode *FindWindowNodeByClient(GooeyShellState *state, Window client);
void CloseWindow(GooeyShellState *state, WindowNode *node);
void ToggleFullscreen(GooeyShellState *state, WindowNode *node);
void MinimizeWindow(GooeyShellState *state, WindowNode *node);
void RestoreWindow(GooeyShellState *state, WindowNode *node);
int GetTitleBarButtonArea(GooeyShellState *state, WindowNode *node, int x, int y);
int GetResizeBorderArea(GooeyShellState *state, WindowNode *node, int x, int y);
void UpdateWindowGeometry(GooeyShellState *state, WindowNode *node);
void UpdateCursorForWindow(GooeyShellState *state, WindowNode *node, int x, int y);
void SetupDesktopApp(GooeyShellState *state, WindowNode *node);
void SetupFullscreenApp(GooeyShellState *state, WindowNode *node, int stay_on_top);
void EnsureDesktopAppStaysInBackground(GooeyShellState *state);
void EnsureFullscreenAppStaysOnTop(GooeyShellState *state);
Cursor CreateCustomCursor(GooeyShellState *state);
void RemoveWindow(GooeyShellState *state, Window client);
void FreeWindowNode(WindowNode *node);
void ReapZombieProcesses(void);
int IsDesktopAppByProperties(GooeyShellState *state, Window client);
int IsFullscreenAppByProperties(GooeyShellState *state, Window client, int *stay_on_top);
int DetectAppTypeByTitleClass(GooeyShellState *state, Window client, int *is_desktop, int *is_fullscreen, int *stay_on_top);
void SetWindowStateProperties(GooeyShellState *state, Window window, Atom *states, int count);
char *StrDup(const char *str);
void SafeXFree(void *data);
int ValidateWindowState(GooeyShellState *state);
void LogError(const char *message, ...);
void LogInfo(const char *message, ...);
void AddToOpenedWindows(Window window);
void RemoveFromOpenedWindows(Window window);
int IsWindowInOpenedWindows(Window window);
void SendWindowStateThroughDBus(GooeyShellState *state, Window window, const char *st);
void SendWorkspaceChangedThroughDBus(GooeyShellState *state, int old_workspace, int new_workspace);
void HandleDBusWindowCommand(GooeyShellState *state, DBusMessage *msg);
void ScheduleWindowListUpdate(GooeyShellState *state);
void OptimizedXFlush(GooeyShellState *state);
void FocusRootWindow(GooeyShellState *state);
void ProcessDBusMessage(GooeyShellState *state, DBusMessage *msg);
void *DBusListenerThread(void *arg);
void SetupDBUS(GooeyShellState *state);

// Public API functions
void GooeyShell_MarkAsDesktopApp(GooeyShellState *state, Window client);
void GooeyShell_RunEventLoop(GooeyShellState *state);
void GooeyShell_AddFullscreenApp(GooeyShellState *state, const char *command, int stay_on_top);
void GooeyShell_AddWindow(GooeyShellState *state, const char *command, int desktop_app);
void GooeyShell_ToggleTitlebar(GooeyShellState *state, Window client);
void GooeyShell_SetTitlebarEnabled(GooeyShellState *state, Window client, int enabled);
int GooeyShell_IsTitlebarEnabled(GooeyShellState *state, Window client);
Window *GooeyShell_GetOpenedWindows(GooeyShellState *state, int *count);
int GooeyShell_IsWindowOpened(GooeyShellState *state, Window window);
int GooeyShell_IsWindowMinimized(GooeyShellState *state, Window client);
void GooeyShell_Cleanup(GooeyShellState *state);
GooeyShellState *GooeyShell_Init(void);

#endif // GOOEY_SHELL_CORE_H