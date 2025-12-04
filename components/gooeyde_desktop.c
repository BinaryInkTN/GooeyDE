#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/sysinfo.h>
#include <math.h>
#include <Gooey/gooey.h>
#include <GLPS/glps_thread.h>
#include <dbus/dbus.h>
#include "utils/resolution_helper.h"
#include "utils/devices_helper.h"

ScreenInfo screen_info;
GooeyWindow *win = NULL;
GooeyLabel *time_label = NULL;
GooeyLabel *date_label = NULL;
int time_thread_running = 1;
GooeyLabel *wifi_label = NULL;
GooeyLabel *volume_label = NULL;
GooeyLabel *brightness_label = NULL;
GooeyLabel *battery_label = NULL;
GooeyLabel *network_label = NULL;
GooeyLabel *workspace_indicator = NULL;
GooeyImage *wifi_status_icon = NULL;
GooeyImage *volume_status_icon = NULL;
GooeyImage *battery_status_icon = NULL;
GooeyLabel *battery_percent_label = NULL;
GooeyImage *wallpaper = NULL;
char current_wallpaper_path[256] = "/usr/local/share/gooeyde/assets/bg.png";
int current_volume = 75;
int current_brightness = 80;
int battery_level = 85;
char network_status[64] = "Connected";
int current_workspace = 1;

// CPU chart variables
GooeyCanvas *cpu_chart_canvas = NULL;
float cpu_percentages[60];
int cpu_index = 0;
int max_cpu_history = 60;
unsigned long long last_total = 0;
unsigned long long last_idle = 0;
int is_first_cpu_read = 1;
GooeyLabel *cpu_label = NULL;

// Memory chart variables
GooeyCanvas *mem_chart_canvas = NULL;
float mem_percentages[60];
int mem_index = 0;
GooeyLabel *mem_label = NULL;

static DBusConnection *dbus_conn = NULL;
static int dbus_initialized = 0;
static int dbus_thread_running = 1;
static gthread_mutex_t ui_update_mutex;

#define DBUS_SERVICE "dev.binaryink.gshell"
#define DBUS_PATH "/dev/binaryink/gshell"
#define DBUS_INTERFACE "dev.binaryink.gshell"

void run_app_menu();
void run_systemsettings();
void init_system_settings();
void update_status_icons();
void update_time_date();
void refresh_system_info();
void *time_update_thread(void *arg);
void *dbus_monitor_thread(void *arg);
int init_dbus_connection();
void cleanup_dbus();
void process_dbus_message(DBusMessage *message);
void handle_workspace_changed(DBusMessage *message);
void handle_wallpaper_changed(DBusMessage *message);
void create_status_icons();
void update_workspace_indicator(int workspace);
void mute_audio_callback();
void change_wallpaper(const char *wallpaper_path);

float get_cpu_usage();
void update_cpu_chart();
void draw_cpu_chart();
void create_cpu_chart();

float get_memory_usage();
void update_memory_chart();
void draw_memory_chart();
void create_memory_chart();

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
    printf("Launching System Settings...\n");
    if (fork() == 0)
    {
        const char *possible_paths[] = {
            "/usr/local/bin/gooeyde_systemsettings",
            "/usr/bin/gnome-control-center",
            "/usr/bin/systemsettings",
            "/usr/bin/xfce4-settings-manager",
            "/usr/bin/cinnamon-settings",
            NULL};
        for (int i = 0; possible_paths[i] != NULL; i++)
        {
            if (access(possible_paths[i], X_OK) == 0)
            {
                execl(possible_paths[i], possible_paths[i], NULL);
                perror("Failed to launch system settings");
            }
        }
        fprintf(stderr, "No system settings application found\n");
        exit(1);
    }
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

float get_cpu_usage()
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file)
        return 0.0f;

    char line[256];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;

    if (fgets(line, sizeof(line), file))
    {
        sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    }
    fclose(file);

    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long idle_time = idle + iowait;

    if (is_first_cpu_read)
    {
        last_total = total;
        last_idle = idle_time;
        is_first_cpu_read = 0;
        return 0.0f;
    }

    unsigned long long total_diff = total - last_total;
    unsigned long long idle_diff = idle_time - last_idle;

    last_total = total;
    last_idle = idle_time;

    if (total_diff == 0)
        return 0.0f;

    float usage = 100.0f * (1.0f - ((float)idle_diff / (float)total_diff));
    return fminf(fmaxf(usage, 0.0f), 100.0f);
}

