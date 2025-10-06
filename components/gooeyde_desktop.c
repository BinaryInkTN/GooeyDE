#include "gooey.h"
#include "utils/resolution_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <GLPS/glps_thread.h>

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

GooeyImage *wifi_status_icon = NULL;
GooeyImage *bluetooth_status_icon = NULL;
GooeyImage *volume_status_icon = NULL;
GooeyImage *battery_status_icon = NULL;
GooeyLabel *battery_percent_label = NULL;

int current_volume = 75;
int current_brightness = 80;
int battery_level = 85;
char network_status[64] = "Connected";

void execute_system_command(const char *command)
{
    system(command);
}

int get_system_brightness()
{
    FILE *fp = popen("cat /sys/class/backlight/*/brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        int brightness = 80;
        fscanf(fp, "%d", &brightness);
        pclose(fp);
        return brightness;
    }
    return 80;
}

int get_max_brightness()
{
    FILE *fp = popen("cat /sys/class/backlight/*/max_brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        int max_brightness = 100;
        fscanf(fp, "%d", &max_brightness);
        pclose(fp);
        return max_brightness;
    }
    return 100;
}

int get_system_volume()
{
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+(?=%)' | head -1", "r");
    if (fp)
    {
        int volume = 75;
        fscanf(fp, "%d", &volume);
        pclose(fp);
        return volume;
    }
    return 75;
}

int get_system_wifi_state()
{
    FILE *fp = popen("nmcli radio wifi 2>/dev/null", "r");
    if (fp)
    {
        char state[16];
        if (fgets(state, sizeof(state), fp))
        {
            pclose(fp);
            return strstr(state, "enabled") != NULL;
        }
        pclose(fp);
    }
    return 1;
}

int get_system_bluetooth_state()
{
    FILE *fp = popen("bluetoothctl show 2>/dev/null | grep -q 'Powered: yes' && echo enabled", "r");
    if (fp)
    {
        char state[16];
        if (fgets(state, sizeof(state), fp))
        {
            pclose(fp);
            return strstr(state, "enabled") != NULL;
        }
        pclose(fp);
    }
    return 0;
}

int get_battery_level()
{
    FILE *fp = popen("cat /sys/class/power_supply/BAT*/capacity 2>/dev/null | head -1", "r");
    if (fp)
    {
        int level = 85;
        fscanf(fp, "%d", &level);
        pclose(fp);
        return level;
    }
    return 85;
}

void get_network_status()
{
    FILE *fp = popen("nmcli -t -f NAME connection show --active 2>/dev/null | head -1", "r");
    if (fp)
    {
        char network[128];
        if (fgets(network, sizeof(network), fp))
        {
            network[strcspn(network, "\n")] = 0;
            if (strlen(network) > 0)
            {
                snprintf(network_status, sizeof(network_status), "Connected: %s", network);
            }
            else
            {
                strcpy(network_status, "Not connected");
            }
        }
        else
        {
            strcpy(network_status, "Not connected");
        }
        pclose(fp);
    }
    else
    {
        strcpy(network_status, "Not connected");
    }
}

void init_system_settings()
{

    int current_bright = get_system_brightness();
    int max_bright = get_max_brightness();
    current_brightness = (current_bright * 100) / max_bright;

    current_volume = get_system_volume();

    int wifi_state = get_system_wifi_state();

    int bluetooth_state = get_system_bluetooth_state();

    battery_level = get_battery_level();

    get_network_status();

    printf("System settings initialized:\n");
    printf("  Brightness: %d%%\n", current_brightness);
    printf("  Volume: %d%%\n", current_volume);
    printf("  WiFi: %s\n", wifi_state ? "Enabled" : "Disabled");
    printf("  Bluetooth: %s\n", bluetooth_state ? "Enabled" : "Disabled");
    printf("  Battery: %d%%\n", battery_level);
    printf("  Network: %s\n", network_status);
}

