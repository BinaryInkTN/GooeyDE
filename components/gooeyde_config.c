#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <Gooey/gooey.h>
#include <dbus/dbus.h>
#include "utils/resolution_helper.h"

#define DBUS_SERVICE "dev.binaryink.gshell"
#define DBUS_PATH "/dev/binaryink/gshell"
#define DBUS_INTERFACE "dev.binaryink.gshell"

GooeyLabel *section_description = NULL;
GooeyImage *wallpaper_preview = NULL;
GooeyWindow *win = NULL;

int send_wallpaper_to_desktop(const char *wallpaper_path);
void set_wallpaper_path(const char *wallpaper_path);
void browse_fs();

// Callback functions for widgets
void checkbox_changed(bool checked, void *user_data);
void slider_changed(long value, void *user_data);
void theme_button_clicked(void *user_data);
void position_button_clicked(void *user_data);
void compositor_button_clicked(void *user_data);
void dock_position_clicked(void *user_data);
void decoration_button_clicked(void *user_data);
void icon_theme_clicked(void *user_data);
void cursor_theme_clicked(void *user_data);
void ui_scaling_clicked(void *user_data);
void font_type_clicked(void *user_data);
void workspace_count_clicked(void *user_data);
void output_device_clicked(void *user_data);
void power_button_action_clicked(void *user_data);

// Page creation functions
void create_wallpaper_page(GooeyWindow *window, GooeyTabs *tabs);
void create_keybinds_page(GooeyWindow *window, GooeyTabs *tabs);
void create_theme_page(GooeyWindow *window, GooeyTabs *tabs);
void create_widgets_page(GooeyWindow *window, GooeyTabs *tabs);
void create_desktop_effects_page(GooeyWindow *window, GooeyTabs *tabs);
void create_dock_page(GooeyWindow *window, GooeyTabs *tabs);
void create_window_manager_page(GooeyWindow *window, GooeyTabs *tabs);
void create_appearance_page(GooeyWindow *window, GooeyTabs *tabs);
void create_fonts_page(GooeyWindow *window, GooeyTabs *tabs);
void create_mouse_page(GooeyWindow *window, GooeyTabs *tabs);
void create_workspaces_page(GooeyWindow *window, GooeyTabs *tabs);
void create_startup_apps_page(GooeyWindow *window, GooeyTabs *tabs);
void create_network_page(GooeyWindow *window, GooeyTabs *tabs);
void create_power_page(GooeyWindow *window, GooeyTabs *tabs);
void create_sounds_page(GooeyWindow *window, GooeyTabs *tabs);

// Callback implementations
void checkbox_changed(bool checked, void *user_data)
{
    const char *widget_name = (const char *)user_data;
    printf("%s checkbox changed to: %s\n", widget_name, checked ? "checked" : "unchecked");
}

void slider_changed(long value, void *user_data)
{
    const char *slider_name = (const char *)user_data;
    printf("%s slider value changed to: %ld\n", slider_name, value);
}

void theme_button_clicked(void *user_data)
{
    const char *theme_name = (const char *)user_data;
    printf("Theme changed to: %s\n", theme_name);
    GooeyNotifications_Run(win, "Theme updated", NOTIFICATION_SUCCESS, NOTIFICATION_POSITION_BOTTOM_RIGHT);
}

void position_button_clicked(void *user_data)
{
    const char *position = (const char *)user_data;
    printf("Widget position changed to: %s\n", position);
}

void compositor_button_clicked(void *user_data)
{
    const char *state = (const char *)user_data;
    printf("Compositor %s\n", state);
}

void dock_position_clicked(void *user_data)
{
    const char *position = (const char *)user_data;
    printf("Dock position changed to: %s\n", position);
}

void decoration_button_clicked(void *user_data)
{
    const char *decoration = (const char *)user_data;
    printf("Window decoration changed to: %s\n", decoration);
}

void icon_theme_clicked(void *user_data)
{
    const char *theme = (const char *)user_data;
    printf("Icon theme changed to: %s\n", theme);
}

void cursor_theme_clicked(void *user_data)
{
    const char *theme = (const char *)user_data;
    printf("Cursor theme changed to: %s\n", theme);
}

void ui_scaling_clicked(void *user_data)
{
    const char *scale = (const char *)user_data;
    printf("UI scaling changed to: %s\n", scale);
}

void font_type_clicked(void *user_data)
{
    const char *font = (const char *)user_data;
    printf("Font changed to: %s\n", font);
}

void workspace_count_clicked(void *user_data)
{
    const char *count = (const char *)user_data;
    printf("Workspace count changed to: %s\n", count);
}

void output_device_clicked(void *user_data)
{
    const char *device = (const char *)user_data;
    printf("Output device changed to: %s\n", device);
}

void power_button_action_clicked(void *user_data)
{
    const char *action = (const char *)user_data;
    printf("Power button action changed to: %s\n", action);
}

int send_wallpaper_to_desktop(const char *wallpaper_path)
{
    DBusError error;
    DBusConnection *conn = NULL;
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusMessageIter args;
    int ret = 0;

    dbus_error_init(&error);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "DBus Connection Error: %s\n", error.message);
        dbus_error_free(&error);
        return 0;
    }

    if (!conn)
    {
        fprintf(stderr, "Failed to get DBus connection\n");
        return 0;
    }

    msg = dbus_message_new_method_call(DBUS_SERVICE,    // Destination
                                       DBUS_PATH,       // Object path
                                       DBUS_INTERFACE,  // Interface
                                       "SetWallpaper"); // Method name

    if (!msg)
    {
        fprintf(stderr, "Failed to create DBus message\n");
        dbus_connection_unref(conn);
        return 0;
    }

    dbus_message_iter_init_append(msg, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &wallpaper_path))
    {
        fprintf(stderr, "Failed to append arguments\n");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return 0;
    }

    printf("Sending wallpaper to desktop via DBus: %s\n", wallpaper_path);

    reply = dbus_connection_send_with_reply_and_block(conn, msg, 5000, &error);

    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "DBus Error: %s\n", error.message);
        dbus_error_free(&error);
        ret = 0;
    }
    else if (!reply)
    {
        fprintf(stderr, "No reply from desktop\n");
        ret = 0;
    }
    else
    {

        if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
        {
            printf("Desktop accepted wallpaper change\n");
            ret = 1;
        }
        else if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
        {
            char *error_msg = NULL;
            dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &error_msg, DBUS_TYPE_INVALID);
            fprintf(stderr, "Desktop error: %s\n", error_msg ? error_msg : "Unknown error");
            ret = 0;
        }

        dbus_message_unref(reply);
    }

    dbus_message_unref(msg);
    dbus_connection_unref(conn);

    return ret;
}