float get_memory_usage()
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file)
        return 0.0f;

    char line[256];
    unsigned long long total_mem = 0;
    unsigned long long free_mem = 0;
    unsigned long long buffers = 0;
    unsigned long long cached = 0;

    while (fgets(line, sizeof(line), file))
    {
        if (strstr(line, "MemTotal:"))
        {
            sscanf(line, "MemTotal: %llu kB", &total_mem);
        }
        else if (strstr(line, "MemFree:"))
        {
            sscanf(line, "MemFree: %llu kB", &free_mem);
        }
        else if (strstr(line, "Buffers:"))
        {
            sscanf(line, "Buffers: %llu kB", &buffers);
        }
        else if (strstr(line, "Cached:"))
        {
            sscanf(line, "Cached: %llu kB", &cached);
        }
    }
    fclose(file);

    if (total_mem == 0)
        return 0.0f;

    // Calculate used memory: Total - (Free + Buffers + Cached)
    unsigned long long used_mem = total_mem - (free_mem + buffers + cached);
    float usage_percent = (float)used_mem / (float)total_mem * 100.0f;

    return fminf(fmaxf(usage_percent, 0.0f), 100.0f);
}

void update_cpu_chart()
{
    float cpu_usage = get_cpu_usage();

    glps_thread_mutex_lock(&ui_update_mutex);

    cpu_percentages[cpu_index] = cpu_usage;
    cpu_index = (cpu_index + 1) % max_cpu_history;

    if (cpu_label)
    {
        char cpu_text[16];
        snprintf(cpu_text, sizeof(cpu_text), "CPU: %.1f%%", cpu_usage);
        GooeyLabel_SetText(cpu_label, cpu_text);
    }

    if (cpu_chart_canvas)
    {
        draw_cpu_chart();
    }

    glps_thread_mutex_unlock(&ui_update_mutex);
}

void update_memory_chart()
{
    float mem_usage = get_memory_usage();

    glps_thread_mutex_lock(&ui_update_mutex);

    mem_percentages[mem_index] = mem_usage;
    mem_index = (mem_index + 1) % max_cpu_history;

    if (mem_label)
    {
        char mem_text[16];
        snprintf(mem_text, sizeof(mem_text), "RAM: %.1f%%", mem_usage);
        GooeyLabel_SetText(mem_label, mem_text);
    }

    if (mem_chart_canvas)
    {
        draw_memory_chart();
    }

    glps_thread_mutex_unlock(&ui_update_mutex);
}

void draw_cpu_chart()
{
    if (!cpu_chart_canvas)
        return;

    GooeyCanvas_Clear(cpu_chart_canvas);

    int chart_width = 120;
    int chart_height = 30;
    int chart_x = 5;
    int chart_y = 10;

    // Draw chart background
    GooeyCanvas_DrawRectangle(cpu_chart_canvas, chart_x, chart_y, chart_width, chart_height,
                              0x333333, true, 1.0f, true, 3.0f);

    // Draw chart border
    GooeyCanvas_DrawRectangle(cpu_chart_canvas, chart_x, chart_y, chart_width, chart_height,
                              0x555555, false, 1.0f, true, 3.0f);

    int bar_width = chart_width / max_cpu_history;
    if (bar_width < 1)
        bar_width = 1;

    for (int i = 0; i < max_cpu_history; i++)
    {
        int idx = (cpu_index - i - 1 + max_cpu_history) % max_cpu_history;
        float usage = cpu_percentages[idx];

        if (usage > 0)
        {
            int bar_height = (int)((usage / 100.0f) * chart_height);
            if (bar_height < 1)
                bar_height = 1;

            unsigned long color;
            if (usage < 50)
                color = 0x00FF00; // Green
            else if (usage < 75)
                color = 0xFFFF00; // Yellow
            else
                color = 0xFF0000; // Red

            int x = chart_x + (max_cpu_history - i - 1) * bar_width;
            int y = chart_y + chart_height - bar_height;

            GooeyCanvas_DrawRectangle(cpu_chart_canvas, x, y, bar_width - 1, bar_height,
                                      color, true, 1.0f, false, 0);
        }
    }

    // Draw current usage line
    float current_usage = cpu_percentages[(cpu_index - 1 + max_cpu_history) % max_cpu_history];
    int line_y = chart_y + chart_height - (int)((current_usage / 100.0f) * chart_height);
    GooeyCanvas_DrawLine(cpu_chart_canvas, chart_x, line_y, chart_x + chart_width, line_y, 0xFFFFFF);
}