void update_status_icons()
{

    int wifi_state = get_system_wifi_state();
    if (wifi_status_icon)
    {
        const char *wifi_icon = wifi_state ? "assets/wifi_on.png" : "assets/wifi_off.png";

        GooeyImage_SetImage(wifi_status_icon, wifi_icon);
    }

    int bluetooth_state = get_system_bluetooth_state();
    if (bluetooth_status_icon)
    {
        const char *bluetooth_icon = bluetooth_state ? "assets/bluetooth_on.png" : NULL;
        if (bluetooth_icon == NULL)
        {
            GooeyWidget_MakeVisible(bluetooth_status_icon, false);
        }
        else
        {
            GooeyWidget_MakeVisible(bluetooth_status_icon, true);

            GooeyImage_SetImage(bluetooth_status_icon, bluetooth_icon);
        }
    }

    if (volume_status_icon)
    {
        const char *volume_icon;
        if (current_volume == 0)
        {
            volume_icon = "assets/volume_mute.png";
        }
        else if (current_volume > 0 && current_volume <= 33)
        {
            volume_icon = "assets/volume_medium.png";
        }
        else if (current_volume > 33 && current_volume < 66)
        {
            volume_icon = "assets/volume_medium.png";
        }
        else
        {
            volume_icon = "assets/volume_high.png";
        }
        GooeyImage_SetImage(volume_status_icon, volume_icon);
    }

    if (battery_status_icon && battery_percent_label)
    {
        const char *battery_icon;
        if (battery_level >= 90)
        {
            battery_icon = "assets/battery_full.png";
        }
        else if (battery_level >= 60)
        {
            battery_icon = "assets/battery_high.png";
        }
        else if (battery_level >= 30)
        {
            battery_icon = "assets/battery_medium.png";
        }
        else if (battery_level >= 10)
        {
            battery_icon = "assets/battery_low.png";
        }
        else
        {
            battery_icon = "assets/battery_critical.png";
        }
        GooeyImage_SetImage(battery_status_icon, battery_icon);

        char battery_text[8];
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery_level);
        GooeyLabel_SetText(battery_percent_label, battery_text);
    }
}

void run_app_menu();
void update_time_date();
void *time_update_thread(void *arg);
void toggle_control_panel();
void create_control_panel();
void destroy_control_panel();
void set_control_panel_visibility(int visible);

void wifi_switch_callback(bool value)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "nmcli radio wifi %s", value ? "on" : "off");
    execute_system_command(cmd);
    printf("WiFi %s\n", value ? "Enabled" : "Disabled");

    get_network_status();
    if (network_label)
    {
        GooeyLabel_SetText(network_label, network_status);
    }
    update_status_icons();
}

void bluetooth_switch_callback(bool value)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "bluetoothctl power %s", value ? "on" : "off");
    execute_system_command(cmd);
    printf("Bluetooth %s\n", value ? "Enabled" : "Disabled");
    update_status_icons();
}

void airplane_switch_callback(bool value)
{
    printf("Airplane Mode %s\n", value ? "Enabled" : "Disabled");

    if (value)
    {

        if (wifi_switch && GooeySwitch_GetState(wifi_switch))
        {
            wifi_switch_callback(false);
            GooeySwitch_Toggle(wifi_switch);
        }
        if (bluetooth_switch && GooeySwitch_GetState(bluetooth_switch))
        {
            bluetooth_switch_callback(false);
            GooeySwitch_Toggle(bluetooth_switch);
        }
    }
    else
    {

        if (wifi_switch && !GooeySwitch_GetState(wifi_switch))
        {
            wifi_switch_callback(true);
            GooeySwitch_Toggle(wifi_switch);
        }
    }
    update_status_icons();
}

void volume_slider_callback(long value)
{
    current_volume = value;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %ld%%", value);
    execute_system_command(cmd);
    printf("Volume set to: %ld%%\n", value);

    if (volume_value_label)
    {
        char volume_text[32];
        snprintf(volume_text, sizeof(volume_text), "%ld%%", value);
        GooeyLabel_SetText(volume_value_label, volume_text);
    }
    update_status_icons();
}

