#include <Gooey/gooey.h>
#include "utils/resolution_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <GLPS/glps_thread.h>
#include <dbus/dbus.h>
#include "utils/devices_helper.h"

ScreenInfo screen_info;
GooeyWindow *win = NULL;
GooeyLabel *time_label = NULL;
GooeyLabel *date_label = NULL;
GooeyImage *control_panel_bg = NULL;
int time_thread_running = 1;
int control_panel_visible = 0;

GooeySwitch *wifi_switch = NULL;
GooeySwitch *bluetooth_switch = NULL;
GooeySwitch *airplane_switch = NULL;
GooeySlider *volume_slider = NULL;
GooeySlider *brightness_slider = NULL;

GooeyLabel *wifi_label = NULL;
GooeyLabel *bluetooth_label = NULL;
GooeyLabel *airplane_label = NULL;
GooeyLabel *volume_label = NULL;
GooeyLabel *brightness_label = NULL;
GooeyLabel *volume_value_label = NULL;
GooeyLabel *brightness_value_label = NULL;
GooeyLabel *panel_title = NULL;
GooeyLabel *settings_title = NULL;
GooeyLabel *system_title = NULL;
GooeyLabel *battery_label = NULL;
GooeyLabel *network_label = NULL;
GooeyButton *settings_button = NULL;
GooeyLabel *settings_label = NULL;
GooeyLabel *unsupported_devices_label = NULL;

GooeyImage *wifi_status_icon = NULL;
GooeyImage *bluetooth_status_icon = NULL;
GooeyImage *volume_status_icon = NULL;
GooeyImage *battery_status_icon = NULL;
GooeyLabel *battery_percent_label = NULL;

GooeyImage *wallpaper_dialog_bg = NULL;
GooeyLabel *wallpaper_dialog_title = NULL;
GooeyButton *wallpaper_browse_button = NULL;
GooeyButton *wallpaper_cancel_button = NULL;
GooeyLabel *wallpaper_current_label = NULL;
GooeyLabel *wallpaper_selected_label = NULL;
GooeyImage *current_wallpaper_preview = NULL;
GooeyImage *wallpaper = NULL;
GooeyButton *logout_button = NULL;
char current_wallpaper_path[256] = "/usr/local/share/gooeyde/assets/bg.png";
char selected_wallpaper_path[256] = "";
int wallpaper_dialog_visible = 0;

int current_volume = 75;
int current_brightness = 80;
int battery_level = 85;
char network_status[64] = "Connected";

static DBusConnection *dbus_conn = NULL;
static int dbus_initialized = 0;
static int dbus_thread_running = 1;

static gthread_mutex_t ui_update_mutex;

static GooeyLabel *workspace_indicator = NULL;
static int current_workspace = 1;

#define DBUS_SERVICE "dev.binaryink.gshell"
#define DBUS_PATH "/dev/binaryink/gshell"
#define DBUS_INTERFACE "dev.binaryink.gshell"

void execute_system_command(const char *command);
void init_system_settings();
void update_status_icons();
void run_app_menu();
void run_systemsettings();
void update_time_date();
void *time_update_thread(void *arg);
void toggle_control_panel();
void create_control_panel();
void destroy_control_panel();
void set_control_panel_visibility(int visible);
void wifi_switch_callback(bool value, void *user_data);
void bluetooth_switch_callback(bool value, void *user_data);
void airplane_switch_callback(bool value, void *user_data);
void volume_slider_callback(long value, void *user_data);
void brightness_slider_callback(long value, void *user_data);
void mute_audio_callback();
void refresh_system_info();

int init_dbus_connection();
void cleanup_dbus();
void *dbus_monitor_thread(void *arg);
void process_dbus_message(DBusMessage *message);
void handle_window_list_updated(DBusMessage *message);
void handle_window_state_changed(DBusMessage *message);
void handle_workspace_changed(DBusMessage *message);

void open_wallpaper_settings();
void wallpaper_dialog_callback(const char *filename);
void set_wallpaper(const char *filename);
void create_wallpaper_dialog();
void destroy_wallpaper_dialog();
void set_wallpaper_dialog_visibility(int visible);

void update_workspace_indicator(int workspace)
{
    char workspace_text[32];
    snprintf(workspace_text, sizeof(workspace_text), "Workspace %d", workspace);

    glps_thread_mutex_lock(&ui_update_mutex);
    if (workspace_indicator)
    {
        GooeyLabel_SetText(workspace_indicator, workspace_text);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);

    char notification_text[64];
    snprintf(notification_text, sizeof(notification_text), "Workspace %d", workspace);
    GooeyNotifications_Run(win, notification_text, NOTIFICATION_INFO, NOTIFICATION_POSITION_TOP_RIGHT);
}