void draw_memory_chart()
{
    if (!mem_chart_canvas)
        return;

    GooeyCanvas_Clear(mem_chart_canvas);

    int chart_width = 120;
    int chart_height = 30;
    int chart_x = 5;
    int chart_y = 10;

    // Draw chart background
    GooeyCanvas_DrawRectangle(mem_chart_canvas, chart_x, chart_y, chart_width, chart_height,
                              0x333333, true, 1.0f, true, 3.0f);

    // Draw chart border
    GooeyCanvas_DrawRectangle(mem_chart_canvas, chart_x, chart_y, chart_width, chart_height,
                              0x555555, false, 1.0f, true, 3.0f);

    int bar_width = chart_width / max_cpu_history;
    if (bar_width < 1)
        bar_width = 1;

    for (int i = 0; i < max_cpu_history; i++)
    {
        int idx = (mem_index - i - 1 + max_cpu_history) % max_cpu_history;
        float usage = mem_percentages[idx];

        if (usage > 0)
        {
            int bar_height = (int)((usage / 100.0f) * chart_height);
            if (bar_height < 1)
                bar_height = 1;

            unsigned long color;
            if (usage < 50)
                color = 0x0099FF; // Blue
            else if (usage < 75)
                color = 0xFF9900; // Orange
            else
                color = 0xFF0066; // Magenta/Pink

            int x = chart_x + (max_cpu_history - i - 1) * bar_width;
            int y = chart_y + chart_height - bar_height;

            GooeyCanvas_DrawRectangle(mem_chart_canvas, x, y, bar_width - 1, bar_height,
                                      color, true, 1.0f, false, 0);
        }
    }

    // Draw current usage line
    float current_usage = mem_percentages[(mem_index - 1 + max_cpu_history) % max_cpu_history];
    int line_y = chart_y + chart_height - (int)((current_usage / 100.0f) * chart_height);
    GooeyCanvas_DrawLine(mem_chart_canvas, chart_x, line_y, chart_x + chart_width, line_y, 0xFFFFFF);
}

void create_cpu_chart()
{
    for (int i = 0; i < max_cpu_history; i++)
    {
        cpu_percentages[i] = 0.0f;
    }

    cpu_chart_canvas = GooeyCanvas_Create(screen_info.width - 600, 0, 130, 40, NULL, NULL);
    cpu_label = GooeyLabel_Create("CPU: 0.0%", 10.0f, screen_info.width - 560, 29);
    GooeyLabel_SetColor(cpu_label, 0xFFFFFF);

    GooeyWindow_RegisterWidget(win, cpu_chart_canvas);
    GooeyWindow_RegisterWidget(win, cpu_label);

    draw_cpu_chart();
}

void create_memory_chart()
{
    for (int i = 0; i < max_cpu_history; i++)
    {
        mem_percentages[i] = 0.0f;
    }

    mem_chart_canvas = GooeyCanvas_Create(screen_info.width - 750, 0, 130, 40, NULL, NULL);
    mem_label = GooeyLabel_Create("RAM: 0.0%", 10.0f, screen_info.width - 713, 29);
    GooeyLabel_SetColor(mem_label, 0xFFFFFF);

    GooeyWindow_RegisterWidget(win, mem_chart_canvas);
    GooeyWindow_RegisterWidget(win, mem_label);

    draw_memory_chart();
}

void refresh_system_info()
{
    printf("Refreshing system information...\n");
    battery_level = get_battery_level();
    glps_thread_mutex_lock(&ui_update_mutex);
    get_network_status(network_status);
    int system_volume = get_system_volume();
    if (system_volume != current_volume)
    {
        current_volume = system_volume;
    }
    int system_brightness = get_system_brightness();
    int max_bright = get_max_brightness();
    int brightness_percent = (system_brightness * 100) / max_bright;
    if (brightness_percent != current_brightness)
    {
        current_brightness = brightness_percent;
    }
    glps_thread_mutex_unlock(&ui_update_mutex);
    update_status_icons();
    update_cpu_chart();
    update_memory_chart();

    printf("System info refreshed\n");
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
        else
        {
            update_cpu_chart();
            update_memory_chart();
        }
        sleep(1);
    }
    printf("Time update thread stopped\n");
    return NULL;
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
    char match_rule[512];
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
        if (strcmp(member, "WorkspaceChanged") == 0)
        {
            handle_workspace_changed(message);
        }
        else if (strcmp(member, "WallpaperChanged") == 0)
        {
            handle_wallpaper_changed(message);
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

void handle_wallpaper_changed(DBusMessage *message)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter))
    {
        fprintf(stderr, "No arguments in WallpaperChanged signal\n");
        return;
    }
    const char *wallpaper_path = NULL;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &wallpaper_path);
    }
    if (wallpaper_path == NULL)
    {
        fprintf(stderr, "Invalid wallpaper path in WallpaperChanged signal\n");
        return;
    }
    printf("Wallpaper changed: %s\n", wallpaper_path);
    change_wallpaper(wallpaper_path);
}

