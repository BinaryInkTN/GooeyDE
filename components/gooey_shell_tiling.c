#include "gooey_shell.h"
#include "gooey_shell_tiling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void TilingNodeRef(TilingNode *node) {
    if (node) {
        node->ref_count++;
    }
}

void TilingNodeUnref(TilingNode *node) {
    if (node && --node->ref_count <= 0) {
        FreeTilingTree(node->left);
        FreeTilingTree(node->right);
        free(node);
    }
}

void FreeTilingTree(TilingNode *root) {
    if (!root)
        return;

    // Use reference counting to safely free
    TilingNodeUnref(root);
}

TilingNode *CreateTilingNode(WindowNode *window, int x, int y, int width, int height) {
    TilingNode *node = calloc(1, sizeof(TilingNode));
    if (!node) {
        LogError("Failed to allocate tiling node");
        return NULL;
    }

    node->window = window;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    node->x = x;
    node->y = y;
    node->width = width;
    node->height = height;
    node->ratio = 0.5f;
    node->split = SPLIT_NONE;
    node->is_leaf = (window != NULL);
    node->ref_count = 1;
    return node;
}

SplitDirection ChooseSplitDirection(int width, int height, int window_count) {
    if (window_count <= 1)
        return SPLIT_NONE;

    if (width > height * 1.2) {
        return SPLIT_VERTICAL;
    }
    else {
        return SPLIT_HORIZONTAL;
    }
}

int CountLeaves(TilingNode *node) {
    if (!node)
        return 0;
    if (node->is_leaf)
        return 1;
    return CountLeaves(node->left) + CountLeaves(node->right);
}

TilingNode *BuildTreeRecursive(WindowNode **windows, int count, int x, int y, int width, int height, TilingNode *existing_root) {
    if (count == 0)
        return NULL;

    if (count == 1) {
        TilingNode *node = CreateTilingNode(windows[0], x, y, width, height);
        if (!node)
            return NULL;

        if (existing_root) {
            TilingNode *findExistingNode(TilingNode * current, WindowNode * target) {
                if (!current)
                    return NULL;
                if (current->window == target)
                    return current;
                TilingNode *found = findExistingNode(current->left, target);
                if (found)
                    return found;
                return findExistingNode(current->right, target);
            }

            TilingNode *existing = findExistingNode(existing_root, windows[0]);
            if (existing && existing->parent) {
                node->parent = existing->parent;
            }
        }

        return node;
    }

    TilingNode *node = CreateTilingNode(NULL, x, y, width, height);
    if (!node)
        return NULL;

    node->split = ChooseSplitDirection(width, height, count);

    if (existing_root) {
        TilingNode *findSimilarSplit(TilingNode * current, int target_count) {
            if (!current || current->is_leaf)
                return NULL;
            if (CountLeaves(current) == target_count)
                return current;
            TilingNode *found = findSimilarSplit(current->left, target_count);
            if (found)
                return found;
            return findSimilarSplit(current->right, target_count);
        }

        TilingNode *similar = findSimilarSplit(existing_root, count);
        if (similar && similar->split == node->split) {
            node->ratio = similar->ratio;
        }
    }

    int left_count = count / 2;
    int right_count = count - left_count;

    if (node->split == SPLIT_VERTICAL) {
        int left_width = (int)(width * node->ratio);
        int right_width = width - left_width;

        node->left = BuildTreeRecursive(windows, left_count, x, y, left_width, height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count, x + left_width, y, right_width, height, existing_root);
    }
    else {
        int left_height = (int)(height * node->ratio);
        int right_height = height - left_height;

        node->left = BuildTreeRecursive(windows, left_count, x, y, width, left_height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count, x, y + left_height, width, right_height, existing_root);
    }

    if (node->left)
        node->left->parent = node;
    if (node->right)
        node->right->parent = node;

    return node;
}

void BuildDynamicTilingTreeForMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number) {
    if (monitor_number < 0 || monitor_number >= workspace->monitor_tiling_roots_count) {
        return;
    }

    TilingNode *old_root = workspace->monitor_tiling_roots[monitor_number];

    WindowNode **tiled_windows = NULL;
    int tiled_count = 0;
    WindowNode *node = workspace->windows;

    // Count tiled windows
    while (node) {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app &&
            node->monitor_number == monitor_number) {
            tiled_count++;
        }
        node = node->next;
    }

    if (tiled_count == 0) {
        if (old_root) {
            // Don't free immediately, let reference counting handle it
            workspace->monitor_tiling_roots[monitor_number] = NULL;
            TilingNodeUnref(old_root);
        }
        return;
    }

    tiled_windows = malloc(sizeof(WindowNode *) * tiled_count);
    if (!tiled_windows) {
        LogError("Failed to allocate tiled windows array");
        return;
    }

    node = workspace->windows;
    int index = 0;

    while (node && index < tiled_count) {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app &&
            node->monitor_number == monitor_number) {
            tiled_windows[index++] = node;
        }
        node = node->next;
    }

    Monitor *mon = &state->monitor_info.monitors[monitor_number];
    int usable_width = mon->width - 2 * state->outer_gap;
    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;
    int usable_x = mon->x + state->outer_gap;
    int usable_y = mon->y + state->outer_gap + BAR_HEIGHT;

    TilingNode *new_root = BuildTreeRecursive(tiled_windows, tiled_count, usable_x, usable_y, usable_width, usable_height, old_root);

    if (new_root) {
        workspace->monitor_tiling_roots[monitor_number] = new_root;
    }

    // Safe cleanup of old root
    if (old_root) {
        TilingNodeUnref(old_root);
    }

    free(tiled_windows);
}

void BuildDynamicTilingTree(GooeyShellState *state, Workspace *workspace) {
    for (int i = 0; i < workspace->monitor_tiling_roots_count; i++) {
        BuildDynamicTilingTreeForMonitor(state, workspace, i);
    }
}

void ArrangeTilingTree(GooeyShellState *state, TilingNode *node) {
    if (!node)
        return;

    if (node->is_leaf && node->window) {
        int gap = state->inner_gap;
        node->window->x = node->x + gap;
        node->window->y = node->y + gap;
        node->window->width = node->width - 2 * gap;
        node->window->height = node->height - 2 * gap;

        node->window->tiling_x = node->window->x;
        node->window->tiling_y = node->window->y;
        node->window->tiling_width = node->window->width;
        node->window->tiling_height = node->window->height;

        if (node->window->width < 1)
            node->window->width = 1;
        if (node->window->height < 1)
            node->window->height = 1;

        UpdateWindowGeometry(state, node->window);
    }
    else {
        ArrangeTilingTree(state, node->left);
        ArrangeTilingTree(state, node->right);
    }
}

void ArrangeWindowsTilingOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number) {
    BuildDynamicTilingTreeForMonitor(state, workspace, monitor_number);
    ArrangeTilingTree(state, workspace->monitor_tiling_roots[monitor_number]);
}

void ArrangeWindowsTiling(GooeyShellState *state, Workspace *workspace) {
    for (int i = 0; i < workspace->monitor_tiling_roots_count; i++) {
        ArrangeWindowsTilingOnMonitor(state, workspace, i);
    }
}

void ArrangeWindowsMonocleOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number) {
    WindowNode *node = workspace->windows;
    Monitor *mon = &state->monitor_info.monitors[monitor_number];

    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;
    int usable_y = mon->y + state->outer_gap + BAR_HEIGHT;

    if (usable_height <= 0) {
        usable_height = 100;
    }

    while (node) {
        if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
            !node->is_desktop_app && !node->is_fullscreen_app &&
            node->monitor_number == monitor_number) {
            node->x = mon->x + state->outer_gap;
            node->y = usable_y + state->inner_gap;
            node->width = mon->width - 2 * state->outer_gap;
            node->height = usable_height - 2 * state->inner_gap;

            if (node->width < 1)
                node->width = 1;
            if (node->height < 1)
                node->height = 1;

            UpdateWindowGeometry(state, node);
        }
        node = node->next;
    }
}

void ArrangeWindowsMonocle(GooeyShellState *state, Workspace *workspace) {
    for (int i = 0; i < state->monitor_info.num_monitors; i++) {
        ArrangeWindowsMonocleOnMonitor(state, workspace, i);
    }
}

