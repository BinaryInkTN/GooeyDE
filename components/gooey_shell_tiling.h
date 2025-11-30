#ifndef GOOEY_SHELL_TILING_H
#define GOOEY_SHELL_TILING_H

#include "gooey_shell.h"

// Tiling and workspace function declarations
void InitializeWorkspaces(GooeyShellState *state);
Workspace *GetCurrentWorkspace(GooeyShellState *state);
void TileWindowsOnWorkspace(GooeyShellState *state, Workspace *workspace);
void ArrangeWindowsTiling(GooeyShellState *state, Workspace *workspace);
void ArrangeWindowsMonocle(GooeyShellState *state, Workspace *workspace);
void FocusWindow(GooeyShellState *state, WindowNode *node);
WindowNode *GetNextWindow(GooeyShellState *state, WindowNode *current);
WindowNode *GetPreviousWindow(GooeyShellState *state, WindowNode *current);
void AddWindowToWorkspace(GooeyShellState *state, WindowNode *node, int workspace);
void RemoveWindowFromWorkspace(GooeyShellState *state, WindowNode *node);
Workspace *GetWorkspace(GooeyShellState *state, int workspace_number);
Workspace *CreateWorkspace(GooeyShellState *state, int workspace_number);

int GetTilingResizeArea(GooeyShellState *state, WindowNode *node, int x, int y);
void HandleTilingResize(GooeyShellState *state, WindowNode *node, int resize_edge, int delta_x, int delta_y);
void ResizeMasterArea(GooeyShellState *state, Workspace *workspace, int delta_width);
void ResizeStackWindow(GooeyShellState *state, Workspace *workspace, int window_index, int delta_height);
void ResizeHorizontalSplit(GooeyShellState *state, Workspace *workspace, int split_index, int delta_height);

TilingNode *CreateTilingNode(WindowNode *window, int x, int y, int width, int height);
void FreeTilingTree(TilingNode *root);
void BuildDynamicTilingTree(GooeyShellState *state, Workspace *workspace);
void ArrangeTilingTree(GooeyShellState *state, TilingNode *node);
SplitDirection ChooseSplitDirection(int width, int height, int window_count);
TilingNode *BuildTreeRecursive(WindowNode **windows, int count, int x, int y, int width, int height, TilingNode *existing_root);
void UpdateTilingNodeGeometry(TilingNode *node, int x, int y, int width, int height);
void CleanupWorkspace(Workspace *ws);

void LaunchDesktopAppsForAllMonitors(GooeyShellState *state);
void MoveWindowToMonitor(GooeyShellState *state, WindowNode *node, int monitor_number);
void SetWindowOpacity(GooeyShellState *state, Window window, float opacity);
void InitializeTransparency(GooeyShellState *state);
Atom GetOpacityAtom(GooeyShellState *state);
int GetCurrentMonitor(GooeyShellState *state);
void BuildDynamicTilingTreeForMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number);
void ArrangeWindowsTilingOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number);
void ArrangeWindowsMonocleOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number);

// Tiling node reference counting
void TilingNodeRef(TilingNode *node);
void TilingNodeUnref(TilingNode *node);

// Utility functions for tiling tree navigation
int IsWindowInSubtree(TilingNode *root, WindowNode *target);
TilingNode *findContainingSplit(TilingNode *root, WindowNode *target);
TilingNode *findVerticalSplitToLeft(TilingNode *root, WindowNode *target);
TilingNode *findVerticalSplitToRight(TilingNode *root, WindowNode *target);

// Public API functions
void GooeyShell_TileWindows(GooeyShellState *state);
void GooeyShell_TileWindowsOnMonitor(GooeyShellState *state, int monitor_number);
void GooeyShell_RetileAllMonitors(GooeyShellState *state);
void GooeyShell_ToggleFloating(GooeyShellState *state, Window client);
void GooeyShell_FocusNextWindow(GooeyShellState *state);
void GooeyShell_FocusPreviousWindow(GooeyShellState *state);
void GooeyShell_MoveWindowToNextMonitor(GooeyShellState *state);
void GooeyShell_MoveWindowToPreviousMonitor(GooeyShellState *state);
void GooeyShell_FocusNextMonitor(GooeyShellState *state);
void GooeyShell_FocusPreviousMonitor(GooeyShellState *state);
void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace);
void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace);
void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout);

#endif // GOOEY_SHELL_TILING_H