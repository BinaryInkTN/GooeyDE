#include <Gooey/gooey.h>
#include "utils/resolution_helper.h"

GooeyLabel* section_description = NULL;
GooeyImage *wallpaper_preview = NULL;
GooeyWindow *win = NULL;
void set_wallpaper_path(const char* wallpaper_path)
{
    GooeyNotifications_Run(win, "Wallpaper Loaded Successfully", NOTIFICATION_SUCCESS, NOTIFICATION_POSITION_TOP_RIGHT);
    GooeyImage_SetImage(wallpaper_preview, wallpaper_path);
}

void browse_fs()
{
    const char* filters[1][2] = {{"Image Files", "*.png*.jpg;*.jpeg;*.svg;"}};
    GooeyFDialog_Open("/", filters, 1, set_wallpaper_path);
}

void create_wallpaper_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *section_title = GooeyLabel_Create("Wallpaper Settings", 28.0f, 60, 150);
    wallpaper_preview = GooeyImage_Create("/usr/local/share/gooeyde/assets/bg.png", 60, 200, 480, 270, NULL, NULL);
    section_description = GooeyLabel_Create("Choose a wallpaper (.png, .jpg, .svg):", 18.0f, 60, 550);
    GooeyButton *wallpaper_selection_btn = GooeyButton_Create("Browse", 380, 530, 100, 30, browse_fs, NULL);
    GooeyTabs_AddWidget(window, tabs, 0, wallpaper_selection_btn);

    GooeyTabs_AddWidget(window, tabs, 0, section_description);
    GooeyTabs_AddWidget(window, tabs, 0, wallpaper_preview);
    GooeyTabs_AddWidget(window, tabs, 0, section_title);
}

int main()
{
    Gooey_Init();
    ScreenInfo screen = get_screen_resolution();
    const int app_width = screen.width;
    const int app_height = screen.height - 50;

    win = GooeyWindow_Create("Environment Config", 0, 0, app_width, app_height, true);

    GooeyTheme *theme = GooeyTheme_LoadFromFile("/usr/local/share/gooeyde/assets/dark.json");
    GooeyWindow_SetTheme(win, theme);

    GooeyTabs *tabs = GooeyTabs_Create(0, 0, app_width, app_height, false);
    GooeyTabs_InsertTab(tabs, "Wallpaper and Themes");
    GooeyTabs_InsertTab(tabs, "Keybinds");
    create_wallpaper_page(win, tabs);
    GooeyWindow_RegisterWidget(win, (GooeyWidget *)tabs);

    GooeyWindow_Run(1, win);
    GooeyWindow_Cleanup(1, win);

    return 0;
}