void change_wallpaper(const char *wallpaper_path)
{
    if (!wallpaper_path || strlen(wallpaper_path) == 0)
    {
        fprintf(stderr, "Empty wallpaper path\n");
        return;
    }
    if (access(wallpaper_path, F_OK) != 0)
    {
        fprintf(stderr, "Wallpaper file not found: %s\n", wallpaper_path);
        return;
    }
    strncpy(current_wallpaper_path, wallpaper_path, sizeof(current_wallpaper_path) - 1);
    current_wallpaper_path[sizeof(current_wallpaper_path) - 1] = '\0';
    glps_thread_mutex_lock(&ui_update_mutex);
    if (wallpaper)
    {
        GooeyImage_SetImage(wallpaper, wallpaper_path);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);
    const char *filename = strrchr(wallpaper_path, '/');
    if (filename)
        filename++;
    else
        filename = wallpaper_path;
    printf("Wallpaper updated to: %s\n", wallpaper_path);
}

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

void mute_audio_callback()
{
    mute_audio();
    update_status_icons();
}

void create_status_icons()
{
    int icon_size = 24;
    int icon_y = 13;
    int start_x = screen_info.width - 400;
    wifi_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/wifi_on.png",
                                         start_x, icon_y, icon_size, icon_size, NULL, NULL);
    volume_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/volume_high.png",
                                           start_x + 80, icon_y, icon_size, icon_size,
                                           mute_audio_callback, NULL);
    battery_status_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/battery_full.png",
                                            start_x + 120, icon_y, icon_size, icon_size, NULL, NULL);
    battery_percent_label = GooeyLabel_Create("85%", 0.26f, start_x + 148, icon_y + 5);
    GooeyLabel_SetColor(battery_percent_label, 0xFFFFFF);
    GooeyWindow_RegisterWidget(win, wifi_status_icon);
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
    win = GooeyWindow_Create("Gooey Desktop", 0, 0, screen_info.width, screen_info.height, true);
    GooeyNotifications_Run(win, "Welcome to GooeyDE", NOTIFICATION_INFO, NOTIFICATION_POSITION_TOP_RIGHT);
    wallpaper = GooeyImage_Create(current_wallpaper_path, 0, 50,
                                  screen_info.width, screen_info.height - 50,
                                  NULL, NULL);
    GooeyCanvas *canvas = GooeyCanvas_Create(0, 0, screen_info.width, 50, NULL, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, screen_info.width, 50, 0x222222,
                              true, 1.0f, true, 1.0f);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);
    workspace_indicator = GooeyLabel_Create("Workspace 1", 18.0f,
                                            screen_info.width / 2 - 60, 30);
    GooeyLabel_SetColor(workspace_indicator, 0xFFFFFF);
    GooeyImage *apps_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/apps.png",
                                              10, 10, 30, 30, run_app_menu, NULL);
    GooeyImage *settings_icon = GooeyImage_Create("/usr/local/share/gooeyde/assets/settings.png",
                                                  screen_info.width - 210, 10, 30, 30,
                                                  run_systemsettings, NULL);
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

    GooeyCanvas_DrawLine(canvas, screen_info.width - 430, 0, screen_info.width - 430, 50, 0xFFFFFF);

    get_cpu_usage();
    create_cpu_chart();
    create_memory_chart();

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
    printf("Initial wallpaper: %s\n", current_wallpaper_path);
    printf("CPU chart initialized with %d sample history\n", max_cpu_history);
    printf("Memory chart initialized with %d sample history\n", max_cpu_history);
    printf("Entering main window loop...\n");

    GooeyWindow_Run(1, win);

    printf("Window closed, stopping threads...\n");
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