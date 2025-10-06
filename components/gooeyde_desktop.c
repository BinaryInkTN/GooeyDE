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

void run_app_menu();
void update_time_date();
void *time_update_thread(void *arg);
void toggle_control_panel();
void create_control_panel();
void destroy_control_panel();
void set_control_panel_visibility(int visible);

void wifi_switch_callback(bool value)
{
    printf("WiFi %s\n", value ? "Enabled" : "Disabled");
}

void bluetooth_switch_callback(bool value)
{
    printf("Bluetooth %s\n", value ? "Enabled" : "Disabled");
}

void airplane_switch_callback(bool value)
{
    printf("Airplane Mode %s\n", value ? "Enabled" : "Disabled");

    if (value && wifi_switch && GooeySwitch_GetState(wifi_switch))
    {
        GooeySwitch_Toggle(wifi_switch);
    }
    if (value && bluetooth_switch && GooeySwitch_GetState(bluetooth_switch))
    {
        GooeySwitch_Toggle(bluetooth_switch);
    }
}

void dark_mode_switch_callback(bool value)
{
    printf("Dark Mode %s\n", value ? "Enabled" : "Disabled");
}

void volume_slider_callback(long value)
{
    printf("Volume set to: %ld%%\n", value);

    if (volume_value_label)
    {
        char volume_text[32];
        snprintf(volume_text, sizeof(volume_text), "%ld%%", value);
        GooeyLabel_SetText(volume_value_label, volume_text);
    }
}

void brightness_slider_callback(long value)
{
    printf("Brightness set to: %ld%%\n", value);

    if (brightness_value_label)
    {
        char brightness_text[32];
        snprintf(brightness_text, sizeof(brightness_text), "%ld%%", value);
        GooeyLabel_SetText(brightness_value_label, brightness_text);
    }
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
    }
    else
    {
        set_control_panel_visibility(0);
    }
}

void create_control_panel()
{
    printf("Creating control panel\n");

    int panel_width = 363;
    int panel_height = 600;
    int panel_x = screen_info.width - panel_width - 10;
    int panel_y = 60;

    control_panel_bg = GooeyImage_Create("control_panel_bg.png", panel_x, panel_y, panel_width, panel_height, NULL);

    panel_title = GooeyLabel_Create("Control Panel", 0.45f, panel_x + 20, panel_y + 45);
    GooeyLabel_SetColor(panel_title, 0xFFFFFF);

    int current_y = panel_y + 90;
    int label_x = panel_x + 20;
    int switch_x = panel_x + panel_width - 120;
    int slider_width = 160;
    int slider_x = panel_x + panel_width - slider_width - 60;

    wifi_label = GooeyLabel_Create("WiFi", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(wifi_label, 0xFFFFFF);
    wifi_switch = GooeySwitch_Create(switch_x, current_y, true, false, wifi_switch_callback);

    current_y += 50;
    bluetooth_label = GooeyLabel_Create("Bluetooth", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(bluetooth_label, 0xFFFFFF);
    bluetooth_switch = GooeySwitch_Create(switch_x, current_y, false, false, bluetooth_switch_callback);

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
    volume_value_label = GooeyLabel_Create("75%", 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(volume_value_label, 0xCCCCCC);

    current_y += 50;
    brightness_label = GooeyLabel_Create("Brightness", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(brightness_label, 0xCCCCCC);
    brightness_slider = GooeySlider_Create(slider_x, current_y, slider_width, 0, 100, false, brightness_slider_callback);
    brightness_value_label = GooeyLabel_Create("80%", 0.25f, slider_x + slider_width + 10, current_y + 8);
    GooeyLabel_SetColor(brightness_value_label, 0xCCCCCC);

    current_y += 70;
    system_title = GooeyLabel_Create("System Info", 0.35f, label_x, current_y);
    GooeyLabel_SetColor(system_title, 0xAAAAAA);

    current_y += 40;
    battery_label = GooeyLabel_Create("Battery: 85%", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(battery_label, 0xCCCCCC);

    current_y += 30;
    network_label = GooeyLabel_Create("Network: Connected", 0.3f, label_x, current_y + 8);
    GooeyLabel_SetColor(network_label, 0xCCCCCC);

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

    GooeyImage *wallpaper = GooeyImage_Create("bg.png", 0, 50, screen_info.width, screen_info.height - 50, NULL);
    GooeyCanvas *canvas = GooeyCanvas_Create(0, 0, screen_info.width, screen_info.height, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, screen_info.width, 50, 0x222222, true, 1.0f, true, 1.0f);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);

    GooeyImage *apps_icon = GooeyImage_Create("apps.png", 10, 10, 30, 30, run_app_menu);
    GooeyImage *settings_icon = GooeyImage_Create("settings.png", screen_info.width - 210, 10, 30, 30, toggle_control_panel);
    GooeyImage *test_app = GooeyImage_Create("test_app.png", 65, 10, 30, 30, NULL);

    time_label = GooeyLabel_Create("Loading...", 0.3f, screen_info.width - 150, 18);
    GooeyLabel_SetColor(time_label, 0xFFFFFF);
    date_label = GooeyLabel_Create("Loading...", 0.25f, screen_info.width - 150, 35);
    GooeyLabel_SetColor(date_label, 0xCCCCCC);

    GooeyWindow_RegisterWidget(win, canvas);
    GooeyWindow_RegisterWidget(win, wallpaper);
    GooeyWindow_RegisterWidget(win, apps_icon);
    GooeyWindow_RegisterWidget(win, settings_icon);
    GooeyWindow_RegisterWidget(win, test_app);
    GooeyWindow_RegisterWidget(win, time_label);
    GooeyWindow_RegisterWidget(win, date_label);

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

    printf("Desktop started with control panel\n");
    printf("Click the settings icon to toggle control panel visibility\n");

    GooeyWindow_Run(1, win);

    printf("Stopping time update thread...\n");
    time_thread_running = 0;
    glps_thread_join(time_thread, NULL);

    GooeyWindow_Cleanup(1, win);
    cleanup_screen_info(&screen_info);

    printf("Desktop shutdown complete\n");
    return 0;
}