void set_wallpaper_path(const char *wallpaper_path)
{
    if (!wallpaper_path || strlen(wallpaper_path) == 0)
    {
        GooeyNotifications_Run(win, "No wallpaper selected", NOTIFICATION_ERROR, NOTIFICATION_POSITION_BOTTOM_RIGHT);
        return;
    }

    if (access(wallpaper_path, F_OK) != 0)
    {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "File not found: %s", wallpaper_path);
        GooeyNotifications_Run(win, error_msg, NOTIFICATION_ERROR, NOTIFICATION_POSITION_BOTTOM_RIGHT);
        return;
    }

    GooeyImage_SetImage(wallpaper_preview, wallpaper_path);

    if (send_wallpaper_to_desktop(wallpaper_path))
    {
        char success_msg[256];
        const char *filename = strrchr(wallpaper_path, '/');
        if (filename)
            filename++;
        else
            filename = wallpaper_path;

        snprintf(success_msg, sizeof(success_msg), "Wallpaper set: %s", filename);
        GooeyNotifications_Run(win, success_msg, NOTIFICATION_SUCCESS, NOTIFICATION_POSITION_BOTTOM_RIGHT);

        printf("Wallpaper sent to desktop successfully: %s\n", wallpaper_path);
    }
    else
    {
        GooeyNotifications_Run(win, "Failed to update desktop wallpaper",
                               NOTIFICATION_ERROR, NOTIFICATION_POSITION_TOP_RIGHT);

        fprintf(stderr, "Failed to send wallpaper to desktop\n");
    }
}

void browse_fs()
{
    const char *filters[1][2] = {{"Image Files", "*.png;*.jpg;*.jpeg;*.svg"}};
    GooeyFDialog_Open("/", filters, 1, set_wallpaper_path);
}

void create_wallpaper_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *section_title = GooeyLabel_Create("Wallpaper Settings", 28.0f, 60, 150);
    GooeyLabel_SetColor(section_title, 0xFFFFFF);

    wallpaper_preview = GooeyImage_Create("/usr/local/share/gooeyde/assets/bg.png",
                                          60, 200, 480, 270, NULL, NULL);

    section_description = GooeyLabel_Create("Choose a wallpaper (.png, .jpg, .svg):",
                                            18.0f, 60, 550);
    GooeyLabel_SetColor(section_description, 0xCCCCCC);

    GooeyButton *wallpaper_selection_btn = GooeyButton_Create("Browse", 380, 530, 100, 30,
                                                              browse_fs, NULL);

    GooeyButton *test_dbus_btn = GooeyButton_Create("Test Connection", 380, 570, 150, 30,
                                                    NULL, NULL);

    GooeyLabel *dbus_status = GooeyLabel_Create("DBus: Not Connected", 14.0f, 60, 600);
    GooeyLabel_SetColor(dbus_status, 0xFF8888);

    GooeyTabs_AddWidget(window, tabs, 0, wallpaper_selection_btn);
    GooeyTabs_AddWidget(window, tabs, 0, section_description);
    GooeyTabs_AddWidget(window, tabs, 0, wallpaper_preview);
    GooeyTabs_AddWidget(window, tabs, 0, section_title);
    GooeyTabs_AddWidget(window, tabs, 0, dbus_status);
    GooeyTabs_AddWidget(window, tabs, 0, test_dbus_btn);

    const char *test_wallpaper = "/usr/local/share/gooeyde/assets/bg.png";
    if (send_wallpaper_to_desktop(test_wallpaper))
    {
        GooeyLabel_SetText(dbus_status, "DBus: Connected to Desktop");
        GooeyLabel_SetColor(dbus_status, 0x88FF88);
        printf("DBus connection test successful\n");
    }
    else
    {
        GooeyLabel_SetText(dbus_status, "DBus: Desktop Not Running");
        GooeyLabel_SetColor(dbus_status, 0xFF8888);
        printf("DBus connection test failed - desktop may not be running\n");
    }
}

void create_keybinds_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *keybinds_title = GooeyLabel_Create("Keybind Settings", 28.0f, 60, 150);
    GooeyLabel_SetColor(keybinds_title, 0xFFFFFF);

    GooeyLabel *keybinds_desc = GooeyLabel_Create("Configure keyboard shortcuts",
                                                  18.0f, 60, 200);
    GooeyLabel_SetColor(keybinds_desc, 0xCCCCCC);

    GooeyLabel *workspace_label = GooeyLabel_Create("Workspace Switching:", 16.0f, 60, 250);
    GooeyLabel_SetColor(workspace_label, 0xFFFFFF);

    GooeyLabel *workspace_keys = GooeyLabel_Create("Super + 1-9", 16.0f, 300, 250);
    GooeyLabel_SetColor(workspace_keys, 0x88FF88);

    GooeyLabel *appmenu_label = GooeyLabel_Create("Application Menu:", 16.0f, 60, 300);
    GooeyLabel_SetColor(appmenu_label, 0xFFFFFF);

    GooeyLabel *appmenu_keys = GooeyLabel_Create("Super", 16.0f, 300, 300);
    GooeyLabel_SetColor(appmenu_keys, 0x88FF88);

    GooeyLabel *config_label = GooeyLabel_Create("Config Interface:", 16.0f, 60, 350);
    GooeyLabel_SetColor(config_label, 0xFFFFFF);

    GooeyLabel *config_keys = GooeyLabel_Create("Super + C", 16.0f, 300, 350);
    GooeyLabel_SetColor(config_keys, 0x88FF88);

    GooeyButton *add_keybind_btn = GooeyButton_Create("Add Custom Keybind", 60, 450, 200, 30,
                                                      NULL, NULL);

    GooeyButton *reset_keybinds_btn = GooeyButton_Create("Reset to Defaults", 280, 450, 200, 30,
                                                         NULL, NULL);

    GooeyTabs_AddWidget(window, tabs, 1, keybinds_title);
    GooeyTabs_AddWidget(window, tabs, 1, keybinds_desc);
    GooeyTabs_AddWidget(window, tabs, 1, workspace_label);
    GooeyTabs_AddWidget(window, tabs, 1, workspace_keys);
    GooeyTabs_AddWidget(window, tabs, 1, appmenu_label);
    GooeyTabs_AddWidget(window, tabs, 1, appmenu_keys);
    GooeyTabs_AddWidget(window, tabs, 1, config_label);
    GooeyTabs_AddWidget(window, tabs, 1, config_keys);
    GooeyTabs_AddWidget(window, tabs, 1, add_keybind_btn);
    GooeyTabs_AddWidget(window, tabs, 1, reset_keybinds_btn);
}