void logout_callback(void *user_data)
{
    execute_system_command("loginctl terminal-user $USER");
}

void set_wallpaper(const char *filename)
{
    if (!filename || strlen(filename) == 0)
    {
        printf("No wallpaper file selected\n");
        return;
    }

    if (access(filename, F_OK) != 0)
    {
        printf("Wallpaper file does not exist: %s\n", filename);
        return;
    }

    strncpy(current_wallpaper_path, filename, sizeof(current_wallpaper_path) - 1);
    current_wallpaper_path[sizeof(current_wallpaper_path) - 1] = '\0';

    printf("Setting wallpaper to: %s\n", filename);

    if (wallpaper_dialog_visible && wallpaper_current_label)
    {
        char display_text[128];
        const char *basename = strrchr(filename, '/');
        if (basename)
        {
            snprintf(display_text, sizeof(display_text), "Current: %s", basename + 1);
        }
        else
        {
            snprintf(display_text, sizeof(display_text), "Current: %s", filename);
        }
        GooeyLabel_SetText(wallpaper_current_label, display_text);
    }
}

void wallpaper_dialog_callback(const char *filename)
{
    if (filename && strlen(filename) > 0)
    {
        printf("Wallpaper selected: %s\n", filename);
        strncpy(selected_wallpaper_path, filename, sizeof(selected_wallpaper_path) - 1);
        selected_wallpaper_path[sizeof(selected_wallpaper_path) - 1] = '\0';

        GooeyImage_Damage(wallpaper);
        GooeyImage_SetImage(wallpaper, filename);

        if (wallpaper_selected_label)
        {
            char display_text[128];
            const char *basename = strrchr(filename, '/');
            if (basename)
            {
                snprintf(display_text, sizeof(display_text), "Selected: %s", basename + 1);
            }
            else
            {
                snprintf(display_text, sizeof(display_text), "Selected: %s", filename);
            }
            GooeyLabel_SetText(wallpaper_selected_label, display_text);
        }

        set_wallpaper(filename);
    }
    else
    {
        printf("No wallpaper file selected\n");
    }
}

void wallpaper_browse_callback()
{
    printf("Opening file dialog for wallpaper selection\n");
    const char *filters[] = {
        "*.png",
        "*.jpg",
        "*.jpeg",
        "*.bmp",
        "*.gif"};
    int filter_count = 5;

    GooeyFDialog_Open(".", NULL, 0, wallpaper_dialog_callback);
}

void wallpaper_cancel_callback()
{
    printf("Closing wallpaper settings\n");
    set_wallpaper_dialog_visibility(0);
}

void create_wallpaper_dialog()
{
    printf("Creating wallpaper settings dialog\n");

    int dialog_width = 400;
    int dialog_height = 300;
    int dialog_x = (screen_info.width - dialog_width) / 2;
    int dialog_y = (screen_info.height - dialog_height) / 2;

    wallpaper_dialog_bg = GooeyImage_Create("/usr/local/share/gooeyde/assets/control_panel_bg.png",
                                            dialog_x,
                                            dialog_y,
                                            dialog_width,
                                            dialog_height,
                                            NULL,
                                            NULL);

    wallpaper_dialog_title = GooeyLabel_Create("Wallpaper Settings", 0.4f, dialog_x + 20, dialog_y + 30);
    GooeyLabel_SetColor(wallpaper_dialog_title, 0xFFFFFF);
    current_wallpaper_preview = GooeyImage_Create(current_wallpaper_path, dialog_x + 20, dialog_y + 50, 192, 108, NULL, NULL);

    char current_text[128];
    const char *basename = strrchr(current_wallpaper_path, '/');
    if (basename)
    {
        snprintf(current_text, sizeof(current_text), "Current: %s", basename + 1);
    }
    else
    {
        snprintf(current_text, sizeof(current_text), "Current: %s", current_wallpaper_path);
    }
    wallpaper_current_label = GooeyLabel_Create(current_text, 18.0f, dialog_x + 20, dialog_y + 190);
    GooeyLabel_SetColor(wallpaper_current_label, 0xCCCCCC);

    wallpaper_selected_label = GooeyLabel_Create("Selected: None", 18.0f, dialog_x + 20, dialog_y + 220);
    GooeyLabel_SetColor(wallpaper_selected_label, 0xCCCCCC);

    wallpaper_browse_button = GooeyButton_Create("Browse Wallpaper",
                                                 dialog_x + 20,
                                                 dialog_y + 240,
                                                 200,
                                                 40,
                                                 wallpaper_browse_callback,
                                                 NULL);

    wallpaper_cancel_button = GooeyButton_Create("Close",
                                                 dialog_x + dialog_width - 120,
                                                 dialog_y + dialog_height - 50,
                                                 100,
                                                 35,
                                                 wallpaper_cancel_callback,
                                                 NULL);

    GooeyWindow_RegisterWidget(win, wallpaper_dialog_bg);
    GooeyWindow_RegisterWidget(win, wallpaper_dialog_title);
    GooeyWindow_RegisterWidget(win, wallpaper_current_label);
    GooeyWindow_RegisterWidget(win, wallpaper_selected_label);
    GooeyWindow_RegisterWidget(win, wallpaper_browse_button);
    GooeyWindow_RegisterWidget(win, wallpaper_cancel_button);
    GooeyWindow_RegisterWidget(win, current_wallpaper_preview);
    wallpaper_dialog_visible = 1;
}