void TileWindowsOnWorkspace(GooeyShellState *state, Workspace *workspace) {
    if (workspace->layout == LAYOUT_TILING) {
        ArrangeWindowsTiling(state, workspace);
    }
    else if (workspace->layout == LAYOUT_MONOCLE) {
        ArrangeWindowsMonocle(state, workspace);
    }
}

int GetTilingResizeArea(GooeyShellState *state, WindowNode *node, int x, int y) {
    if (node->is_floating || node->is_fullscreen || node->is_desktop_app || node->is_fullscreen_app)
        return 0;

    Workspace *workspace = GetCurrentWorkspace(state);
    if (!workspace)
        return 0;

    int resize_handle_size = 6;

    if (x >= node->width - resize_handle_size && x <= node->width + resize_handle_size) {
        return 1;
    }

    if (y >= node->height - resize_handle_size && y <= node->height + resize_handle_size) {
        return 2;
    }

    return 0;
}

void HandleTilingResize(GooeyShellState *state, WindowNode *node, int resize_edge, int delta_x, int delta_y) {
    Workspace *workspace = GetCurrentWorkspace(state);
    if (!workspace || !workspace->monitor_tiling_roots)
        return;

    typedef struct {
        TilingNode *split;
        int is_left_child;
    } SplitInfo;

    SplitInfo splits[32];
    int split_count = 0;

    void collectSplits(TilingNode * current, WindowNode * target) {
        if (!current || current->is_leaf)
            return;

        int in_left = IsWindowInSubtree(current->left, target);
        int in_right = IsWindowInSubtree(current->right, target);

        if (in_left || in_right) {
            if (split_count < 32) {
                splits[split_count].split = current;
                splits[split_count].is_left_child = in_left ? 1 : 0;
                split_count++;
            }

            if (in_left)
                collectSplits(current->left, target);
            if (in_right)
                collectSplits(current->right, target);
        }
    }

    split_count = 0;
    TilingNode *monitor_root = workspace->monitor_tiling_roots[node->monitor_number];
    collectSplits(monitor_root, node);

    if (split_count == 0)
        return;

    TilingNode *target_split = NULL;
    float sensitivity = 0.03f;

    for (int i = 0; i < split_count; i++) {
        TilingNode *split = splits[i].split;
        int is_left = splits[i].is_left_child;

        if (resize_edge == 1) {
            if (split->split == SPLIT_VERTICAL) {
                if ((is_left && delta_x > 0) || (!is_left && delta_x < 0)) {
                    target_split = split;
                    break;
                }
            }
        }
        else if (resize_edge == 2) {
            if (split->split == SPLIT_HORIZONTAL) {
                if ((is_left && delta_y > 0) || (!is_left && delta_y < 0)) {
                    target_split = split;
                    break;
                }
            }
        }
    }

    if (!target_split) {
        for (int i = 0; i < split_count; i++) {
            TilingNode *split = splits[i].split;
            if ((resize_edge == 1 && split->split == SPLIT_VERTICAL) ||
                (resize_edge == 2 && split->split == SPLIT_HORIZONTAL)) {
                target_split = split;
                break;
            }
        }
    }

    if (!target_split)
        return;

    int is_left = 0;
    for (int i = 0; i < split_count; i++) {
        if (splits[i].split == target_split) {
            is_left = splits[i].is_left_child;
            break;
        }
    }

    int direction = is_left ? 1 : -1;

    if (resize_edge == 1 && target_split->split == SPLIT_VERTICAL) {
        target_split->ratio += direction * (float)delta_x / target_split->width * sensitivity;
    }
    else if (resize_edge == 2 && target_split->split == SPLIT_HORIZONTAL) {
        target_split->ratio += direction * (float)delta_y / target_split->height * sensitivity;
    }

    if (target_split->ratio < 0.1f)
        target_split->ratio = 0.1f;
    if (target_split->ratio > 0.9f)
        target_split->ratio = 0.9f;

    LogInfo("Resizing split: %s, ratio: %.2f, direction: %d",
            target_split->split == SPLIT_VERTICAL ? "vertical" : "horizontal",
            target_split->ratio, direction);

    TileWindowsOnWorkspace(state, workspace);
}