void create_theme_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *theme_title = GooeyLabel_Create("Theme Selection", 28.0f, 60, 150);
    GooeyLabel_SetColor(theme_title, 0xFFFFFF);

    GooeyLabel *theme_desc = GooeyLabel_Create("Choose your desktop theme",
                                               18.0f, 60, 200);
    GooeyLabel_SetColor(theme_desc, 0xCCCCCC);

    GooeyButton *dark_theme_btn = GooeyButton_Create("Dark Theme", 60, 250, 150, 40,
                                                     theme_button_clicked, (void *)"Dark");

    GooeyButton *light_theme_btn = GooeyButton_Create("Light Theme", 230, 250, 150, 40,
                                                      theme_button_clicked, (void *)"Light");

    GooeyButton *auto_theme_btn = GooeyButton_Create("Auto (Time-based)", 400, 250, 200, 40,
                                                     theme_button_clicked, (void *)"Auto");

    GooeyLabel *accent_label = GooeyLabel_Create("Accent Color:", 16.0f, 60, 320);
    GooeyLabel_SetColor(accent_label, 0xFFFFFF);

    GooeyButton *blue_accent_btn = GooeyButton_Create("Blue", 60, 360, 80, 30,
                                                      theme_button_clicked, (void *)"Blue");

    GooeyButton *green_accent_btn = GooeyButton_Create("Green", 160, 360, 80, 30,
                                                       theme_button_clicked, (void *)"Green");

    GooeyButton *purple_accent_btn = GooeyButton_Create("Purple", 260, 360, 80, 30,
                                                        theme_button_clicked, (void *)"Purple");

    GooeyButton *orange_accent_btn = GooeyButton_Create("Orange", 360, 360, 80, 30,
                                                        theme_button_clicked, (void *)"Orange");

    GooeyTabs_AddWidget(window, tabs, 2, theme_title);
    GooeyTabs_AddWidget(window, tabs, 2, theme_desc);
    GooeyTabs_AddWidget(window, tabs, 2, dark_theme_btn);
    GooeyTabs_AddWidget(window, tabs, 2, light_theme_btn);
    GooeyTabs_AddWidget(window, tabs, 2, auto_theme_btn);
    GooeyTabs_AddWidget(window, tabs, 2, accent_label);
    GooeyTabs_AddWidget(window, tabs, 2, blue_accent_btn);
    GooeyTabs_AddWidget(window, tabs, 2, green_accent_btn);
    GooeyTabs_AddWidget(window, tabs, 2, purple_accent_btn);
    GooeyTabs_AddWidget(window, tabs, 2, orange_accent_btn);
}

void create_widgets_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *widgets_title = GooeyLabel_Create("Desktop Widgets", 28.0f, 60, 150);
    GooeyLabel_SetColor(widgets_title, 0xFFFFFF);

    GooeyLabel *widgets_desc = GooeyLabel_Create("Enable/disable desktop widgets",
                                                 18.0f, 60, 200);
    GooeyLabel_SetColor(widgets_desc, 0xCCCCCC);

    GooeyCheckbox *clock_widget = GooeyCheckbox_Create(60, 250, "Clock Widget", 
                                                       checkbox_changed, (void *)"Clock Widget");

    GooeyCheckbox *weather_widget = GooeyCheckbox_Create(60, 290, "Weather Widget",
                                                         checkbox_changed, (void *)"Weather Widget");

    GooeyCheckbox *system_monitor = GooeyCheckbox_Create(60, 330, "System Monitor",
                                                         checkbox_changed, (void *)"System Monitor");

    GooeyCheckbox *calendar_widget = GooeyCheckbox_Create(60, 370, "Calendar Widget",
                                                          checkbox_changed, (void *)"Calendar Widget");

    GooeyLabel *widget_position = GooeyLabel_Create("Widget Position:", 16.0f, 300, 320);
    GooeyLabel_SetColor(widget_position, 0xFFFFFF);

    GooeyButton *top_left_btn = GooeyButton_Create("Top Left", 300, 350, 100, 30,
                                                   position_button_clicked, (void *)"Top Left");

    GooeyButton *top_right_btn = GooeyButton_Create("Top Right", 420, 350, 100, 30,
                                                    position_button_clicked, (void *)"Top Right");

    GooeyButton *bottom_left_btn = GooeyButton_Create("Bottom Left", 300, 390, 100, 30,
                                                      position_button_clicked, (void *)"Bottom Left");

    GooeyButton *bottom_right_btn = GooeyButton_Create("Bottom Right", 420, 390, 100, 30,
                                                       position_button_clicked, (void *)"Bottom Right");

    GooeyTabs_AddWidget(window, tabs, 3, widgets_title);
    GooeyTabs_AddWidget(window, tabs, 3, widgets_desc);
    GooeyTabs_AddWidget(window, tabs, 3, (GooeyWidget *)clock_widget);
    GooeyTabs_AddWidget(window, tabs, 3, (GooeyWidget *)weather_widget);
    GooeyTabs_AddWidget(window, tabs, 3, (GooeyWidget *)system_monitor);
    GooeyTabs_AddWidget(window, tabs, 3, (GooeyWidget *)calendar_widget);
    GooeyTabs_AddWidget(window, tabs, 3, widget_position);
    GooeyTabs_AddWidget(window, tabs, 3, top_left_btn);
    GooeyTabs_AddWidget(window, tabs, 3, top_right_btn);
    GooeyTabs_AddWidget(window, tabs, 3, bottom_left_btn);
    GooeyTabs_AddWidget(window, tabs, 3, bottom_right_btn);
}

void create_desktop_effects_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *effects_title = GooeyLabel_Create("Desktop Effects", 28.0f, 60, 150);
    GooeyLabel_SetColor(effects_title, 0xFFFFFF);

    GooeyLabel *effects_desc = GooeyLabel_Create("Configure visual effects and animations",
                                                 18.0f, 60, 200);
    GooeyLabel_SetColor(effects_desc, 0xCCCCCC);

    GooeyLabel *animation_speed = GooeyLabel_Create("Animation Speed:", 16.0f, 60, 250);
    GooeyLabel_SetColor(animation_speed, 0xFFFFFF);

    GooeySlider *speed_slider = GooeySlider_Create(200, 245, 300, 0, 100, true,
                                                   slider_changed, (void *)"Animation Speed");

    GooeyCheckbox *window_animations = GooeyCheckbox_Create(60, 300, "Window Animations",
                                                            checkbox_changed, (void *)"Window Animations");

    GooeyCheckbox *transparency = GooeyCheckbox_Create(60, 340, "Transparency Effects",
                                                       checkbox_changed, (void *)"Transparency");

    GooeyCheckbox *shadows = GooeyCheckbox_Create(60, 380, "Window Shadows",
                                                  checkbox_changed, (void *)"Shadows");

    GooeyCheckbox *blur = GooeyCheckbox_Create(60, 420, "Background Blur",
                                               checkbox_changed, (void *)"Blur");

    GooeyLabel *compositor_label = GooeyLabel_Create("Compositor:", 16.0f, 60, 470);
    GooeyLabel_SetColor(compositor_label, 0xFFFFFF);

    GooeyButton *compositor_on = GooeyButton_Create("Enabled", 150, 465, 100, 30,
                                                    compositor_button_clicked, (void *)"Enabled");

    GooeyButton *compositor_off = GooeyButton_Create("Disabled", 260, 465, 100, 30,
                                                     compositor_button_clicked, (void *)"Disabled");

    GooeyTabs_AddWidget(window, tabs, 4, effects_title);
    GooeyTabs_AddWidget(window, tabs, 4, effects_desc);
    GooeyTabs_AddWidget(window, tabs, 4, animation_speed);
    GooeyTabs_AddWidget(window, tabs, 4, (GooeyWidget *)speed_slider);
    GooeyTabs_AddWidget(window, tabs, 4, (GooeyWidget *)window_animations);
    GooeyTabs_AddWidget(window, tabs, 4, (GooeyWidget *)transparency);
    GooeyTabs_AddWidget(window, tabs, 4, (GooeyWidget *)shadows);
    GooeyTabs_AddWidget(window, tabs, 4, (GooeyWidget *)blur);
    GooeyTabs_AddWidget(window, tabs, 4, compositor_label);
    GooeyTabs_AddWidget(window, tabs, 4, compositor_on);
    GooeyTabs_AddWidget(window, tabs, 4, compositor_off);
}