void destroy_wallpaper_dialog()
{
    printf("Destroying wallpaper settings dialog\n");
    set_wallpaper_dialog_visibility(0);
}

void set_wallpaper_dialog_visibility(int visible)
{
    wallpaper_dialog_visible = visible;

    if (wallpaper_dialog_bg)
    {
        GooeyWidget_MakeVisible(wallpaper_dialog_bg, visible);
        GooeyWidget_MakeVisible(wallpaper_dialog_title, visible);
        GooeyWidget_MakeVisible(wallpaper_current_label, visible);
        GooeyWidget_MakeVisible(wallpaper_selected_label, visible);
        GooeyWidget_MakeVisible(wallpaper_browse_button, visible);
        GooeyWidget_MakeVisible(wallpaper_cancel_button, visible);
        GooeyWidget_MakeVisible(current_wallpaper_preview, visible);
    }
}

void open_wallpaper_settings()
{
    printf("Opening wallpaper settings\n");

    if (!wallpaper_dialog_bg)
    {
        create_wallpaper_dialog();
    }
    else
    {
        set_wallpaper_dialog_visibility(1);
    }
}

int init_dbus_connection()
{
    DBusError error;
    dbus_error_init(&error);

    dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "DBus Connection Error: %s\n", error.message);
        dbus_error_free(&error);
        return 0;
    }

    if (!dbus_conn)
    {
        fprintf(stderr, "Failed to get DBus connection\n");
        return 0;
    }

    int ret = dbus_bus_request_name(dbus_conn, "dev.binaryink.gshell.desktop",
                                    DBUS_NAME_FLAG_REPLACE_EXISTING, &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "DBus Name Error: %s\n", error.message);
        dbus_error_free(&error);
        dbus_connection_close(dbus_conn);
        dbus_conn = NULL;
        return 0;
    }

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        fprintf(stderr, "Not primary owner of DBus name\n");
        dbus_connection_close(dbus_conn);
        dbus_conn = NULL;
        return 0;
    }

    char match_rule[256];
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='%s',path='%s'",
             DBUS_INTERFACE, DBUS_PATH);

    dbus_bus_add_match(dbus_conn, match_rule, &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "DBus Match Error: %s\n", error.message);
        dbus_error_free(&error);
        dbus_connection_close(dbus_conn);
        dbus_conn = NULL;
        return 0;
    }

    dbus_initialized = 1;
    printf("DBus initialized successfully\n");
    return 1;
}

void cleanup_dbus()
{
    if (dbus_conn)
    {
        dbus_connection_close(dbus_conn);
        dbus_conn = NULL;
    }
    dbus_initialized = 0;
}

void *dbus_monitor_thread(void *arg)
{
    printf("DBus monitor thread started\n");

    while (dbus_thread_running && dbus_initialized)
    {
        if (!dbus_connection_read_write(dbus_conn, 50))
        {
            usleep(50000);
            continue;
        }

        DBusMessage *message;
        while ((message = dbus_connection_pop_message(dbus_conn)) != NULL)
        {
            process_dbus_message(message);
            dbus_message_unref(message);
        }

        usleep(50000);
    }

    printf("DBus monitor thread stopped\n");
    return NULL;
}