void ResizeMasterArea(GooeyShellState *state, Workspace *workspace, int delta_width) {
    Monitor *mon = &state->monitor_info.monitors[0];
    int usable_width = mon->width - 2 * state->outer_gap;

    float ratio_change = (float)delta_width / usable_width;
    workspace->master_ratio += ratio_change;

    if (workspace->master_ratio < 0.1)
        workspace->master_ratio = 0.1;
    if (workspace->master_ratio > 0.9)
        workspace->master_ratio = 0.9;
}

void ResizeHorizontalSplit(GooeyShellState *state, Workspace *workspace, int split_index, int delta_height) {
    if (!workspace->stack_ratios || split_index < 0 || split_index >= workspace->stack_ratios_count - 1)
        return;

    Monitor *mon = &state->monitor_info.monitors[0];
    int usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;

    float ratio_change = (float)delta_height / usable_height;

    workspace->stack_ratios[split_index] += ratio_change;
    workspace->stack_ratios[split_index + 1] -= ratio_change;

    float min_ratio = 0.1f;
    if (workspace->stack_ratios[split_index] < min_ratio) {
        workspace->stack_ratios[split_index + 1] += (workspace->stack_ratios[split_index] - min_ratio);
        workspace->stack_ratios[split_index] = min_ratio;
    }
    if (workspace->stack_ratios[split_index + 1] < min_ratio) {
        workspace->stack_ratios[split_index] += (workspace->stack_ratios[split_index + 1] - min_ratio);
        workspace->stack_ratios[split_index + 1] = min_ratio;
    }
}

void FocusWindow(GooeyShellState *state, WindowNode *node) {
    if (!node)
        return;

    if (state->focused_window != None && state->focused_window != node->frame) {
        WindowNode *prev_node = FindWindowNodeByFrame(state, state->focused_window);
        if (prev_node) {
            XSetWindowBorder(state->display, prev_node->frame, state->border_color);
            if (!prev_node->is_titlebar_disabled) {
                DrawTitleBar(state, prev_node);
            }
        }
    }

    state->focused_window = node->frame;
    XSetWindowBorder(state->display, node->frame, state->focused_border_color);
    XRaiseWindow(state->display, node->frame);

    if (!node->is_titlebar_disabled) {
        DrawTitleBar(state, node);
    }

    XSetInputFocus(state->display, node->client, RevertToParent, CurrentTime);

    XWindowAttributes attr;
    if (XGetWindowAttributes(state->display, node->client, &attr)) {
        if (attr.map_state == IsViewable) {
            XEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.xfocus.type = FocusIn;
            ev.xfocus.window = node->client;
            ev.xfocus.mode = NotifyNormal;
            ev.xfocus.detail = NotifyAncestor;
            XSendEvent(state->display, node->client, False, FocusChangeMask, &ev);
        }
    }
}

WindowNode *GetNextWindow(GooeyShellState *state, WindowNode *current) {
    if (!current) {
        Workspace *ws = GetCurrentWorkspace(state);
        WindowNode *node = ws->windows;
        while (node) {
            if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app) {
                return node;
            }
            node = node->next;
        }
        return NULL;
    }

    WindowNode *node = current->next;
    while (node) {
        if (!node->is_minimized && node->workspace == state->current_workspace &&
            !node->is_desktop_app && !node->is_fullscreen_app) {
            return node;
        }
        node = node->next;
    }

    Workspace *ws = GetCurrentWorkspace(state);
    node = ws->windows;
    while (node && node != current) {
        if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app) {
            return node;
        }
        node = node->next;
    }

    return current;
}

WindowNode *GetPreviousWindow(GooeyShellState *state, WindowNode *current) {
    if (!current) {
        Workspace *ws = GetCurrentWorkspace(state);
        WindowNode *node = ws->windows;
        WindowNode *last = NULL;
        while (node) {
            if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app) {
                last = node;
            }
            node = node->next;
        }
        return last;
    }

    WindowNode *node = current->prev;
    while (node) {
        if (!node->is_minimized && node->workspace == state->current_workspace &&
            !node->is_desktop_app && !node->is_fullscreen_app) {
            return node;
        }
        node = node->prev;
    }

    Workspace *ws = GetCurrentWorkspace(state);
    WindowNode *last = NULL;
    node = ws->windows;
    while (node) {
        if (!node->is_minimized && !node->is_desktop_app && !node->is_fullscreen_app) {
            last = node;
        }
        node = node->next;
    }

    return last ? last : current;
}