void create_dock_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *dock_title = GooeyLabel_Create("Dock Settings", 28.0f, 60, 150);
    GooeyLabel_SetColor(dock_title, 0xFFFFFF);

    GooeyLabel *dock_desc = GooeyLabel_Create("Configure the application dock",
                                              18.0f, 60, 200);
    GooeyLabel_SetColor(dock_desc, 0xCCCCCC);

    GooeyLabel *dock_position = GooeyLabel_Create("Dock Position:", 16.0f, 60, 250);
    GooeyLabel_SetColor(dock_position, 0xFFFFFF);

    GooeyButton *dock_bottom = GooeyButton_Create("Bottom", 180, 245, 100, 30,
                                                  dock_position_clicked, (void *)"Bottom");

    GooeyButton *dock_left = GooeyButton_Create("Left", 290, 245, 100, 30,
                                                dock_position_clicked, (void *)"Left");

    GooeyButton *dock_right = GooeyButton_Create("Right", 400, 245, 100, 30,
                                                 dock_position_clicked, (void *)"Right");

    GooeyLabel *dock_behavior = GooeyLabel_Create("Dock Behavior:", 16.0f, 60, 300);
    GooeyLabel_SetColor(dock_behavior, 0xFFFFFF);

    GooeyCheckbox *dock_autohide = GooeyCheckbox_Create(60, 330, "Auto-hide",
                                                        checkbox_changed, (void *)"Dock Auto-hide");

    GooeyCheckbox *dock_intellihide = GooeyCheckbox_Create(60, 370, "Intellihide",
                                                           checkbox_changed, (void *)"Dock Intellihide");

    GooeyLabel *dock_size = GooeyLabel_Create("Dock Size:", 16.0f, 300, 330);
    GooeyLabel_SetColor(dock_size, 0xFFFFFF);

    GooeySlider *size_slider = GooeySlider_Create(300, 360, 200, 30, 100, true,
                                                  slider_changed, (void *)"Dock Size");

    GooeyCheckbox *show_running = GooeyCheckbox_Create(60, 420, "Show Running Apps",
                                                       checkbox_changed, (void *)"Show Running Apps");

    GooeyCheckbox *show_trash = GooeyCheckbox_Create(60, 460, "Show Trash Icon",
                                                     checkbox_changed, (void *)"Show Trash Icon");

    GooeyTabs_AddWidget(window, tabs, 5, dock_title);
    GooeyTabs_AddWidget(window, tabs, 5, dock_desc);
    GooeyTabs_AddWidget(window, tabs, 5, dock_position);
    GooeyTabs_AddWidget(window, tabs, 5, dock_bottom);
    GooeyTabs_AddWidget(window, tabs, 5, dock_left);
    GooeyTabs_AddWidget(window, tabs, 5, dock_right);
    GooeyTabs_AddWidget(window, tabs, 5, dock_behavior);
    GooeyTabs_AddWidget(window, tabs, 5, (GooeyWidget *)dock_autohide);
    GooeyTabs_AddWidget(window, tabs, 5, (GooeyWidget *)dock_intellihide);
    GooeyTabs_AddWidget(window, tabs, 5, dock_size);
    GooeyTabs_AddWidget(window, tabs, 5, (GooeyWidget *)size_slider);
    GooeyTabs_AddWidget(window, tabs, 5, (GooeyWidget *)show_running);
    GooeyTabs_AddWidget(window, tabs, 5, (GooeyWidget *)show_trash);
}

void create_window_manager_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *wm_title = GooeyLabel_Create("Window Manager", 28.0f, 60, 150);
    GooeyLabel_SetColor(wm_title, 0xFFFFFF);

    GooeyLabel *wm_desc = GooeyLabel_Create("Configure window behavior and tiling",
                                            18.0f, 60, 200);
    GooeyLabel_SetColor(wm_desc, 0xCCCCCC);

    GooeyCheckbox *window_tiling = GooeyCheckbox_Create(60, 250, "Enable Window Tiling",
                                                        checkbox_changed, (void *)"Window Tiling");

    GooeyCheckbox *snap_windows = GooeyCheckbox_Create(60, 290, "Snap Windows to Edges",
                                                       checkbox_changed, (void *)"Snap Windows");

    GooeyCheckbox *center_new_windows = GooeyCheckbox_Create(60, 330, "Center New Windows",
                                                             checkbox_changed, (void *)"Center Windows");

    GooeyLabel *window_decoration = GooeyLabel_Create("Window Decorations:", 16.0f, 60, 380);
    GooeyLabel_SetColor(window_decoration, 0xFFFFFF);

    GooeyButton *decoration_none = GooeyButton_Create("None", 60, 410, 100, 30,
                                                      decoration_button_clicked, (void *)"None");

    GooeyButton *decoration_minimal = GooeyButton_Create("Minimal", 170, 410, 100, 30,
                                                         decoration_button_clicked, (void *)"Minimal");

    GooeyButton *decoration_full = GooeyButton_Create("Full", 280, 410, 100, 30,
                                                      decoration_button_clicked, (void *)"Full");

    GooeyLabel *titlebar_buttons = GooeyLabel_Create("Titlebar Buttons:", 16.0f, 60, 460);
    GooeyLabel_SetColor(titlebar_buttons, 0xFFFFFF);

    GooeyCheckbox *show_minimize = GooeyCheckbox_Create(60, 490, "Minimize",
                                                        checkbox_changed, (void *)"Minimize Button");

    GooeyCheckbox *show_maximize = GooeyCheckbox_Create(160, 490, "Maximize",
                                                        checkbox_changed, (void *)"Maximize Button");

    GooeyCheckbox *show_close = GooeyCheckbox_Create(260, 490, "Close",
                                                     checkbox_changed, (void *)"Close Button");

    GooeyTabs_AddWidget(window, tabs, 6, wm_title);
    GooeyTabs_AddWidget(window, tabs, 6, wm_desc);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)window_tiling);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)snap_windows);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)center_new_windows);
    GooeyTabs_AddWidget(window, tabs, 6, window_decoration);
    GooeyTabs_AddWidget(window, tabs, 6, decoration_none);
    GooeyTabs_AddWidget(window, tabs, 6, decoration_minimal);
    GooeyTabs_AddWidget(window, tabs, 6, decoration_full);
    GooeyTabs_AddWidget(window, tabs, 6, titlebar_buttons);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)show_minimize);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)show_maximize);
    GooeyTabs_AddWidget(window, tabs, 6, (GooeyWidget *)show_close);
}

