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

GooeyLabel* section_description = NULL;
GooeyImage *wallpaper_preview = NULL;
GooeyWindow *win = NULL;

int send_wallpaper_to_desktop(const char *wallpaper_path);
void set_wallpaper_path(const char* wallpaper_path);
void browse_fs();
void create_wallpaper_page(GooeyWindow *window, GooeyTabs *tabs);
void create_keybinds_page(GooeyWindow *window, GooeyTabs *tabs);

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
    
    // Create a new method call message
    msg = dbus_message_new_method_call(DBUS_SERVICE,  // Destination
                                       DBUS_PATH,     // Object path
                                       DBUS_INTERFACE, // Interface
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

void set_wallpaper_path(const char* wallpaper_path)
{
    if (!wallpaper_path || strlen(wallpaper_path) == 0)
    {
        GooeyNotifications_Run(win, "No wallpaper selected", NOTIFICATION_ERROR, NOTIFICATION_POSITION_TOP_RIGHT);
        return;
    }
    
    if (access(wallpaper_path, F_OK) != 0)
    {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "File not found: %s", wallpaper_path);
        GooeyNotifications_Run(win, error_msg, NOTIFICATION_ERROR, NOTIFICATION_POSITION_TOP_RIGHT);
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
        GooeyNotifications_Run(win, success_msg, NOTIFICATION_SUCCESS, NOTIFICATION_POSITION_TOP_RIGHT);
        
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
    const char* filters[1][2] = {{"Image Files", "*.png;*.jpg;*.jpeg;*.svg"}};
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
    
    GooeyTabs_AddWidget(window, tabs, 1, keybinds_title);
    GooeyTabs_AddWidget(window, tabs, 1, keybinds_desc);
    GooeyTabs_AddWidget(window, tabs, 1, workspace_label);
    GooeyTabs_AddWidget(window, tabs, 1, workspace_keys);
    GooeyTabs_AddWidget(window, tabs, 1, appmenu_label);
    GooeyTabs_AddWidget(window, tabs, 1, appmenu_keys);
    GooeyTabs_AddWidget(window, tabs, 1, config_label);
    GooeyTabs_AddWidget(window, tabs, 1, config_keys);
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
    
    const int app_width = 600;
    const int app_height = 700;
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
    
    GooeyTabs *tabs = GooeyTabs_Create(0, 0, app_width, app_height, false);
    
   
    GooeyTabs_InsertTab(tabs, "Wallpaper and Themes");
    GooeyTabs_InsertTab(tabs, "Keybinds");
    
    create_wallpaper_page(win, tabs);
    create_keybinds_page(win, tabs);
    
    GooeyLabel *status_label = GooeyLabel_Create("Changes will update desktop immediately", 
                                                 14.0f, 10, app_height - 30);
    GooeyLabel_SetColor(status_label, 0x888888);
    GooeyWindow_RegisterWidget(win, (GooeyWidget *)status_label);
    
    GooeyWindow_RegisterWidget(win, (GooeyWidget *)tabs);
    
    GooeyNotifications_Run(win, "Config Interface Started", NOTIFICATION_INFO, NOTIFICATION_POSITION_TOP_RIGHT);
    
    printf("Config interface started\n");
    printf("DBus service target: %s\n", DBUS_SERVICE);
    printf("DBus object path: %s\n", DBUS_PATH);
    

    GooeyWindow_Run(1, win);
    
    GooeyWindow_Cleanup(1, win);
    
    printf("Config interface shutdown complete\n");
    return 0;
}