void process_dbus_message(DBusMessage *message)
{
    if (!message)
        return;

    const char *interface = dbus_message_get_interface(message);
    const char *member = dbus_message_get_member(message);

    if (!interface || !member)
        return;

    if (strcmp(interface, DBUS_INTERFACE) == 0)
    {
        if (strcmp(member, "WindowListUpdated") == 0)
        {

        }
        else if (strcmp(member, "WindowStateChanged") == 0)
        {

        }
        else if (strcmp(member, "WorkspaceChanged") == 0)
        {
            handle_workspace_changed(message);
        }
    }
}

void handle_workspace_changed(DBusMessage *message)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter))
    {
        fprintf(stderr, "No arguments in WorkspaceChanged signal\n");
        return;
    }

    int old_workspace = 1;
    int new_workspace = 1;

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic(&iter, &old_workspace);
        dbus_message_iter_next(&iter);
    }

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic(&iter, &new_workspace);
    }

    printf("Workspace changed: %d -> %d\n", old_workspace, new_workspace);

    current_workspace = new_workspace;
    update_workspace_indicator(new_workspace);
}

void init_system_settings()
{
    current_brightness = 80;
    current_volume = 75;
    battery_level = 85;
    strcpy(network_status, "Connected");

    current_brightness = get_system_brightness();
    int max_bright = get_max_brightness();
    if (max_bright > 0)
    {
        current_brightness = (current_brightness * 100) / max_bright;
    }

    current_volume = get_system_volume();
    battery_level = get_battery_level();
    get_network_status(network_status);

    printf("System settings initialized:\n");
    printf("  Brightness: %d%%\n", current_brightness);
    printf("  Volume: %d%%\n", current_volume);
    printf("  Battery: %d%%\n", battery_level);
    printf("  Network: %s\n", network_status);
}

void desktop_clicked(void *user_data)
{

}

void update_status_icons()
{
    glps_thread_mutex_lock(&ui_update_mutex);

    int wifi_state = get_system_wifi_state();
    if (wifi_status_icon)
    {
        const char *wifi_icon = wifi_state ? "/usr/local/share/gooeyde/assets/wifi_on.png" : "/usr/local/share/gooeyde/assets/wifi_off.png";
        GooeyImage_SetImage(wifi_status_icon, wifi_icon);
    }

    if (volume_status_icon)
    {
        const char *volume_icon;
        if (current_volume == 0)
        {
            volume_icon = "/usr/local/share/gooeyde/assets/volume_mute.png";
        }
        else if (current_volume > 0 && current_volume <= 33)
        {
            volume_icon = "/usr/local/share/gooeyde/assets/volume_medium.png";
        }
        else if (current_volume > 33 && current_volume < 66)
        {
            volume_icon = "/usr/local/share/gooeyde/assets/volume_medium.png";
        }
        else
        {
            volume_icon = "/usr/local/share/gooeyde/assets/volume_high.png";
        }
        GooeyImage_SetImage(volume_status_icon, volume_icon);
    }

    if (battery_status_icon && battery_percent_label)
    {
        const char *battery_icon;
        if (battery_level >= 90)
        {
            battery_icon = "/usr/local/share/gooeyde/assets/battery_full.png";
        }
        else if (battery_level >= 60)
        {
            battery_icon = "/usr/local/share/gooeyde/assets/battery_high.png";
        }
        else if (battery_level >= 30)
        {
            battery_icon = "/usr/local/share/gooeyde/assets/battery_medium.png";
        }
        else if (battery_level >= 10)
        {
            battery_icon = "/usr/local/share/gooeyde/assets/battery_low.png";
        }
        else
        {
            battery_icon = "/usr/local/share/gooeyde/assets/battery_critical.png";
        }
        GooeyImage_SetImage(battery_status_icon, battery_icon);

        char battery_text[8];
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery_level);
        GooeyLabel_SetText(battery_percent_label, battery_text);
    }

    glps_thread_mutex_unlock(&ui_update_mutex);
}

void run_app_menu()
{
    if (fork() == 0)
    {
        execl("/usr/local/bin/gooeyde_appmenu", "/usr/local/bin/gooeyde_appmenu", NULL);
        perror("Failed to launch app menu");
        exit(1);
    }
}

void run_systemsettings()
{
    if (fork() == 0)
    {
        execl("/usr/local/bin/gooeyde_systemsettings", "/usr/local/bin/gooeyde_systemsettings", NULL);
        perror("Failed to launch system settings");
        exit(1);
    }
}