void create_appearance_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *appearance_title = GooeyLabel_Create("Appearance", 28.0f, 60, 150);
    GooeyLabel_SetColor(appearance_title, 0xFFFFFF);

    GooeyLabel *appearance_desc = GooeyLabel_Create("Customize visual appearance",
                                                    18.0f, 60, 200);
    GooeyLabel_SetColor(appearance_desc, 0xCCCCCC);

    GooeyLabel *icon_theme = GooeyLabel_Create("Icon Theme:", 16.0f, 60, 250);
    GooeyLabel_SetColor(icon_theme, 0xFFFFFF);

    GooeyButton *icons_default = GooeyButton_Create("Default", 60, 280, 100, 30,
                                                    icon_theme_clicked, (void *)"Default");

    GooeyButton *icons_flat = GooeyButton_Create("Flat", 180, 280, 100, 30,
                                                 icon_theme_clicked, (void *)"Flat");

    GooeyButton *icons_colorful = GooeyButton_Create("Colorful", 300, 280, 100, 30,
                                                     icon_theme_clicked, (void *)"Colorful");

    GooeyLabel *cursor_theme = GooeyLabel_Create("Cursor Theme:", 16.0f, 60, 330);
    GooeyLabel_SetColor(cursor_theme, 0xFFFFFF);

    GooeyButton *cursor_default = GooeyButton_Create("Default", 60, 360, 100, 30,
                                                     cursor_theme_clicked, (void *)"Default");

    GooeyButton *cursor_modern = GooeyButton_Create("Modern", 180, 360, 100, 30,
                                                    cursor_theme_clicked, (void *)"Modern");

    GooeyButton *cursor_minimal = GooeyButton_Create("Minimal", 300, 360, 100, 30,
                                                     cursor_theme_clicked, (void *)"Minimal");

    GooeyLabel *ui_scaling = GooeyLabel_Create("UI Scaling:", 16.0f, 60, 410);
    GooeyLabel_SetColor(ui_scaling, 0xFFFFFF);

    GooeyButton *scale_100 = GooeyButton_Create("100%", 60, 440, 80, 30,
                                                ui_scaling_clicked, (void *)"100%");

    GooeyButton *scale_125 = GooeyButton_Create("125%", 150, 440, 80, 30,
                                                ui_scaling_clicked, (void *)"125%");

    GooeyButton *scale_150 = GooeyButton_Create("150%", 240, 440, 80, 30,
                                                ui_scaling_clicked, (void *)"150%");

    GooeyButton *scale_200 = GooeyButton_Create("200%", 330, 440, 80, 30,
                                                ui_scaling_clicked, (void *)"200%");

    GooeyTabs_AddWidget(window, tabs, 7, appearance_title);
    GooeyTabs_AddWidget(window, tabs, 7, appearance_desc);
    GooeyTabs_AddWidget(window, tabs, 7, icon_theme);
    GooeyTabs_AddWidget(window, tabs, 7, icons_default);
    GooeyTabs_AddWidget(window, tabs, 7, icons_flat);
    GooeyTabs_AddWidget(window, tabs, 7, icons_colorful);
    GooeyTabs_AddWidget(window, tabs, 7, cursor_theme);
    GooeyTabs_AddWidget(window, tabs, 7, cursor_default);
    GooeyTabs_AddWidget(window, tabs, 7, cursor_modern);
    GooeyTabs_AddWidget(window, tabs, 7, cursor_minimal);
    GooeyTabs_AddWidget(window, tabs, 7, ui_scaling);
    GooeyTabs_AddWidget(window, tabs, 7, scale_100);
    GooeyTabs_AddWidget(window, tabs, 7, scale_125);
    GooeyTabs_AddWidget(window, tabs, 7, scale_150);
    GooeyTabs_AddWidget(window, tabs, 7, scale_200);
}

void create_fonts_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *fonts_title = GooeyLabel_Create("Font Settings", 28.0f, 60, 150);
    GooeyLabel_SetColor(fonts_title, 0xFFFFFF);

    GooeyLabel *fonts_desc = GooeyLabel_Create("Customize system fonts",
                                               18.0f, 60, 200);
    GooeyLabel_SetColor(fonts_desc, 0xCCCCCC);

    GooeyLabel *ui_font = GooeyLabel_Create("Interface Font:", 16.0f, 60, 250);
    GooeyLabel_SetColor(ui_font, 0xFFFFFF);

    GooeyButton *font_sans = GooeyButton_Create("Sans", 60, 280, 100, 30,
                                                font_type_clicked, (void *)"Sans");

    GooeyButton *font_serif = GooeyButton_Create("Serif", 180, 280, 100, 30,
                                                 font_type_clicked, (void *)"Serif");

    GooeyButton *font_mono = GooeyButton_Create("Monospace", 300, 280, 120, 30,
                                                font_type_clicked, (void *)"Monospace");

    GooeyLabel *font_size = GooeyLabel_Create("Font Size:", 16.0f, 60, 330);
    GooeyLabel_SetColor(font_size, 0xFFFFFF);

    GooeySlider *font_size_slider = GooeySlider_Create(60, 360, 300, 8, 20, true,
                                                       slider_changed, (void *)"Font Size");

    GooeyLabel *hinting_label = GooeyLabel_Create("Font Hinting:", 16.0f, 60, 410);
    GooeyLabel_SetColor(hinting_label, 0xFFFFFF);

    GooeyCheckbox *hinting_none = GooeyCheckbox_Create(60, 440, "None",
                                                       checkbox_changed, (void *)"Hinting None");

    GooeyCheckbox *hinting_slight = GooeyCheckbox_Create(160, 440, "Slight",
                                                         checkbox_changed, (void *)"Hinting Slight");

    GooeyCheckbox *hinting_full = GooeyCheckbox_Create(260, 440, "Full",
                                                       checkbox_changed, (void *)"Hinting Full");

    GooeyLabel *antialiasing = GooeyLabel_Create("Antialiasing:", 16.0f, 60, 490);
    GooeyLabel_SetColor(antialiasing, 0xFFFFFF);

    GooeyCheckbox *aa_enabled = GooeyCheckbox_Create(60, 520, "Enabled",
                                                     checkbox_changed, (void *)"Antialiasing");

    GooeyTabs_AddWidget(window, tabs, 8, fonts_title);
    GooeyTabs_AddWidget(window, tabs, 8, fonts_desc);
    GooeyTabs_AddWidget(window, tabs, 8, ui_font);
    GooeyTabs_AddWidget(window, tabs, 8, font_sans);
    GooeyTabs_AddWidget(window, tabs, 8, font_serif);
    GooeyTabs_AddWidget(window, tabs, 8, font_mono);
    GooeyTabs_AddWidget(window, tabs, 8, font_size);
    GooeyTabs_AddWidget(window, tabs, 8, (GooeyWidget *)font_size_slider);
    GooeyTabs_AddWidget(window, tabs, 8, hinting_label);
    GooeyTabs_AddWidget(window, tabs, 8, (GooeyWidget *)hinting_none);
    GooeyTabs_AddWidget(window, tabs, 8, (GooeyWidget *)hinting_slight);
    GooeyTabs_AddWidget(window, tabs, 8, (GooeyWidget *)hinting_full);
    GooeyTabs_AddWidget(window, tabs, 8, antialiasing);
    GooeyTabs_AddWidget(window, tabs, 8, (GooeyWidget *)aa_enabled);
}