void GooeyShell_TileWindows(GooeyShellState *state) {
    LogInfo("GooeyShell_TileWindows called");
    Workspace *workspace = GetCurrentWorkspace(state);
    if (workspace) {
        int tiled_count = 0;
        WindowNode *node = workspace->windows;
        while (node) {
            if (!node->is_floating && !node->is_fullscreen && !node->is_minimized &&
                !node->is_desktop_app && !node->is_fullscreen_app) {
                tiled_count++;
            }
            node = node->next;
        }
        LogInfo("Tiling %d windows on workspace %d", tiled_count, workspace->number);
        TileWindowsOnWorkspace(state, workspace);
    }
}

void GooeyShell_TileWindowsOnMonitor(GooeyShellState *state, int monitor_number) {
    Workspace *workspace = GetCurrentWorkspace(state);
    if (workspace && monitor_number >= 0 && monitor_number < state->monitor_info.num_monitors) {
        ArrangeWindowsTilingOnMonitor(state, workspace, monitor_number);
    }
}

void GooeyShell_RetileAllMonitors(GooeyShellState *state) {
    Workspace *workspace = GetCurrentWorkspace(state);
    if (workspace) {
        TileWindowsOnWorkspace(state, workspace);
    }
}

void GooeyShell_ToggleFloating(GooeyShellState *state, Window client) {
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node || node->is_desktop_app || node->is_fullscreen_app)
        return;

    int old_x = node->x;
    int old_y = node->y;
    int old_width = node->width;
    int old_height = node->height;

    node->is_floating = !node->is_floating;

    if (!node->is_floating) {
        LogInfo("Window returning to tiling layout");

        if (node->tiling_x != -1 && node->tiling_y != -1 &&
            node->tiling_width != -1 && node->tiling_height != -1) {
            node->x = node->tiling_x;
            node->y = node->tiling_y;
            node->width = node->tiling_width;
            node->height = node->tiling_height;
        }

        GooeyShell_TileWindows(state);
    }
    else {
        LogInfo("Window becoming floating");

        node->tiling_x = old_x;
        node->tiling_y = old_y;
        node->tiling_width = old_width;
        node->tiling_height = old_height;

        Monitor *mon = &state->monitor_info.monitors[node->monitor_number];

        if (node->width < 100 || node->height < 100) {
            node->width = mon->width / 2;
            node->height = mon->height / 2;
        }

        node->x = mon->x + (mon->width - node->width) / 2;
        node->y = mon->y + BAR_HEIGHT + (mon->height - BAR_HEIGHT - node->height) / 2;

        if (node->y + node->height > mon->y + mon->height) {
            node->y = mon->y + mon->height - node->height;
        }
        if (node->y < mon->y + BAR_HEIGHT) {
            node->y = mon->y + BAR_HEIGHT;
        }

        UpdateWindowGeometry(state, node);
        XRaiseWindow(state->display, node->frame);
    }
}

void GooeyShell_FocusNextWindow(GooeyShellState *state) {
    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    WindowNode *next = GetNextWindow(state, current);
    if (next) {
        FocusWindow(state, next);
    }
}

void GooeyShell_FocusPreviousWindow(GooeyShellState *state) {
    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    WindowNode *prev = GetPreviousWindow(state, current);
    if (prev) {
        FocusWindow(state, prev);
    }
}

void GooeyShell_MoveWindowToNextMonitor(GooeyShellState *state) {
    if (state->monitor_info.num_monitors <= 1)
        return;

    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    if (!current || current->is_desktop_app || current->is_fullscreen_app)
        return;

    int next_monitor = (current->monitor_number + 1) % state->monitor_info.num_monitors;
    MoveWindowToMonitor(state, current, next_monitor);
}

void GooeyShell_MoveWindowToPreviousMonitor(GooeyShellState *state) {
    if (state->monitor_info.num_monitors <= 1)
        return;

    WindowNode *current = FindWindowNodeByFrame(state, state->focused_window);
    if (!current || current->is_desktop_app || current->is_fullscreen_app)
        return;

    int prev_monitor = (current->monitor_number - 1 + state->monitor_info.num_monitors) % state->monitor_info.num_monitors;
    MoveWindowToMonitor(state, current, prev_monitor);
}