void update_time_date()
{
    time_t raw_time;
    struct tm *time_info;
    char time_buffer[64];
    char date_buffer[64];

    time(&raw_time);
    time_info = localtime(&raw_time);

    strftime(time_buffer, sizeof(time_buffer), "%I:%M:%S %p", time_info);
    strftime(date_buffer, sizeof(date_buffer), "%A, %B %d", time_info);

    glps_thread_mutex_lock(&ui_update_mutex);
    if (time_label)
        GooeyLabel_SetText(time_label, time_buffer);
    if (date_label)
        GooeyLabel_SetText(date_label, date_buffer);
    glps_thread_mutex_unlock(&ui_update_mutex);
}

void *time_update_thread(void *arg)
{
    printf("Time update thread started\n");

    while (time_thread_running)
    {
        update_time_date();

        static int counter = 0;
        if (counter++ >= 5)
        {
            refresh_system_info();
            counter = 0;
        }
        sleep(1);
    }

    printf("Time update thread stopped\n");
    return NULL;
}

void toggle_control_panel()
{
    printf("clicked \n");
    control_panel_visible = !control_panel_visible;

    if (control_panel_visible)
    {
        if (!control_panel_bg)
        {
            create_control_panel();
        }
        else
        {
            set_control_panel_visibility(1);
        }

        refresh_system_info();
    }
    else
    {
        set_control_panel_visibility(0);
    }
}