void create_mouse_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *mouse_title = GooeyLabel_Create("Mouse & Touchpad", 28.0f, 60, 150);
    GooeyLabel_SetColor(mouse_title, 0xFFFFFF);

    GooeyLabel *mouse_desc = GooeyLabel_Create("Configure pointer behavior",
                                               18.0f, 60, 200);
    GooeyLabel_SetColor(mouse_desc, 0xCCCCCC);

    GooeyLabel *speed_label = GooeyLabel_Create("Pointer Speed:", 16.0f, 60, 250);
    GooeyLabel_SetColor(speed_label, 0xFFFFFF);

    GooeySlider *speed_slider = GooeySlider_Create(60, 280, 300, 1, 10, true,
                                                   slider_changed, (void *)"Pointer Speed");

    GooeyLabel *acceleration = GooeyLabel_Create("Acceleration:", 16.0f, 60, 330);
    GooeyLabel_SetColor(acceleration, 0xFFFFFF);

    GooeyCheckbox *accel_enabled = GooeyCheckbox_Create(60, 360, "Enabled",
                                                        checkbox_changed, (void *)"Acceleration");

    GooeyLabel *natural_scroll = GooeyLabel_Create("Natural Scrolling:", 16.0f, 60, 410);
    GooeyLabel_SetColor(natural_scroll, 0xFFFFFF);

    GooeyCheckbox *natural_enabled = GooeyCheckbox_Create(60, 440, "Enabled",
                                                          checkbox_changed, (void *)"Natural Scrolling");

    GooeyLabel *double_click = GooeyLabel_Create("Double Click Speed:", 16.0f, 60, 490);
    GooeyLabel_SetColor(double_click, 0xFFFFFF);

    GooeySlider *double_click_slider = GooeySlider_Create(60, 520, 300, 100, 1000, true,
                                                          slider_changed, (void *)"Double Click Speed");

    GooeyLabel *tap_to_click = GooeyLabel_Create("Touchpad Tap-to-Click:", 16.0f, 60, 570);
    GooeyLabel_SetColor(tap_to_click, 0xFFFFFF);

    GooeyCheckbox *tap_enabled = GooeyCheckbox_Create(60, 600, "Enabled",
                                                      checkbox_changed, (void *)"Tap to Click");

    GooeyTabs_AddWidget(window, tabs, 9, mouse_title);
    GooeyTabs_AddWidget(window, tabs, 9, mouse_desc);
    GooeyTabs_AddWidget(window, tabs, 9, speed_label);
    GooeyTabs_AddWidget(window, tabs, 9, (GooeyWidget *)speed_slider);
    GooeyTabs_AddWidget(window, tabs, 9, acceleration);
    GooeyTabs_AddWidget(window, tabs, 9, (GooeyWidget *)accel_enabled);
    GooeyTabs_AddWidget(window, tabs, 9, natural_scroll);
    GooeyTabs_AddWidget(window, tabs, 9, (GooeyWidget *)natural_enabled);
    GooeyTabs_AddWidget(window, tabs, 9, double_click);
    GooeyTabs_AddWidget(window, tabs, 9, (GooeyWidget *)double_click_slider);
    GooeyTabs_AddWidget(window, tabs, 9, tap_to_click);
    GooeyTabs_AddWidget(window, tabs, 9, (GooeyWidget *)tap_enabled);
}

void create_workspaces_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *workspaces_title = GooeyLabel_Create("Workspaces", 28.0f, 60, 150);
    GooeyLabel_SetColor(workspaces_title, 0xFFFFFF);

    GooeyLabel *workspaces_desc = GooeyLabel_Create("Configure virtual workspaces",
                                                    18.0f, 60, 200);
    GooeyLabel_SetColor(workspaces_desc, 0xCCCCCC);

    GooeyLabel *num_workspaces = GooeyLabel_Create("Number of Workspaces:", 16.0f, 60, 250);
    GooeyLabel_SetColor(num_workspaces, 0xFFFFFF);

    GooeyButton *ws_4 = GooeyButton_Create("4", 60, 280, 60, 30,
                                           workspace_count_clicked, (void *)"4");

    GooeyButton *ws_6 = GooeyButton_Create("6", 130, 280, 60, 30,
                                           workspace_count_clicked, (void *)"6");

    GooeyButton *ws_9 = GooeyButton_Create("9", 200, 280, 60, 30,
                                           workspace_count_clicked, (void *)"9");

    GooeyButton *ws_custom = GooeyButton_Create("Custom", 270, 280, 80, 30,
                                                workspace_count_clicked, (void *)"Custom");

    GooeyCheckbox *dynamic_workspaces = GooeyCheckbox_Create(60, 330, "Dynamic Workspaces",
                                                             checkbox_changed, (void *)"Dynamic Workspaces");

    GooeyCheckbox *workspace_indicator = GooeyCheckbox_Create(60, 370, "Show Workspace Indicator",
                                                              checkbox_changed, (void *)"Workspace Indicator");

    GooeyLabel *workspace_names = GooeyLabel_Create("Workspace Names:", 16.0f, 60, 420);
    GooeyLabel_SetColor(workspace_names, 0xFFFFFF);

    GooeyButton *edit_names = GooeyButton_Create("Edit Names", 60, 450, 120, 30,
                                                 NULL, NULL);

    GooeyTabs_AddWidget(window, tabs, 10, workspaces_title);
    GooeyTabs_AddWidget(window, tabs, 10, workspaces_desc);
    GooeyTabs_AddWidget(window, tabs, 10, num_workspaces);
    GooeyTabs_AddWidget(window, tabs, 10, ws_4);
    GooeyTabs_AddWidget(window, tabs, 10, ws_6);
    GooeyTabs_AddWidget(window, tabs, 10, ws_9);
    GooeyTabs_AddWidget(window, tabs, 10, ws_custom);
    GooeyTabs_AddWidget(window, tabs, 10, (GooeyWidget *)dynamic_workspaces);
    GooeyTabs_AddWidget(window, tabs, 10, (GooeyWidget *)workspace_indicator);
    GooeyTabs_AddWidget(window, tabs, 10, workspace_names);
    GooeyTabs_AddWidget(window, tabs, 10, edit_names);
}

void create_startup_apps_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *startup_title = GooeyLabel_Create("Startup Applications", 28.0f, 60, 150);
    GooeyLabel_SetColor(startup_title, 0xFFFFFF);

    GooeyLabel *startup_desc = GooeyLabel_Create("Manage applications that start automatically",
                                                 18.0f, 60, 200);
    GooeyLabel_SetColor(startup_desc, 0xCCCCCC);

    GooeyCheckbox *startup_terminal = GooeyCheckbox_Create(60, 250, "Terminal",
                                                           checkbox_changed, (void *)"Terminal Startup");

    GooeyCheckbox *startup_filemanager = GooeyCheckbox_Create(60, 290, "File Manager",
                                                              checkbox_changed, (void *)"File Manager Startup");

    GooeyCheckbox *startup_network = GooeyCheckbox_Create(60, 330, "Network Manager",
                                                          checkbox_changed, (void *)"Network Manager Startup");

    GooeyCheckbox *startup_sound = GooeyCheckbox_Create(60, 370, "Sound Daemon",
                                                        checkbox_changed, (void *)"Sound Daemon Startup");

    GooeyCheckbox *startup_clipboard = GooeyCheckbox_Create(60, 410, "Clipboard Manager",
                                                            checkbox_changed, (void *)"Clipboard Manager Startup");

    GooeyButton *add_startup_app = GooeyButton_Create("Add Application", 60, 460, 150, 30,
                                                      NULL, NULL);

    GooeyButton *remove_startup_app = GooeyButton_Create("Remove Application", 230, 460, 150, 30,
                                                         NULL, NULL);

    GooeyLabel *delay_label = GooeyLabel_Create("Startup Delay (seconds):", 16.0f, 60, 510);
    GooeyLabel_SetColor(delay_label, 0xFFFFFF);

    GooeySlider *delay_slider = GooeySlider_Create(60, 540, 300, 0, 30, true,
                                                   slider_changed, (void *)"Startup Delay");

    GooeyTabs_AddWidget(window, tabs, 11, startup_title);
    GooeyTabs_AddWidget(window, tabs, 11, startup_desc);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)startup_terminal);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)startup_filemanager);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)startup_network);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)startup_sound);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)startup_clipboard);
    GooeyTabs_AddWidget(window, tabs, 11, add_startup_app);
    GooeyTabs_AddWidget(window, tabs, 11, remove_startup_app);
    GooeyTabs_AddWidget(window, tabs, 11, delay_label);
    GooeyTabs_AddWidget(window, tabs, 11, (GooeyWidget *)delay_slider);
}

