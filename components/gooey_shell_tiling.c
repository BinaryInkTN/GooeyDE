#include "gooey_shell.h"
#include "gooey_shell_tiling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct SplitInfo
{
    TilingNode *split;
    int is_left_child;
} SplitInfo;
void TilingNodeRef(TilingNode *node)
{
    if (node != NULL)
    {
        node->ref_count++;
    }
}
void TilingNodeUnref(TilingNode *node)
{
    if (node != NULL)
    {
        node->ref_count--;
        if (node->ref_count <= 0)
        {
            FreeTilingTree(node->left);
            FreeTilingTree(node->right);
            free(node);
        }
    }
}
void FreeTilingTree(TilingNode *root)
{
    if (root != NULL)
    {
        TilingNodeUnref(root);
    }
}
TilingNode *CreateTilingNode(WindowNode *window, int x, int y, int width, int height)
{
    TilingNode *node = NULL;
    node = calloc(1, sizeof(TilingNode));
    if (node == NULL)
    {
        LogError("CreateTilingNode: Memory allocation failed");
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
    node->is_leaf = (window != NULL) ? 1 : 0;
    node->ref_count = 1;
    return node;
}
SplitDirection ChooseSplitDirection(int width, int height, int window_count)
{
    if (window_count <= 1)
    {
        return SPLIT_NONE;
    }
    if (width > (int)(height * 1.2f))
    {
        return SPLIT_VERTICAL;
    }
    else
    {
        return SPLIT_HORIZONTAL;
    }
}
int CountLeaves(TilingNode *node)
{
    int count = 0;
    if (node == NULL)
    {
        return 0;
    }
    if (node->is_leaf != 0)
    {
        return 1;
    }
    count = CountLeaves(node->left);
    count += CountLeaves(node->right);
    return count;
}
static TilingNode *findExistingNode(TilingNode *current, WindowNode *target)
{
    TilingNode *found = NULL;
    if (current == NULL)
    {
        return NULL;
    }
    if (current->window == target)
    {
        return current;
    }
    found = findExistingNode(current->left, target);
    if (found != NULL)
    {
        return found;
    }
    found = findExistingNode(current->right, target);
    return found;
}
static TilingNode *findSimilarSplit(TilingNode *current, int target_count)
{
    TilingNode *found = NULL;
    if ((current == NULL) || (current->is_leaf != 0))
    {
        return NULL;
    }
    if (CountLeaves(current) == target_count)
    {
        return current;
    }
    found = findSimilarSplit(current->left, target_count);
    if (found != NULL)
    {
        return found;
    }
    found = findSimilarSplit(current->right, target_count);
    return found;
}
TilingNode *BuildTreeRecursive(WindowNode **windows, int count, int x, int y,
                               int width, int height, TilingNode *existing_root)
{
    TilingNode *node = NULL;
    int left_count = 0;
    int right_count = 0;
    if (count == 0)
    {
        return NULL;
    }
    if (count == 1)
    {
        node = CreateTilingNode(windows[0], x, y, width, height);
        if (node == NULL)
        {
            return NULL;
        }
        if (existing_root != NULL)
        {
            TilingNode *existing = findExistingNode(existing_root, windows[0]);
            if ((existing != NULL) && (existing->parent != NULL))
            {
                node->parent = existing->parent;
            }
        }
        return node;
    }
    node = CreateTilingNode(NULL, x, y, width, height);
    if (node == NULL)
    {
        return NULL;
    }
    node->split = ChooseSplitDirection(width, height, count);
    if (existing_root != NULL)
    {
        TilingNode *similar = findSimilarSplit(existing_root, count);
        if ((similar != NULL) && (similar->split == node->split))
        {
            node->ratio = similar->ratio;
        }
    }
    left_count = count / 2;
    right_count = count - left_count;
    if (node->split == SPLIT_VERTICAL)
    {
        int left_width = (int)((float)width * node->ratio);
        int right_width = width - left_width;
        node->left = BuildTreeRecursive(windows, left_count, x, y,
                                        left_width, height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count,
                                         x + left_width, y, right_width,
                                         height, existing_root);
    }
    else
    {
        int left_height = (int)((float)height * node->ratio);
        int right_height = height - left_height;
        node->left = BuildTreeRecursive(windows, left_count, x, y,
                                        width, left_height, existing_root);
        node->right = BuildTreeRecursive(windows + left_count, right_count,
                                         x, y + left_height, width,
                                         right_height, existing_root);
    }
    if (node->left != NULL)
    {
        node->left->parent = node;
    }
    if (node->right != NULL)
    {
        node->right->parent = node;
    }
    return node;
}
void BuildDynamicTilingTreeForMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number)
{
    WindowNode **tiled_windows = NULL;
    int tiled_count = 0;
    WindowNode *node = NULL;
    Monitor *mon = NULL;
    int bar_height = 0;
    int usable_width = 0;
    int usable_height = 0;
    int usable_x = 0;
    int usable_y = 0;
    TilingNode *old_root = NULL;
    TilingNode *new_root = NULL;
    int index = 0;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("BuildDynamicTilingTreeForMonitor: Invalid parameters");
        return;
    }
    if ((monitor_number < 0) || (monitor_number >= workspace->monitor_tiling_roots_count))
    {
        LogError("BuildDynamicTilingTreeForMonitor: Invalid monitor number %d", monitor_number);
        return;
    }
    old_root = workspace->monitor_tiling_roots[monitor_number];
    node = workspace->windows;
    while (node != NULL)
    {
        if ((node->is_floating == 0) && (node->is_fullscreen == 0) &&
            (node->is_minimized == 0) && (node->is_desktop_app == 0) &&
            (node->is_fullscreen_app == 0) && (node->monitor_number == monitor_number))
        {
            tiled_count++;
        }
        node = node->next;
    }
    if (tiled_count == 0)
    {
        if (old_root != NULL)
        {
            workspace->monitor_tiling_roots[monitor_number] = NULL;
            TilingNodeUnref(old_root);
        }
        return;
    }
    tiled_windows = malloc(sizeof(WindowNode *) * (size_t)tiled_count);
    if (tiled_windows == NULL)
    {
        LogError("BuildDynamicTilingTreeForMonitor: Memory allocation failed");
        return;
    }
    node = workspace->windows;
    index = 0;
    while ((node != NULL) && (index < tiled_count))
    {
        if ((node->is_floating == 0) && (node->is_fullscreen == 0) &&
            (node->is_minimized == 0) && (node->is_desktop_app == 0) &&
            (node->is_fullscreen_app == 0) && (node->monitor_number == monitor_number))
        {
            tiled_windows[index] = node;
            index++;
        }
        node = node->next;
    }
    if (monitor_number < state->monitor_info.num_monitors)
    {
        mon = &state->monitor_info.monitors[monitor_number];
        bar_height = (monitor_number == 0) ? BAR_HEIGHT : 0;
        usable_width = mon->width - 2 * state->outer_gap;
        usable_height = mon->height - 2 * state->outer_gap - bar_height;
        usable_x = mon->x + state->outer_gap;
        usable_y = mon->y + state->outer_gap + bar_height;
        new_root = BuildTreeRecursive(tiled_windows, tiled_count, usable_x, usable_y,
                                      usable_width, usable_height, old_root);
        if (new_root != NULL)
        {
            workspace->monitor_tiling_roots[monitor_number] = new_root;
        }
        if (old_root != NULL)
        {
            TilingNodeUnref(old_root);
        }
    }
    free(tiled_windows);
}
void BuildDynamicTilingTree(GooeyShellState *state, Workspace *workspace)
{
    int i = 0;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("BuildDynamicTilingTree: Invalid parameters");
        return;
    }
    for (i = 0; i < workspace->monitor_tiling_roots_count; i++)
    {
        BuildDynamicTilingTreeForMonitor(state, workspace, i);
    }
}
void ArrangeTilingTree(GooeyShellState *state, TilingNode *node)
{
    int gap = 0;
    int bar_height = 0;

    if ((state == NULL) || (node == NULL))
    {
        return;
    }

    if ((node->is_leaf != 0) && (node->window != NULL))
    {
        gap = state->inner_gap;
        node->window->x = node->x + gap;
        node->window->y = node->y + gap;
        node->window->width = node->width - 2 * gap;

        bar_height = (node->window->monitor_number == 0) ? BAR_HEIGHT : 0;
        node->window->height = node->height - 2 * gap - bar_height;

        node->window->tiling_x = node->window->x;
        node->window->tiling_y = node->window->y;
        node->window->tiling_width = node->window->width;
        node->window->tiling_height = node->window->height;

        if (node->window->width < 1)
        {
            node->window->width = 1;
        }
        if (node->window->height < 1)
        {
            node->window->height = 1;
        }

        UpdateWindowGeometry(state, node->window);
    }
    else
    {
        ArrangeTilingTree(state, node->left);
        ArrangeTilingTree(state, node->right);
    }
}
void ArrangeWindowsTilingOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number)
{
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ArrangeWindowsTilingOnMonitor: Invalid parameters");
        return;
    }
    BuildDynamicTilingTreeForMonitor(state, workspace, monitor_number);
    ArrangeTilingTree(state, workspace->monitor_tiling_roots[monitor_number]);
}
void ArrangeWindowsTiling(GooeyShellState *state, Workspace *workspace)
{
    int i = 0;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ArrangeWindowsTiling: Invalid parameters");
        return;
    }
    for (i = 0; i < workspace->monitor_tiling_roots_count; i++)
    {
        ArrangeWindowsTilingOnMonitor(state, workspace, i);
    }
}
void ArrangeWindowsMonocleOnMonitor(GooeyShellState *state, Workspace *workspace, int monitor_number)
{
    WindowNode *node = NULL;
    Monitor *mon = NULL;
    int usable_height = 0;
    int usable_y = 0;

    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ArrangeWindowsMonocleOnMonitor: Invalid parameters");
        return;
    }

    if ((monitor_number < 0) || (monitor_number >= state->monitor_info.num_monitors))
    {
        LogError("ArrangeWindowsMonocleOnMonitor: Invalid monitor number %d", monitor_number);
        return;
    }

    mon = &state->monitor_info.monitors[monitor_number];
    int bar_height = (monitor_number == 0) ? BAR_HEIGHT : 0;
    usable_height = mon->height - 2 * state->outer_gap - bar_height;
    usable_y = mon->y + state->outer_gap + bar_height;

    if (usable_height <= 0)
    {
        usable_height = 100;
    }

    node = workspace->windows;
    while (node != NULL)
    {
        if ((node->is_floating == 0) && (node->is_fullscreen == 0) &&
            (node->is_minimized == 0) && (node->is_desktop_app == 0) &&
            (node->is_fullscreen_app == 0) && (node->monitor_number == monitor_number))
        {

            node->x = mon->x + state->outer_gap;
            node->y = usable_y + state->inner_gap;
            node->width = mon->width - 2 * state->outer_gap;
            node->height = usable_height - 2 * state->inner_gap - bar_height;

            if (node->width < 1)
            {
                node->width = 1;
            }
            if (node->height < 1)
            {
                node->height = 1;
            }

            UpdateWindowGeometry(state, node);
        }
        node = node->next;
    }
}
void ArrangeWindowsMonocle(GooeyShellState *state, Workspace *workspace)
{
    int i = 0;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ArrangeWindowsMonocle: Invalid parameters");
        return;
    }
    for (i = 0; i < state->monitor_info.num_monitors; i++)
    {
        ArrangeWindowsMonocleOnMonitor(state, workspace, i);
    }
}
void TileWindowsOnWorkspace(GooeyShellState *state, Workspace *workspace)
{
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("TileWindowsOnWorkspace: Invalid parameters");
        return;
    }
    if (workspace->layout == LAYOUT_TILING)
    {
        ArrangeWindowsTiling(state, workspace);
    }
    else if (workspace->layout == LAYOUT_MONOCLE)
    {
        ArrangeWindowsMonocle(state, workspace);
    }
}
int GetTilingResizeArea(GooeyShellState *state, WindowNode *node, int x, int y)
{
    Workspace *workspace = NULL;
    int resize_handle_size = 6;
    if ((state == NULL) || (node == NULL))
    {
        return 0;
    }
    if ((node->is_floating != 0) || (node->is_fullscreen != 0) ||
        (node->is_desktop_app != 0) || (node->is_fullscreen_app != 0))
    {
        return 0;
    }
    workspace = GetCurrentWorkspace(state);
    if (workspace == NULL)
    {
        return 0;
    }
    if ((x >= node->width - resize_handle_size) && (x <= node->width + resize_handle_size))
    {
        return 1;
    }
    if ((y >= node->height - resize_handle_size) && (y <= node->height + resize_handle_size))
    {
        return 2;
    }
    return 0;
}
static void collectSplits(TilingNode *current, WindowNode *target,
                          SplitInfo *splits, int *split_count, int max_splits)
{
    int in_left = 0;
    int in_right = 0;
    if ((current == NULL) || (current->is_leaf != 0) || (*split_count >= max_splits))
    {
        return;
    }
    in_left = IsWindowInSubtree(current->left, target);
    in_right = IsWindowInSubtree(current->right, target);
    if ((in_left != 0) || (in_right != 0))
    {
        splits[*split_count].split = current;
        splits[*split_count].is_left_child = (in_left != 0) ? 1 : 0;
        (*split_count)++;
        if (in_left != 0)
        {
            collectSplits(current->left, target, splits, split_count, max_splits);
        }
        if (in_right != 0)
        {
            collectSplits(current->right, target, splits, split_count, max_splits);
        }
    }
}
void HandleTilingResize(GooeyShellState *state, WindowNode *node,
                        int resize_edge, int delta_x, int delta_y)
{
    Workspace *workspace = NULL;
    SplitInfo splits[32];
    int split_count = 0;
    TilingNode *monitor_root = NULL;
    TilingNode *target_split = NULL;
    int i = 0;
    int is_left = 0;
    int direction = 0;
    float sensitivity = 0.03f;
    if ((state == NULL) || (node == NULL))
    {
        LogError("HandleTilingResize: Invalid parameters");
        return;
    }
    workspace = GetCurrentWorkspace(state);
    if ((workspace == NULL) || (workspace->monitor_tiling_roots == NULL))
    {
        LogError("HandleTilingResize: No workspace or tiling roots");
        return;
    }
    split_count = 0;
    monitor_root = workspace->monitor_tiling_roots[node->monitor_number];
    collectSplits(monitor_root, node, splits, &split_count, 32);
    if (split_count == 0)
    {
        return;
    }
    target_split = NULL;
    for (i = 0; i < split_count; i++)
    {
        TilingNode *split = splits[i].split;
        int is_left_child = splits[i].is_left_child;
        if (resize_edge == 1)
        {
            if (split->split == SPLIT_VERTICAL)
            {
                if ((is_left_child != 0 && delta_x > 0) ||
                    (is_left_child == 0 && delta_x < 0))
                {
                    target_split = split;
                    is_left = is_left_child;
                    break;
                }
            }
        }
        else if (resize_edge == 2)
        {
            if (split->split == SPLIT_HORIZONTAL)
            {
                if ((is_left_child != 0 && delta_y > 0) ||
                    (is_left_child == 0 && delta_y < 0))
                {
                    target_split = split;
                    is_left = is_left_child;
                    break;
                }
            }
        }
    }
    if (target_split == NULL)
    {
        for (i = 0; i < split_count; i++)
        {
            TilingNode *split = splits[i].split;
            if ((resize_edge == 1 && split->split == SPLIT_VERTICAL) ||
                (resize_edge == 2 && split->split == SPLIT_HORIZONTAL))
            {
                target_split = split;
                is_left = splits[i].is_left_child;
                break;
            }
        }
    }
    if (target_split == NULL)
    {
        return;
    }
    direction = (is_left != 0) ? 1 : -1;
    if ((resize_edge == 1) && (target_split->split == SPLIT_VERTICAL))
    {
        target_split->ratio += direction * (float)delta_x / (float)target_split->width * sensitivity;
    }
    else if ((resize_edge == 2) && (target_split->split == SPLIT_HORIZONTAL))
    {
        target_split->ratio += direction * (float)delta_y / (float)target_split->height * sensitivity;
    }
    if (target_split->ratio < 0.1f)
    {
        target_split->ratio = 0.1f;
    }
    if (target_split->ratio > 0.9f)
    {
        target_split->ratio = 0.9f;
    }
    LogInfo("HandleTilingResize: Resizing split: %s, ratio: %.2f, direction: %d",
            target_split->split == SPLIT_VERTICAL ? "vertical" : "horizontal",
            target_split->ratio, direction);
    TileWindowsOnWorkspace(state, workspace);
}
void ResizeMasterArea(GooeyShellState *state, Workspace *workspace, int delta_width)
{
    Monitor *mon = NULL;
    int usable_width = 0;
    float ratio_change = 0.0f;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ResizeMasterArea: Invalid parameters");
        return;
    }
    mon = &state->monitor_info.monitors[0];
    usable_width = mon->width - 2 * state->outer_gap;
    if (usable_width <= 0)
    {
        usable_width = 1;
    }
    ratio_change = (float)delta_width / (float)usable_width;
    workspace->master_ratio += ratio_change;
    if (workspace->master_ratio < 0.1f)
    {
        workspace->master_ratio = 0.1f;
    }
    if (workspace->master_ratio > 0.9f)
    {
        workspace->master_ratio = 0.9f;
    }
}
void ResizeHorizontalSplit(GooeyShellState *state, Workspace *workspace,
                           int split_index, int delta_height)
{
    Monitor *mon = NULL;
    int usable_height = 0;
    float ratio_change = 0.0f;
    float min_ratio = 0.1f;
    if ((state == NULL) || (workspace == NULL))
    {
        LogError("ResizeHorizontalSplit: Invalid parameters");
        return;
    }
    if ((workspace->stack_ratios == NULL) || (split_index < 0) ||
        (split_index >= workspace->stack_ratios_count - 1))
    {
        LogError("ResizeHorizontalSplit: Invalid split index %d", split_index);
        return;
    }
    mon = &state->monitor_info.monitors[0];
    usable_height = mon->height - 2 * state->outer_gap - BAR_HEIGHT;
    if (usable_height <= 0)
    {
        usable_height = 1;
    }
    ratio_change = (float)delta_height / (float)usable_height;
    workspace->stack_ratios[split_index] += ratio_change;
    workspace->stack_ratios[split_index + 1] -= ratio_change;
    if (workspace->stack_ratios[split_index] < min_ratio)
    {
        workspace->stack_ratios[split_index + 1] += (workspace->stack_ratios[split_index] - min_ratio);
        workspace->stack_ratios[split_index] = min_ratio;
    }
    if (workspace->stack_ratios[split_index + 1] < min_ratio)
    {
        workspace->stack_ratios[split_index] += (workspace->stack_ratios[split_index + 1] - min_ratio);
        workspace->stack_ratios[split_index + 1] = min_ratio;
    }
}
void FocusWindow(GooeyShellState *state, WindowNode *node)
{
    WindowNode *current = NULL;
    if ((state == NULL) || (node == NULL))
    {
        LogError("FocusWindow: Invalid parameters");
        return;
    }
    current = state->window_list;
    while (current != NULL)
    {
        if ((current != node) && (current->frame != None) &&
            (current->is_desktop_app == 0) && (current->is_fullscreen_app == 0) &&
            (current->is_minimized == 0))
        {
            XSetWindowBorder(state->display, current->frame, state->border_color);
            if (current->is_titlebar_disabled == 0)
            {
                DrawTitleBar(state, current);
            }
        }
        current = current->next;
    }
    if (node->frame != None)
    {
        XSetWindowBorder(state->display, node->frame, state->focused_border_color);
        if (node->is_titlebar_disabled == 0)
        {
            DrawTitleBar(state, node);
        }
        XSetInputFocus(state->display, node->client, RevertToParent, CurrentTime);
        state->focused_window = node->frame;
        XRaiseWindow(state->display, node->frame);
        SendWindowStateThroughDBus(state, node->frame, "focused");
    }
    OptimizedXFlush(state);
}
WindowNode *GetNextWindow(GooeyShellState *state, WindowNode *current)
{
    Workspace *ws = NULL;
    WindowNode *node = NULL;
    if (state == NULL)
    {
        return NULL;
    }
    if (current == NULL)
    {
        ws = GetCurrentWorkspace(state);
        if (ws == NULL)
        {
            return NULL;
        }
        node = ws->windows;
        while (node != NULL)
        {
            if ((node->is_minimized == 0) && (node->is_desktop_app == 0) &&
                (node->is_fullscreen_app == 0))
            {
                return node;
            }
            node = node->next;
        }
        return NULL;
    }
    node = current->next;
    while (node != NULL)
    {
        if ((node->is_minimized == 0) && (node->workspace == state->current_workspace) &&
            (node->is_desktop_app == 0) && (node->is_fullscreen_app == 0))
        {
            return node;
        }
        node = node->next;
    }
    ws = GetCurrentWorkspace(state);
    if (ws == NULL)
    {
        return NULL;
    }
    node = ws->windows;
    while ((node != NULL) && (node != current))
    {
        if ((node->is_minimized == 0) && (node->is_desktop_app == 0) &&
            (node->is_fullscreen_app == 0))
        {
            return node;
        }
        node = node->next;
    }
    return current;
}
WindowNode *GetPreviousWindow(GooeyShellState *state, WindowNode *current)
{
    Workspace *ws = NULL;
    WindowNode *node = NULL;
    WindowNode *last = NULL;
    if (state == NULL)
    {
        return NULL;
    }
    if (current == NULL)
    {
        ws = GetCurrentWorkspace(state);
        if (ws == NULL)
        {
            return NULL;
        }
        node = ws->windows;
        while (node != NULL)
        {
            if ((node->is_minimized == 0) && (node->is_desktop_app == 0) &&
                (node->is_fullscreen_app == 0))
            {
                last = node;
            }
            node = node->next;
        }
        return last;
    }
    node = current->prev;
    while (node != NULL)
    {
        if ((node->is_minimized == 0) && (node->workspace == state->current_workspace) &&
            (node->is_desktop_app == 0) && (node->is_fullscreen_app == 0))
        {
            return node;
        }
        node = node->prev;
    }
    ws = GetCurrentWorkspace(state);
    if (ws == NULL)
    {
        return NULL;
    }
    last = NULL;
    node = ws->windows;
    while (node != NULL)
    {
        if ((node->is_minimized == 0) && (node->is_desktop_app == 0) &&
            (node->is_fullscreen_app == 0))
        {
            last = node;
        }
        node = node->next;
    }
    return (last != NULL) ? last : current;
}
void GooeyShell_TileWindows(GooeyShellState *state)
{
    Workspace *workspace = NULL;
    int tiled_count = 0;
    WindowNode *node = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_TileWindows: NULL state pointer");
        return;
    }
    LogInfo("GooeyShell_TileWindows called");
    workspace = GetCurrentWorkspace(state);
    if (workspace != NULL)
    {
        node = workspace->windows;
        while (node != NULL)
        {
            if ((node->is_floating == 0) && (node->is_fullscreen == 0) &&
                (node->is_minimized == 0) && (node->is_desktop_app == 0) &&
                (node->is_fullscreen_app == 0))
            {
                tiled_count++;
            }
            node = node->next;
        }
        LogInfo("GooeyShell_TileWindows: Tiling %d windows on workspace %d",
                tiled_count, workspace->number);
        TileWindowsOnWorkspace(state, workspace);
    }
}
void GooeyShell_TileWindowsOnMonitor(GooeyShellState *state, int monitor_number)
{
    Workspace *workspace = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_TileWindowsOnMonitor: NULL state pointer");
        return;
    }
    workspace = GetCurrentWorkspace(state);
    if ((workspace != NULL) && (monitor_number >= 0) &&
        (monitor_number < state->monitor_info.num_monitors))
    {
        ArrangeWindowsTilingOnMonitor(state, workspace, monitor_number);
    }
}
void GooeyShell_RetileAllMonitors(GooeyShellState *state)
{
    Workspace *workspace = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_RetileAllMonitors: NULL state pointer");
        return;
    }
    workspace = GetCurrentWorkspace(state);
    if (workspace != NULL)
    {
        TileWindowsOnWorkspace(state, workspace);
    }
}
void GooeyShell_ToggleFloating(GooeyShellState *state, Window client)
{
    WindowNode *node = NULL;
    int old_x = 0;
    int old_y = 0;
    int old_width = 0;
    int old_height = 0;
    Monitor *mon = NULL;
    int bar_height = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_ToggleFloating: NULL state pointer");
        return;
    }
    node = FindWindowNodeByClient(state, client);
    if ((node == NULL) || (node->is_desktop_app != 0) || (node->is_fullscreen_app != 0))
    {
        return;
    }
    old_x = node->x;
    old_y = node->y;
    old_width = node->width;
    old_height = node->height;
    node->is_floating = (node->is_floating == 0) ? 1 : 0;
    if (node->is_floating == 0)
    {
        LogInfo("GooeyShell_ToggleFloating: Window returning to tiling layout");
        if ((node->tiling_x != -1) && (node->tiling_y != -1) &&
            (node->tiling_width != -1) && (node->tiling_height != -1))
        {
            node->x = node->tiling_x;
            node->y = node->tiling_y;
            node->width = node->tiling_width;
            node->height = node->tiling_height;
        }
        GooeyShell_TileWindows(state);
    }
    else
    {
        LogInfo("GooeyShell_ToggleFloating: Window becoming floating");
        node->tiling_x = old_x;
        node->tiling_y = old_y;
        node->tiling_width = old_width;
        node->tiling_height = old_height;
        if ((node->monitor_number >= 0) &&
            (node->monitor_number < state->monitor_info.num_monitors))
        {
            mon = &state->monitor_info.monitors[node->monitor_number];
            if ((node->width < 100) || (node->height < 100))
            {
                node->width = mon->width / 2;
                node->height = mon->height / 2;
            }
            bar_height = (node->monitor_number == 0) ? BAR_HEIGHT : 0;
            node->x = mon->x + (mon->width - node->width) / 2;
            node->y = mon->y + bar_height + (mon->height - bar_height - node->height) / 2;
            if (node->y + node->height > mon->y + mon->height)
            {
                node->y = mon->y + mon->height - node->height;
            }
            if (node->y < mon->y + bar_height)
            {
                node->y = mon->y + bar_height;
            }
            UpdateWindowGeometry(state, node);
            XRaiseWindow(state->display, node->frame);
        }
    }
}
void GooeyShell_FocusNextWindow(GooeyShellState *state)
{
    WindowNode *current = NULL;
    WindowNode *next = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_FocusNextWindow: NULL state pointer");
        return;
    }
    current = FindWindowNodeByFrame(state, state->focused_window);
    next = GetNextWindow(state, current);
    if (next != NULL)
    {
        FocusWindow(state, next);
    }
}
void GooeyShell_FocusPreviousWindow(GooeyShellState *state)
{
    WindowNode *current = NULL;
    WindowNode *prev = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_FocusPreviousWindow: NULL state pointer");
        return;
    }
    current = FindWindowNodeByFrame(state, state->focused_window);
    prev = GetPreviousWindow(state, current);
    if (prev != NULL)
    {
        FocusWindow(state, prev);
    }
}
void GooeyShell_MoveWindowToNextMonitor(GooeyShellState *state)
{
    WindowNode *current = NULL;
    int next_monitor = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_MoveWindowToNextMonitor: NULL state pointer");
        return;
    }
    if (state->monitor_info.num_monitors <= 1)
    {
        return;
    }
    current = FindWindowNodeByFrame(state, state->focused_window);
    if ((current == NULL) || (current->is_desktop_app != 0) ||
        (current->is_fullscreen_app != 0))
    {
        return;
    }
    next_monitor = (current->monitor_number + 1) % state->monitor_info.num_monitors;
    MoveWindowToMonitor(state, current, next_monitor);
}
void GooeyShell_MoveWindowToPreviousMonitor(GooeyShellState *state)
{
    WindowNode *current = NULL;
    int prev_monitor = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_MoveWindowToPreviousMonitor: NULL state pointer");
        return;
    }
    if (state->monitor_info.num_monitors <= 1)
    {
        return;
    }
    current = FindWindowNodeByFrame(state, state->focused_window);
    if ((current == NULL) || (current->is_desktop_app != 0) ||
        (current->is_fullscreen_app != 0))
    {
        return;
    }
    prev_monitor = (current->monitor_number - 1 + state->monitor_info.num_monitors) %
                   state->monitor_info.num_monitors;
    MoveWindowToMonitor(state, current, prev_monitor);
}
void GooeyShell_FocusNextMonitor(GooeyShellState *state)
{
    WindowNode *node = NULL;
    int next_monitor = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_FocusNextMonitor: NULL state pointer");
        return;
    }
    if (state->monitor_info.num_monitors <= 1)
    {
        return;
    }
    next_monitor = (state->focused_monitor + 1) % state->monitor_info.num_monitors;
    state->focused_monitor = next_monitor;
    node = state->window_list;
    while (node != NULL)
    {
        if ((node->monitor_number == next_monitor) && (node->is_minimized == 0))
        {
            FocusWindow(state, node);
            break;
        }
        node = node->next;
    }
}
void GooeyShell_FocusPreviousMonitor(GooeyShellState *state)
{
    WindowNode *node = NULL;
    int prev_monitor = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_FocusPreviousMonitor: NULL state pointer");
        return;
    }
    if (state->monitor_info.num_monitors <= 1)
    {
        return;
    }
    prev_monitor = (state->focused_monitor - 1 + state->monitor_info.num_monitors) %
                   state->monitor_info.num_monitors;
    state->focused_monitor = prev_monitor;
    node = state->window_list;
    while (node != NULL)
    {
        if ((node->monitor_number == prev_monitor) && (node->is_minimized == 0))
        {
            FocusWindow(state, node);
            break;
        }
        node = node->next;
    }
}
void GooeyShell_MoveWindowToWorkspace(GooeyShellState *state, Window client, int workspace)
{
    WindowNode *node = NULL;
    Workspace *old_ws = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_MoveWindowToWorkspace: NULL state pointer");
        return;
    }
    node = FindWindowNodeByClient(state, client);
    if (node == NULL)
    {
        return;
    }
    RemoveWindowFromWorkspace(state, node);
    AddWindowToWorkspace(state, node, workspace);
    if (node->workspace == state->current_workspace)
    {
        XUnmapWindow(state->display, node->frame);
    }
    if (workspace == state->current_workspace)
    {
        XMapWindow(state->display, node->frame);
        if (node->is_minimized != 0)
        {
            RestoreWindow(state, node);
        }
    }
    old_ws = GetWorkspace(state, node->workspace);
    if (old_ws != NULL)
    {
        TileWindowsOnWorkspace(state, old_ws);
    }
    GooeyShell_TileWindows(state);
}
void GooeyShell_SwitchWorkspace(GooeyShellState *state, int workspace)
{
    WindowNode *node = NULL;
    int old_workspace = 0;
    if (state == NULL)
    {
        LogError("GooeyShell_SwitchWorkspace: NULL state pointer");
        return;
    }
    if (workspace == state->current_workspace)
    {
        return;
    }
    old_workspace = state->current_workspace;
    node = state->window_list;
    while (node != NULL)
    {
        if ((node->workspace == state->current_workspace) && (node->is_desktop_app == 0))
        {
            XUnmapWindow(state->display, node->frame);
        }
        node = node->next;
    }
    state->current_workspace = workspace;
    node = state->window_list;
    while (node != NULL)
    {
        if ((node->workspace == state->current_workspace) && (node->is_desktop_app == 0))
        {
            XMapWindow(state->display, node->frame);
            if (node->is_minimized != 0)
            {
                RestoreWindow(state, node);
            }
        }
        node = node->next;
    }
    SendWorkspaceChangedThroughDBus(state, old_workspace, workspace);
    GooeyShell_TileWindows(state);
    RegrabKeys(state);
}
void GooeyShell_SetLayout(GooeyShellState *state, LayoutMode layout)
{
    Workspace *workspace = NULL;
    if (state == NULL)
    {
        LogError("GooeyShell_SetLayout: NULL state pointer");
        return;
    }
    workspace = GetCurrentWorkspace(state);
    if (workspace != NULL)
    {
        workspace->layout = layout;
        state->current_layout = layout;
        TileWindowsOnWorkspace(state, workspace);
    }
}
int IsWindowInSubtree(TilingNode *root, WindowNode *target)
{
    int found = 0;
    if (root == NULL)
    {
        return 0;
    }
    if (root->is_leaf != 0)
    {
        return (root->window == target) ? 1 : 0;
    }
    found = IsWindowInSubtree(root->left, target);
    if (found != 0)
    {
        return 1;
    }
    found = IsWindowInSubtree(root->right, target);
    return found;
}
TilingNode *findContainingSplit(TilingNode *root, WindowNode *target)
{
    int in_left = 0;
    int in_right = 0;
    if ((root == NULL) || (root->is_leaf != 0))
    {
        return NULL;
    }
    in_left = IsWindowInSubtree(root->left, target);
    in_right = IsWindowInSubtree(root->right, target);
    if ((in_left != 0) && (in_right != 0))
    {
        return NULL;
    }
    if ((in_left != 0) || (in_right != 0))
    {
        return root;
    }
    return NULL;
}
TilingNode *findVerticalSplitToLeft(TilingNode *root, WindowNode *target)
{
    TilingNode *found = NULL;
    if ((root == NULL) || (root->is_leaf != 0))
    {
        return NULL;
    }
    if (root->split == SPLIT_VERTICAL)
    {
        if (IsWindowInSubtree(root->right, target) != 0)
        {
            return root;
        }
    }
    found = findVerticalSplitToLeft(root->left, target);
    if (found != NULL)
    {
        return found;
    }
    found = findVerticalSplitToLeft(root->right, target);
    return found;
}
TilingNode *findVerticalSplitToRight(TilingNode *root, WindowNode *target)
{
    TilingNode *found = NULL;
    if ((root == NULL) || (root->is_leaf != 0))
    {
        return NULL;
    }
    if (root->split == SPLIT_VERTICAL)
    {
        if (IsWindowInSubtree(root->left, target) != 0)
        {
            return root;
        }
    }
    found = findVerticalSplitToRight(root->left, target);
    if (found != NULL)
    {
        return found;
    }
    found = findVerticalSplitToRight(root->right, target);
    return found;
}