void brightness_slider_callback(long value)
{
    current_brightness = value;
    int max_bright = get_max_brightness();
    int actual_level = (value * max_bright) / 100;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/class/backlight/*/brightness >/dev/null 2>&1", actual_level);
    execute_system_command(cmd);
    printf("Brightness set to: %ld%%\n", value);

    if (brightness_value_label)
    {
        char brightness_text[32];
        snprintf(brightness_text, sizeof(brightness_text), "%ld%%", value);
        GooeyLabel_SetText(brightness_value_label, brightness_text);
    }
}

void mute_audio_callback()
{
    execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    printf("Audio mute toggled\n");
    update_status_icons();
}

void refresh_system_info()
{
    printf("Refreshing system information...\n");

    battery_level = get_battery_level();
    if (battery_label)
    {
        char battery_text[32];
        snprintf(battery_text, sizeof(battery_text), "Battery: %d%%", battery_level);
        GooeyLabel_SetText(battery_label, battery_text);
    }

    get_network_status();
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

    printf("System info refreshed\n");
}

void run_app_menu()
{
    if (fork() == 0)
    {
        execl("./gooeyde_appmenu", "./gooeyde_appmenu", NULL);
        perror("Failed to launch app menu");
        exit(1);
    }
}

void set_control_panel_visibility(int visible)
{
    if (!control_panel_bg)
        return;

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
}

void toggle_control_panel()
{
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

    control_panel_bg = GooeyImage_Create("assets/control_panel_bg.png", panel_x, panel_y, panel_width, panel_height, NULL);

    panel_title = GooeyLabel_Create("Control Panel", 0.45f, panel_x + 20, panel_y + 45);
    GooeyLabel_SetColor(panel_title, 0xFFFFFF);

    int current_y = panel_y + 90;
    int label_x = panel_x + 20;
    int switch_x = panel_x + panel_width - 120;
    int slider_width = 160;
    int slider_x = panel_x + panel_width - slider_width - 60;

    wifi_label = GooeyLabel_Create("WiFi", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(wifi_label, 0xFFFFFF);
    wifi_switch = GooeySwitch_Create(switch_x, current_y, get_system_wifi_state(), false, wifi_switch_callback);

    current_y += 50;
    bluetooth_label = GooeyLabel_Create("Bluetooth", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(bluetooth_label, 0xFFFFFF);
    bluetooth_switch = GooeySwitch_Create(switch_x, current_y, get_system_bluetooth_state(), false, bluetooth_switch_callback);

    current_y += 50;
    airplane_label = GooeyLabel_Create("Airplane Mode", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(airplane_label, 0xFFFFFF);
    airplane_switch = GooeySwitch_Create(switch_x, current_y, false, false, airplane_switch_callback);

    current_y += 70;
    settings_title = GooeyLabel_Create("Quick Settings", 0.35f, label_x, current_y);
    GooeyLabel_SetColor(settings_title, 0xAAAAAA);

    current_y += 40;
    volume_label = GooeyLabel_Create("Volume", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(volume_label, 0xCCCCCC);
    volume_slider = GooeySlider_Create(slider_x, current_y, slider_width, 0, 100, false, volume_slider_callback);
    volume_slider->value = current_volume;
    char volume_text[32];
    snprintf(volume_text, sizeof(volume_text), "%d%%", current_volume);
    volume_value_label = GooeyLabel_Create(volume_text, 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(volume_value_label, 0xCCCCCC);

    current_y += 50;
    brightness_label = GooeyLabel_Create("Brightness", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(brightness_label, 0xCCCCCC);
    brightness_slider = GooeySlider_Create(slider_x, current_y, slider_width, 0, 100, false, brightness_slider_callback);
    brightness_slider->value = current_brightness;
    char brightness_text[32];
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_brightness);
    brightness_value_label = GooeyLabel_Create(brightness_text, 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(brightness_value_label, 0xCCCCCC);

    current_y += 70;
    system_title = GooeyLabel_Create("System Info", 0.35f, label_x, current_y);
    GooeyLabel_SetColor(system_title, 0xAAAAAA);

    current_y += 40;
    char battery_text[32];
    snprintf(battery_text, sizeof(battery_text), "Battery: %d%%", battery_level);
    battery_label = GooeyLabel_Create(battery_text, 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(battery_label, 0xCCCCCC);

    current_y += 30;
    network_label = GooeyLabel_Create(network_status, 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(network_label, 0xCCCCCC);

    current_y += 50;
    settings_label = GooeyLabel_Create("Visit settings:", 0.3f, label_x, current_y + 18);
    GooeyLabel_SetColor(settings_label, 0xCCCCCC);
    settings_button = GooeyButton_Create("Refresh", slider_x, current_y, 100, 30, refresh_system_info);

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
}

void destroy_control_panel()
{
    printf("Destroying control panel\n");
    set_control_panel_visibility(0);
}

void create_status_icons()
{
    int icon_size = 24;
    int icon_y = 13;
    int start_x = screen_info.width - 400;

    wifi_status_icon = GooeyImage_Create("assets/wifi_on.png", start_x, icon_y, icon_size, icon_size, NULL);

    bluetooth_status_icon = GooeyImage_Create("assets/bluetooth_on.png", start_x + 40, icon_y, icon_size, icon_size, NULL);

    volume_status_icon = GooeyImage_Create("assets/volume_high.png", start_x + 80, icon_y, icon_size, icon_size, mute_audio_callback);

    battery_status_icon = GooeyImage_Create("assets/battery_full.png", start_x + 120, icon_y, icon_size, icon_size, NULL);
    battery_percent_label = GooeyLabel_Create("85%", 0.26f, start_x + 148, icon_y + 5);
    GooeyLabel_SetColor(battery_percent_label, 0xFFFFFF);

    GooeyWindow_RegisterWidget(win, wifi_status_icon);
    GooeyWindow_RegisterWidget(win, bluetooth_status_icon);
    GooeyWindow_RegisterWidget(win, volume_status_icon);
    GooeyWindow_RegisterWidget(win, battery_status_icon);
    GooeyWindow_RegisterWidget(win, battery_percent_label);
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

    if (time_label)
        GooeyLabel_SetText(time_label, time_buffer);
    if (date_label)
        GooeyLabel_SetText(date_label, date_buffer);
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

int main(int argc, char **argv)
{
    Gooey_Init();
    screen_info = get_screen_resolution();
    if (screen_info.width == 0 || screen_info.height == 0)
    {
        fprintf(stderr, "Failed to get screen resolution, using default 1024x768\n");
        screen_info.width = 1024;
        screen_info.height = 768;
    }

    win = GooeyWindow_Create("Gooey Desktop", screen_info.width, screen_info.height, true);

    GooeyImage *wallpaper = GooeyImage_Create("assets/bg.png", 0, 50, screen_info.width, screen_info.height - 50, NULL);
    GooeyCanvas *canvas = GooeyCanvas_Create(0, 0, screen_info.width, screen_info.height, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, screen_info.width, 50, 0x222222, true, 1.0f, true, 1.0f);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);

    GooeyImage *apps_icon = GooeyImage_Create("assets/apps.png", 10, 10, 30, 30, run_app_menu);
    GooeyImage *settings_icon = GooeyImage_Create("assets/settings.png", screen_info.width - 210, 10, 30, 30, toggle_control_panel);
    GooeyImage *test_app = GooeyImage_Create("assets/test_app.png", 65, 10, 30, 30, NULL);

    time_label = GooeyLabel_Create("Loading...", 0.3f, screen_info.width - 150, 18);
    GooeyLabel_SetColor(time_label, 0xFFFFFF);
    date_label = GooeyLabel_Create("Loading...", 0.25f, screen_info.width - 150, 35);
    GooeyLabel_SetColor(date_label, 0xCCCCCC);

    create_status_icons();

    GooeyWindow_RegisterWidget(win, canvas);
    GooeyWindow_RegisterWidget(win, wallpaper);
    GooeyWindow_RegisterWidget(win, apps_icon);
    GooeyWindow_RegisterWidget(win, settings_icon);
    GooeyWindow_RegisterWidget(win, test_app);
    GooeyWindow_RegisterWidget(win, time_label);
    GooeyWindow_RegisterWidget(win, date_label);

    init_system_settings();
    update_status_icons();

    gthread_t time_thread;
    if (glps_thread_create(&time_thread, NULL, time_update_thread, NULL) != 0)
    {
        fprintf(stderr, "Failed to create time update thread\n");
    }
    else
    {
        printf("Time update thread created successfully\n");
    }

    update_time_date();

    printf("Desktop started with Linux system control panel\n");
    printf("Click the settings icon to toggle control panel visibility\n");
    printf("Note: Brightness control may require sudo permissions\n");

    GooeyWindow_Run(1, win);

    printf("Stopping time update thread...\n");
    time_thread_running = 0;
    glps_thread_join(time_thread, NULL);

    GooeyWindow_Cleanup(1, win);
    cleanup_screen_info(&screen_info);
    printf("Desktop shutdown complete\n");
    return 0;
}