void create_network_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *network_title = GooeyLabel_Create("Network", 28.0f, 60, 150);
    GooeyLabel_SetColor(network_title, 0xFFFFFF);

    GooeyLabel *network_desc = GooeyLabel_Create("Configure network settings",
                                                 18.0f, 60, 200);
    GooeyLabel_SetColor(network_desc, 0xCCCCCC);

    GooeyLabel *wifi_label = GooeyLabel_Create("Wi-Fi:", 16.0f, 60, 250);
    GooeyLabel_SetColor(wifi_label, 0xFFFFFF);

    GooeyCheckbox *wifi_enabled = GooeyCheckbox_Create(60, 280, "Enabled",
                                                       checkbox_changed, (void *)"Wi-Fi");

    GooeyButton *scan_wifi = GooeyButton_Create("Scan Networks", 180, 275, 140, 30,
                                                NULL, NULL);

    GooeyLabel *vpn_label = GooeyLabel_Create("VPN:", 16.0f, 60, 330);
    GooeyLabel_SetColor(vpn_label, 0xFFFFFF);

    GooeyCheckbox *vpn_enabled = GooeyCheckbox_Create(60, 360, "Enabled",
                                                      checkbox_changed, (void *)"VPN");

    GooeyButton *add_vpn = GooeyButton_Create("Add VPN", 180, 355, 100, 30,
                                              NULL, NULL);

    GooeyLabel *proxy_label = GooeyLabel_Create("Proxy:", 16.0f, 60, 410);
    GooeyLabel_SetColor(proxy_label, 0xFFFFFF);

    GooeyCheckbox *proxy_enabled = GooeyCheckbox_Create(60, 440, "Enabled",
                                                        checkbox_changed, (void *)"Proxy");

    GooeyButton *configure_proxy = GooeyButton_Create("Configure", 180, 435, 100, 30,
                                                      NULL, NULL);

    GooeyTabs_AddWidget(window, tabs, 12, network_title);
    GooeyTabs_AddWidget(window, tabs, 12, network_desc);
    GooeyTabs_AddWidget(window, tabs, 12, wifi_label);
    GooeyTabs_AddWidget(window, tabs, 12, (GooeyWidget *)wifi_enabled);
    GooeyTabs_AddWidget(window, tabs, 12, scan_wifi);
    GooeyTabs_AddWidget(window, tabs, 12, vpn_label);
    GooeyTabs_AddWidget(window, tabs, 12, (GooeyWidget *)vpn_enabled);
    GooeyTabs_AddWidget(window, tabs, 12, add_vpn);
    GooeyTabs_AddWidget(window, tabs, 12, proxy_label);
    GooeyTabs_AddWidget(window, tabs, 12, (GooeyWidget *)proxy_enabled);
    GooeyTabs_AddWidget(window, tabs, 12, configure_proxy);
}

void create_power_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *power_title = GooeyLabel_Create("Power Management", 28.0f, 60, 150);
    GooeyLabel_SetColor(power_title, 0xFFFFFF);

    GooeyLabel *power_desc = GooeyLabel_Create("Configure power saving settings",
                                               18.0f, 60, 200);
    GooeyLabel_SetColor(power_desc, 0xCCCCCC);

    GooeyLabel *sleep_label = GooeyLabel_Create("Sleep After (minutes):", 16.0f, 60, 250);
    GooeyLabel_SetColor(sleep_label, 0xFFFFFF);

    GooeySlider *sleep_slider = GooeySlider_Create(60, 280, 300, 1, 60, true,
                                                   slider_changed, (void *)"Sleep Timer");

    GooeyLabel *screen_off = GooeyLabel_Create("Turn Screen Off After:", 16.0f, 60, 330);
    GooeyLabel_SetColor(screen_off, 0xFFFFFF);

    GooeySlider *screen_off_slider = GooeySlider_Create(60, 360, 300, 1, 30, true,
                                                        slider_changed, (void *)"Screen Off Timer");

    GooeyCheckbox *suspend_on_lid = GooeyCheckbox_Create(60, 410, "Suspend on Lid Close",
                                                         checkbox_changed, (void *)"Suspend on Lid");

    GooeyCheckbox *power_saver = GooeyCheckbox_Create(60, 450, "Power Saver Mode",
                                                      checkbox_changed, (void *)"Power Saver");

    GooeyLabel *power_button = GooeyLabel_Create("Power Button Action:", 16.0f, 60, 500);
    GooeyLabel_SetColor(power_button, 0xFFFFFF);

    GooeyButton *power_suspend = GooeyButton_Create("Suspend", 60, 530, 100, 30,
                                                    power_button_action_clicked, (void *)"Suspend");

    GooeyButton *power_shutdown = GooeyButton_Create("Shutdown", 170, 530, 100, 30,
                                                     power_button_action_clicked, (void *)"Shutdown");

    GooeyButton *power_hibernate = GooeyButton_Create("Hibernate", 280, 530, 100, 30,
                                                      power_button_action_clicked, (void *)"Hibernate");

    GooeyTabs_AddWidget(window, tabs, 13, power_title);
    GooeyTabs_AddWidget(window, tabs, 13, power_desc);
    GooeyTabs_AddWidget(window, tabs, 13, sleep_label);
    GooeyTabs_AddWidget(window, tabs, 13, (GooeyWidget *)sleep_slider);
    GooeyTabs_AddWidget(window, tabs, 13, screen_off);
    GooeyTabs_AddWidget(window, tabs, 13, (GooeyWidget *)screen_off_slider);
    GooeyTabs_AddWidget(window, tabs, 13, (GooeyWidget *)suspend_on_lid);
    GooeyTabs_AddWidget(window, tabs, 13, (GooeyWidget *)power_saver);
    GooeyTabs_AddWidget(window, tabs, 13, power_button);
    GooeyTabs_AddWidget(window, tabs, 13, power_suspend);
    GooeyTabs_AddWidget(window, tabs, 13, power_shutdown);
    GooeyTabs_AddWidget(window, tabs, 13, power_hibernate);
}