void GooeyShell_FocusNextMonitor(GooeyShellState *state) {
    if (state->monitor_info.num_monitors <= 1)
        return;

    int current_monitor = GetCurrentMonitor(state);
    int next_monitor = (current_monitor + 1) % state->monitor_info.num_monitors;

    state->focused_monitor = next_monitor;

    WindowNode *node = state->window_list;
    while (node) {
        if (node->monitor_number == next_monitor && !node->is_minimized) {
            FocusWindow(state, node);
            break;
        }
        node = node->next;
    }
}

void GooeyShell_FocusPreviousMonitor(GooeyShellState *state) {
    if (state->monitor_info.num_monitors <= 1)
        return;

    int current_monitor = GetCurrentMonitor(state);
    int prev_monitor = (current_monitor - 1 + state->monitor_info.num_monitors) % state->monitor_info.num_monitors;

    state->focused_monitor = prev_monitor;

    WindowNode *node = state->window_list;
    while (node) {
        if (node->monitor_number == prev_monitor && !node->is_minimized) {
            FocusWindow(state, node);
            break;
        }
        node = node->next;
    }
}

void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace) {
    WindowNode *node = FindWindowNodeByClient(state, client);
    if (!node)
        return;

    RemoveWindowFromWorkspace(state, node);
    AddWindowToWorkspace(state, node, workspace);

    if (node->workspace == state->current_workspace) {
        XUnmapWindow(state->display, node->frame);
    }

    if (workspace == state->current_workspace) {
        XMapWindow(state->display, node->frame);
        if (node->is_minimized) {
            RestoreWindow(state, node);
        }
    }

    Workspace *old_ws = GetWorkspace(state, node->workspace);
    if (old_ws) {
        TileWindowsOnWorkspace(state, old_ws);
    }
    GooeyShell_TileWindows(state);
}

void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace) {
    if (workspace == state->current_workspace)
        return;

    int old_workspace = state->current_workspace;

    WindowNode *node = state->window_list;
    while (node) {
        if (node->workspace == state->current_workspace && !node->is_desktop_app) {
            XUnmapWindow(state->display, node->frame);
        }
        node = node->next;
    }

    state->current_workspace = workspace;

    node = state->window_list;
    while (node) {
        if (node->workspace == state->current_workspace && !node->is_desktop_app) {
            XMapWindow(state->display, node->frame);
            if (node->is_minimized) {
                RestoreWindow(state, node);
            }
        }
        node = node->next;
    }

    SendWorkspaceChangedThroughDBus(state, old_workspace, workspace);

    GooeyShell_TileWindows(state);
    RegrabKeys(state);
}

void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout) {
    Workspace *workspace = GetCurrentWorkspace(state);
    workspace->layout = layout;
    state->current_layout = layout;

    TileWindowsOnWorkspace(state, workspace);
}

int IsWindowInSubtree(TilingNode *root, WindowNode *target) {
    if (!root)
        return 0;
    if (root->is_leaf)
        return root->window == target;
    return IsWindowInSubtree(root->left, target) || IsWindowInSubtree(root->right, target);
}

TilingNode *findContainingSplit(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf)
        return NULL;

    int in_left = IsWindowInSubtree(root->left, target);
    int in_right = IsWindowInSubtree(root->right, target);

    if (in_left && in_right) {
        return NULL;
    }

    if (in_left || in_right) {
        return root;
    }

    return NULL;
}

TilingNode *findVerticalSplitToLeft(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf)
        return NULL;

    if (root->split == SPLIT_VERTICAL) {
        if (IsWindowInSubtree(root->right, target)) {
            return root;
        }
    }

    TilingNode *found = findVerticalSplitToLeft(root->left, target);
    if (found)
        return found;
    return findVerticalSplitToLeft(root->right, target);
}

TilingNode *findVerticalSplitToRight(TilingNode *root, WindowNode *target) {
    if (!root || root->is_leaf)
        return NULL;

    if (root->split == SPLIT_VERTICAL) {
        if (IsWindowInSubtree(root->left, target)) {
            return root;
        }
    }

    TilingNode *found = findVerticalSplitToRight(root->left, target);
    if (found)
        return found;
    return findVerticalSplitToRight(root->right, target);
}