void create_control_panel()
{
    printf("Creating control panel\n");

    init_system_settings();

    int panel_width = 363;
    int panel_height = 600;
    int panel_x = screen_info.width - panel_width - 10;
    int panel_y = 60;

    control_panel_bg = GooeyImage_Create("/usr/local/share/gooeyde/assets/control_panel_bg.png", panel_x, panel_y, panel_width, panel_height, desktop_clicked, NULL);

    panel_title = GooeyLabel_Create("Control Panel", 18.0f, panel_x + 20, panel_y + 45);
    GooeyLabel_SetColor(panel_title, 0xFFFFFF);
    logout_button = GooeyButton_Create("Logout", panel_x + panel_width - 110, panel_y + 20, 90, 30, logout_callback, NULL);

    int current_y = panel_y + 90;
    int label_x = panel_x + 20;
    int switch_x = panel_x + panel_width - 120;
    int slider_width = 160;
    int slider_x = panel_x + panel_width - slider_width - 60;

    wifi_label = GooeyLabel_Create("WiFi", 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(wifi_label, 0xFFFFFF);
    wifi_switch = GooeySwitch_Create(switch_x, current_y, get_system_wifi_state(), false, wifi_switch_callback, NULL);

    current_y += 50;
    bluetooth_label = GooeyLabel_Create("Bluetooth", 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(bluetooth_label, 0xFFFFFF);
    bluetooth_switch = GooeySwitch_Create(switch_x, current_y, false, false, bluetooth_switch_callback, NULL);

    current_y += 50;
    airplane_label = GooeyLabel_Create("Airplane Mode", 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(airplane_label, 0xFFFFFF);
    airplane_switch = GooeySwitch_Create(switch_x, current_y, false, false, airplane_switch_callback, NULL);

    current_y += 70;
    settings_title = GooeyLabel_Create("Quick Settings", 0.35f, label_x, current_y);
    GooeyLabel_SetColor(settings_title, 0xAAAAAA);

    current_y += 40;
    volume_label = GooeyLabel_Create("Volume", 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(volume_label, 0xCCCCCC);
    volume_slider = GooeySlider_Create(slider_x, current_y, slider_width, 0, 100, false, volume_slider_callback, NULL);
    volume_slider->value = current_volume;
    char volume_text[32];
    snprintf(volume_text, sizeof(volume_text), "%d%%", current_volume);
    volume_value_label = GooeyLabel_Create(volume_text, 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(volume_value_label, 0xCCCCCC);

    current_y += 50;
    brightness_label = GooeyLabel_Create("Brightness", 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(brightness_label, 0xCCCCCC);
    brightness_slider = GooeySlider_Create(slider_x, current_y, slider_width, 0, 100, false, brightness_slider_callback, NULL);
    brightness_slider->value = current_brightness;
    char brightness_text[32];
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_brightness);
    brightness_value_label = GooeyLabel_Create(brightness_text, 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(brightness_value_label, 0xCCCCCC);

    current_y += 70;
    system_title = GooeyLabel_Create("System Info", 0.35f, label_x, current_y);
    GooeyLabel_SetColor(system_title, 0xAAAAAA);

    current_y += 25;
    char battery_text[32];
    snprintf(battery_text, sizeof(battery_text), "Battery: %d%%", battery_level);
    battery_label = GooeyLabel_Create(battery_text, 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(battery_label, 0xCCCCCC);

    current_y += 30;
    network_label = GooeyLabel_Create(network_status, 18.0f, label_x, current_y + 8);
    GooeyLabel_SetColor(network_label, 0xCCCCCC);

    current_y += 25;
    unsupported_devices_label = GooeyLabel_Create("Some devices may not be supported on this system", 0.26f, label_x, current_y + 8);
    GooeyLabel_SetColor(unsupported_devices_label, 0x888888);

    current_y += 25;
    settings_label = GooeyLabel_Create("Visit settings:", 18.0f, label_x, current_y + 18);
    GooeyLabel_SetColor(settings_label, 0xCCCCCC);
    settings_button = GooeyButton_Create("Open", slider_x, current_y, 100, 30, run_systemsettings, NULL);

    GooeyWindow_RegisterWidget(win, settings_button);
    GooeyWindow_RegisterWidget(win, settings_label);
    GooeyWindow_RegisterWidget(win, control_panel_bg);
    GooeyWindow_RegisterWidget(win, panel_title);
    GooeyWindow_RegisterWidget(win, wifi_label);
    GooeyWindow_RegisterWidget(win, wifi_switch);
    GooeyWindow_RegisterWidget(win, bluetooth_label);
    GooeyWindow_RegisterWidget(win, bluetooth_switch);
    GooeyWindow_RegisterWidget(win, airplane_label);
    GooeyWindow_RegisterWidget(win, airplane_switch);
    GooeyWindow_RegisterWidget(win, logout_button);
    GooeyWindow_RegisterWidget(win, settings_title);
    GooeyWindow_RegisterWidget(win, volume_label);
    GooeyWindow_RegisterWidget(win, volume_slider);
    GooeyWindow_RegisterWidget(win, volume_value_label);
    GooeyWindow_RegisterWidget(win, brightness_label);
    GooeyWindow_RegisterWidget(win, brightness_slider);
    GooeyWindow_RegisterWidget(win, brightness_value_label);
    GooeyWindow_RegisterWidget(win, system_title);
    GooeyWindow_RegisterWidget(win, battery_label);
    GooeyWindow_RegisterWidget(win, network_label);
    GooeyWindow_RegisterWidget(win, unsupported_devices_label);
}

void destroy_control_panel()
{
    printf("Destroying control panel\n");
    set_control_panel_visibility(0);
}

void set_control_panel_visibility(int visible)
{
    if (!control_panel_bg)
        return;

    glps_thread_mutex_lock(&ui_update_mutex);
    GooeyWidget_MakeVisible(logout_button, visible);
    GooeyWidget_MakeVisible(control_panel_bg, visible);
    GooeyWidget_MakeVisible(panel_title, visible);
    GooeyWidget_MakeVisible(wifi_label, visible);
    GooeyWidget_MakeVisible(wifi_switch, visible);
    GooeyWidget_MakeVisible(bluetooth_label, visible);
    GooeyWidget_MakeVisible(bluetooth_switch, visible);
    GooeyWidget_MakeVisible(airplane_label, visible);
    GooeyWidget_MakeVisible(airplane_switch, visible);
    GooeyWidget_MakeVisible(settings_button, visible);
    GooeyWidget_MakeVisible(settings_label, visible);
    GooeyWidget_MakeVisible(settings_title, visible);
    GooeyWidget_MakeVisible(volume_label, visible);
    GooeyWidget_MakeVisible(volume_slider, visible);
    GooeyWidget_MakeVisible(volume_value_label, visible);
    GooeyWidget_MakeVisible(brightness_label, visible);
    GooeyWidget_MakeVisible(brightness_slider, visible);
    GooeyWidget_MakeVisible(brightness_value_label, visible);
    GooeyWidget_MakeVisible(system_title, visible);
    GooeyWidget_MakeVisible(battery_label, visible);
    GooeyWidget_MakeVisible(network_label, visible);
    GooeyWidget_MakeVisible(unsupported_devices_label, visible);

    glps_thread_mutex_unlock(&ui_update_mutex);
}

void wifi_switch_callback(bool value, void *user_data)
{
    enable_wifi(value);
    printf("WiFi %s\n", value ? "Enabled" : "Disabled");

    get_network_status(network_status);

    glps_thread_mutex_lock(&ui_update_mutex);
    if (network_label)
    {
        GooeyLabel_SetText(network_label, network_status);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);

    update_status_icons();
}

void bluetooth_switch_callback(bool value, void *user_data)
{
    enable_bluetooth(value);
    update_status_icons();
}

void airplane_switch_callback(bool value, void *user_data)
{
    printf("Airplane Mode %s\n", value ? "Enabled" : "Disabled");

    if (value)
    {
        if (wifi_switch && GooeySwitch_GetState(wifi_switch))
        {
            wifi_switch_callback(false, NULL);
            GooeySwitch_Toggle(wifi_switch);
        }
        if (bluetooth_switch && GooeySwitch_GetState(bluetooth_switch))
        {
            bluetooth_switch_callback(false, NULL);
            GooeySwitch_Toggle(bluetooth_switch);
        }
    }
    else
    {
        if (wifi_switch && !GooeySwitch_GetState(wifi_switch))
        {
            wifi_switch_callback(true, NULL);
            GooeySwitch_Toggle(wifi_switch);
        }
    }
    update_status_icons();
}

void volume_slider_callback(long value, void *user_data)
{
    change_system_volume(value);
    current_volume = value;
    glps_thread_mutex_lock(&ui_update_mutex);
    if (volume_value_label)
    {
        char volume_text[32];
        snprintf(volume_text, sizeof(volume_text), "%ld%%", value);
        GooeyLabel_SetText(volume_value_label, volume_text);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);

    update_status_icons();
}

void brightness_slider_callback(long value, void *user_data)
{
    change_display_brightness(value);
    current_brightness = value;
    int max_bright = get_max_brightness();
    int actual_level = (value * max_bright) / 100;

    glps_thread_mutex_lock(&ui_update_mutex);
    if (brightness_value_label)
    {
        char brightness_text[32];
        snprintf(brightness_text, sizeof(brightness_text), "%ld%%", value);
        GooeyLabel_SetText(brightness_value_label, brightness_text);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);
}

void mute_audio_callback()
{
    mute_audio();
    update_status_icons();
}

void refresh_system_info()
{
    printf("Refreshing system information...\n");

    battery_level = get_battery_level();

    glps_thread_mutex_lock(&ui_update_mutex);
    if (battery_label)
    {
        char battery_text[32];
        snprintf(battery_text, sizeof(battery_text), "Battery: %d%%", battery_level);
        GooeyLabel_SetText(battery_label, battery_text);
    }

    get_network_status(network_status);
    if (network_label)
    {
        GooeyLabel_SetText(network_label, network_status);
    }

    int system_volume = get_system_volume();
    if (system_volume != current_volume && volume_slider)
    {
        current_volume = system_volume;
        volume_slider->value = system_volume;
        if (volume_value_label)
        {
            char volume_text[32];
            snprintf(volume_text, sizeof(volume_text), "%d%%", system_volume);
            GooeyLabel_SetText(volume_value_label, volume_text);
        }
    }

    int system_brightness = get_system_brightness();
    int max_bright = get_max_brightness();
    int brightness_percent = (system_brightness * 100) / max_bright;
    if (brightness_percent != current_brightness && brightness_slider)
    {
        current_brightness = brightness_percent;
        brightness_slider->value = brightness_percent;
        if (brightness_value_label)
        {
            char brightness_text[32];
            snprintf(brightness_text, sizeof(brightness_text), "%d%%", brightness_percent);
            GooeyLabel_SetText(brightness_value_label, brightness_text);
        }
    }
    glps_thread_mutex_unlock(&ui_update_mutex);

    printf("System info refreshed\n");
}

void create_status_icons()
{
    int icon_size = 24;
    int icon_y = 13;
    int start_x = screen_info.width - 400;

    wifi_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/wifi_on.png", start_x, icon_y, icon_size, icon_size, NULL, NULL);

    bluetooth_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/bluetooth_on.png", start_x + 40, icon_y, icon_size, icon_size, NULL, NULL);

    volume_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/volume_high.png", start_x + 80, icon_y, icon_size, icon_size, mute_audio_callback, NULL);

    battery_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/battery_full.png", start_x + 120, icon_y, icon_size, icon_size, NULL, NULL);
    battery_percent_label = GooeyLabel_Create("85%", 0.26f, start_x + 148, icon_y + 5);
    GooeyLabel_SetColor(battery_percent_label, 0xFFFFFF);

    GooeyWindow_RegisterWidget(win, wifi_status_icon);
    GooeyWindow_RegisterWidget(win, bluetooth_status_icon);
    GooeyWindow_RegisterWidget(win, volume_status_icon);
    GooeyWindow_RegisterWidget(win, battery_status_icon);
    GooeyWindow_RegisterWidget(win, battery_percent_label);
}

int main(int argc, char **argv)
{
    glps_thread_mutex_init(&ui_update_mutex, NULL);

    Gooey_Init();
    screen_info = get_screen_resolution();
    if (screen_info.width == 0 || screen_info.height == 0)
    {
        fprintf(stderr, "Failed to get screen resolution, using default 1024x768\n");
        screen_info.width = 1024;
        screen_info.height = 768;
    }

    win = GooeyWindow_Create("Gooey Desktop", screen_info.width, screen_info.height, true);
    GooeyNotifications_Run(win, "Welcome to GooeyDE", NOTIFICATION_INFO, NOTIFICATION_POSITION_TOP_RIGHT);

    wallpaper = GooeyImage_Create("/usr/local/share/gooeyde/assets/bg.png", 0, 50, screen_info.width, screen_info.height - 50, NULL, NULL);
    GooeyCanvas *canvas = GooeyCanvas_Create(0, 0, screen_info.width, 50, NULL, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, screen_info.width, 50, 0x222222, true, 1.0f, true, 1.0f);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);

    workspace_indicator = GooeyLabel_Create("Workspace 1", 18.0f, screen_info.width / 2 - 60, 30);
    GooeyLabel_SetColor(workspace_indicator, 0xFFFFFF);

    GooeyImage *apps_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/apps.png", 10, 10, 30, 30, run_app_menu, NULL);
    GooeyImage *settings_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/settings.png", screen_info.width - 210, 10, 30, 30, toggle_control_panel, NULL);
    time_label = GooeyLabel_Create("Loading...", 18.0f, screen_info.width - 160, 23);
    GooeyLabel_SetColor(time_label, 0xFFFFFF);
    date_label = GooeyLabel_Create("Loading...", 12.0f, screen_info.width - 160, 40);
    GooeyLabel_SetColor(date_label, 0xCCCCCC);

    create_status_icons();
    GooeyWindow_MakeResizable(win, false);
    GooeyWindow_RegisterWidget(win, canvas);
    GooeyWindow_RegisterWidget(win, wallpaper);
    GooeyWindow_RegisterWidget(win, workspace_indicator);
    GooeyWindow_RegisterWidget(win, apps_icon);
    GooeyWindow_RegisterWidget(win, settings_icon);
    GooeyWindow_RegisterWidget(win, time_label);
    GooeyWindow_RegisterWidget(win, date_label);

    init_system_settings();
    update_status_icons();
    update_time_date();

    gthread_t time_thread;
    if (glps_thread_create(&time_thread, NULL, time_update_thread, NULL) != 0)
    {
        fprintf(stderr, "Failed to create time update thread\n");
    }
    else
    {
        printf("Time update thread created successfully\n");
    }

    gthread_t dbus_thread;
    if (init_dbus_connection())
    {
        if (glps_thread_create(&dbus_thread, NULL, dbus_monitor_thread, NULL) != 0)
        {
            fprintf(stderr, "Failed to create DBus monitor thread\n");
        }
        else
        {
            printf("DBus monitor thread created successfully\n");
        }
    }
    else
    {
        fprintf(stderr, "DBus initialization failed\n");
    }

    printf("Desktop started successfully\n");
    printf("Screen resolution: %dx%d\n", screen_info.width, screen_info.height);
    printf("Click the settings icon to toggle control panel visibility\n");
    printf("Dock has been removed - use Alt+Tab or window management shortcuts\n");

    GooeyWindow_Run(1, win);

    printf("Stopping threads...\n");
    time_thread_running = 0;
    dbus_thread_running = 0;

    glps_thread_join(time_thread, NULL);
    if (dbus_initialized)
    {
        glps_thread_join(dbus_thread, NULL);
    }

    cleanup_dbus();

    glps_thread_mutex_destroy(&ui_update_mutex);

    GooeyWindow_Cleanup(1, win);
    printf("Desktop shutdown complete\n");
    return 0;
}