void create_sounds_page(GooeyWindow *window, GooeyTabs *tabs)
{
    GooeyLabel *sounds_title = GooeyLabel_Create("Sound Settings", 28.0f, 60, 150);
    GooeyLabel_SetColor(sounds_title, 0xFFFFFF);

    GooeyLabel *sounds_desc = GooeyLabel_Create("Configure audio and notifications",
                                                18.0f, 60, 200);
    GooeyLabel_SetColor(sounds_desc, 0xCCCCCC);

    GooeyLabel *volume_label = GooeyLabel_Create("Master Volume:", 16.0f, 60, 250);
    GooeyLabel_SetColor(volume_label, 0xFFFFFF);

    GooeySlider *volume_slider = GooeySlider_Create(60, 280, 300, 0, 100, true,
                                                    slider_changed, (void *)"Master Volume");

    GooeyCheckbox *event_sounds = GooeyCheckbox_Create(60, 330, "Event Sounds",
                                                       checkbox_changed, (void *)"Event Sounds");

    GooeyCheckbox *feedback_sounds = GooeyCheckbox_Create(60, 370, "Feedback Sounds",
                                                          checkbox_changed, (void *)"Feedback Sounds");

    GooeyLabel *alert_volume = GooeyLabel_Create("Alert Volume:", 16.0f, 60, 420);
    GooeyLabel_SetColor(alert_volume, 0xFFFFFF);

    GooeySlider *alert_slider = GooeySlider_Create(60, 450, 300, 0, 100, true,
                                                   slider_changed, (void *)"Alert Volume");

    GooeyLabel *output_device = GooeyLabel_Create("Output Device:", 16.0f, 60, 500);
    GooeyLabel_SetColor(output_device, 0xFFFFFF);

    GooeyButton *device_speakers = GooeyButton_Create("Speakers", 60, 530, 100, 30,
                                                      output_device_clicked, (void *)"Speakers");

    GooeyButton *device_headphones = GooeyButton_Create("Headphones", 170, 530, 100, 30,
                                                        output_device_clicked, (void *)"Headphones");

    GooeyButton *device_hdmi = GooeyButton_Create("HDMI", 280, 530, 100, 30,
                                                  output_device_clicked, (void *)"HDMI");

    GooeyTabs_AddWidget(window, tabs, 14, sounds_title);
    GooeyTabs_AddWidget(window, tabs, 14, sounds_desc);
    GooeyTabs_AddWidget(window, tabs, 14, volume_label);
    GooeyTabs_AddWidget(window, tabs, 14, (GooeyWidget *)volume_slider);
    GooeyTabs_AddWidget(window, tabs, 14, (GooeyWidget *)event_sounds);
    GooeyTabs_AddWidget(window, tabs, 14, (GooeyWidget *)feedback_sounds);
    GooeyTabs_AddWidget(window, tabs, 14, alert_volume);
    GooeyTabs_AddWidget(window, tabs, 14, (GooeyWidget *)alert_slider);
    GooeyTabs_AddWidget(window, tabs, 14, output_device);
    GooeyTabs_AddWidget(window, tabs, 14, device_speakers);
    GooeyTabs_AddWidget(window, tabs, 14, device_headphones);
    GooeyTabs_AddWidget(window, tabs, 14, device_hdmi);
}

void close_application()
{
    GooeyWindow_Cleanup(1, win);
    exit(0);
}

int main()
{
    Gooey_Init();

    ScreenInfo screen = get_screen_resolution();
    if (screen.width == 0 || screen.height == 0)
    {
        fprintf(stderr, "Failed to get screen resolution, using defaults\n");
        screen.width = 800;
        screen.height = 600;
    }

    const int app_width = screen.width;
    const int app_height = screen.height;
    const int x_pos = (screen.width - app_width) / 2;
    const int y_pos = (screen.height - app_height) / 2;

    win = GooeyWindow_Create("GooeyDE Config", x_pos, y_pos, app_width, app_height, false);

    GooeyTheme *theme = GooeyTheme_LoadFromFile("/usr/local/share/gooeyde/assets/dark.json");
    if (theme)
    {
        GooeyWindow_SetTheme(win, theme);
    }
    else
    {
        fprintf(stderr, "Failed to load theme\n");
    }

    GooeyTabs *tabs = GooeyTabs_Create(0, 80, app_width, app_height, false);
    GooeyImage *close_button = GooeyImage_Create("/usr/local/share/gooeyde/assets/cross.png", app_width - 50, 20, 32, 32, close_application, NULL);
    
    GooeyLabel *title = GooeyLabel_Create("Environment Setup", 28.0f, 30, 50);
    if (title)
    {
        GooeyWindow_RegisterWidget(win, (GooeyWidget *) title);
    }
    tabs->is_sidebar = 1;
    GooeyTabs_InsertTab(tabs, "Wallpaper and Themes");
    GooeyTabs_InsertTab(tabs, "Keybinds");
    GooeyTabs_InsertTab(tabs, "Theme Selection");
    GooeyTabs_InsertTab(tabs, "Desktop Widgets");
    GooeyTabs_InsertTab(tabs, "Desktop Effects");
    GooeyTabs_InsertTab(tabs, "Dock Settings");
    GooeyTabs_InsertTab(tabs, "Window Manager");
    GooeyTabs_InsertTab(tabs, "Appearance");
    GooeyTabs_InsertTab(tabs, "Fonts");
    GooeyTabs_InsertTab(tabs, "Mouse & Touchpad");
    GooeyTabs_InsertTab(tabs, "Workspaces");
    GooeyTabs_InsertTab(tabs, "Startup Apps");
    GooeyTabs_InsertTab(tabs, "Network");
    GooeyTabs_InsertTab(tabs, "Power Management");
    GooeyTabs_InsertTab(tabs, "Sound Settings");

    if (close_button)
    {
        GooeyWindow_RegisterWidget(win, (GooeyWidget *)close_button);
    }

    // Create all pages
    create_wallpaper_page(win, tabs);
    create_keybinds_page(win, tabs);
    create_theme_page(win, tabs);
    create_widgets_page(win, tabs);
    create_desktop_effects_page(win, tabs);
    create_dock_page(win, tabs);
    create_window_manager_page(win, tabs);
    create_appearance_page(win, tabs);
    create_fonts_page(win, tabs);
    create_mouse_page(win, tabs);
    create_workspaces_page(win, tabs);
    create_startup_apps_page(win, tabs);
    create_network_page(win, tabs);
    create_power_page(win, tabs);
    create_sounds_page(win, tabs);

    GooeyLabel *status_label = GooeyLabel_Create("Changes will update desktop immediately",
                                                 14.0f, 10, app_height - 30);
    GooeyLabel_SetColor(status_label, 0x888888);
    GooeyWindow_RegisterWidget(win, (GooeyWidget *)status_label);

    GooeyWindow_RegisterWidget(win, (GooeyWidget *)tabs);

    GooeyNotifications_Run(win, "Config Interface Started", NOTIFICATION_INFO, NOTIFICATION_POSITION_BOTTOM_RIGHT);

    printf("Config interface started\n");
    printf("DBus service target: %s\n", DBUS_SERVICE);
    printf("DBus object path: %s\n", DBUS_PATH);
    printf("Total tabs: 15\n");

    GooeyWindow_Run(1, win);
    GooeyWindow_Cleanup(1, win);

    printf("Config interface shutdown complete\n");
    return 0;
}