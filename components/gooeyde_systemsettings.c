#include <Gooey/gooey.h>
#include "utils/resolution_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <GLPS/glps_thread.h>
#include <time.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include "utils/devices_helper.h"

GooeyTabs *settings_tabs = NULL;
GooeyWindow *win = NULL;
GooeySwitch *wifi_switch = NULL;
GooeySwitch *bluetooth_switch = NULL;
GooeyList *wifi_list = NULL;
GooeyList *bluetooth_list = NULL;
GooeyButton *refresh_wifi_btn = NULL;
GooeyButton *refresh_bluetooth_btn = NULL;
GooeyButton *disconnect_wifi_btn = NULL;
GooeyLabel *wifi_status_label = NULL;
GooeyLabel *bt_status_label = NULL;

GooeySlider *brightness_slider = NULL;

GooeySlider *master_volume_slider = NULL;
GooeySwitch *mute_switch = NULL;
GooeyDropdown *audio_devices_dropdown = NULL;
GooeyButton *refresh_audio_btn = NULL;

GooeyList *process_list = NULL;
GooeyButton *kill_process_btn = NULL;
GooeyButton *refresh_processes_btn = NULL;

GooeyList *storage_list = NULL;
GooeyButton *refresh_storage_btn = NULL;

GooeyLabel *host_value = NULL;
GooeyLabel *user_value = NULL;
GooeyLabel *os_value = NULL;
GooeyLabel *cpu_value = NULL;
GooeyLabel *ram_value = NULL;
GooeyLabel *kernel_value = NULL;
GooeyCanvas *system_header = NULL;
GooeyCanvas *wifi_header = NULL;
GooeyCanvas *bluetooth_header = NULL;
GooeyCanvas *audio_header = NULL;
GooeyCanvas *process_header = NULL;
GooeyCanvas *storage_header = NULL;

#define MAX_WIFI_NETWORKS 128
#define MAX_BT_DEVICES 128
#define MAX_PROCESSES 256
#define MAX_AUDIO_DEVICES 32
#define CMD_BUFFER_SIZE 512

static char wifi_ssids[MAX_WIFI_NETWORKS][128];
static int wifi_count = 0;
static gthread_mutex_t wifi_mutex;
static bool wifi_scanning = false;
static bool is_wifi_enabled = false;

static char bt_devices[MAX_BT_DEVICES][128];
static char bt_addresses[MAX_BT_DEVICES][64];
static int bt_count = 0;
static gthread_mutex_t bt_mutex;
static bool bt_scanning = false;
static bool is_bt_enabled = false;

static bool is_dark_mode = true;

static int process_pids[MAX_PROCESSES];
static char process_names[MAX_PROCESSES][128];
static char process_users[MAX_PROCESSES][32];
static float process_cpu[MAX_PROCESSES];
static float process_mem[MAX_PROCESSES];
static int process_count = 0;
static int selected_process_pid = -1;
static gthread_mutex_t process_mutex;

static char storage_filesystems[16][32];
static char storage_sizes[16][16];
static char storage_used[16][16];
static char storage_available[16][16];
static char storage_mounts[16][128];
static int storage_count = 0;

static char audio_devices[MAX_AUDIO_DEVICES][128];
static char audio_descriptions[MAX_AUDIO_DEVICES][128];
static int audio_device_count = 0;
static bool is_muted = false;
static int current_volume = 50;
static gthread_mutex_t audio_mutex;

static int window_width = 0;
static int window_height = 0;

static void cleanup_resources(void)
{
    printf("Cleaning up resources...\n");

    glps_thread_mutex_destroy(&wifi_mutex);
    glps_thread_mutex_destroy(&bt_mutex);
    glps_thread_mutex_destroy(&process_mutex);
    glps_thread_mutex_destroy(&audio_mutex);

    if (win) {
        GooeyWindow_Cleanup(1, win);
        win = NULL;
    }

    settings_tabs = NULL;
    wifi_switch = NULL;
    bluetooth_switch = NULL;
    wifi_list = NULL;
    bluetooth_list = NULL;
    refresh_wifi_btn = NULL;
    refresh_bluetooth_btn = NULL;
    disconnect_wifi_btn = NULL;
    wifi_status_label = NULL;
    bt_status_label = NULL;
    brightness_slider = NULL;
    master_volume_slider = NULL;
    mute_switch = NULL;
    audio_devices_dropdown = NULL;
    refresh_audio_btn = NULL;
    process_list = NULL;
    kill_process_btn = NULL;
    refresh_processes_btn = NULL;
    storage_list = NULL;
    refresh_storage_btn = NULL;
    host_value = NULL;
    user_value = NULL;
    os_value = NULL;
    cpu_value = NULL;
    ram_value = NULL;
    kernel_value = NULL;
    system_header = NULL;
    wifi_header = NULL;
    bluetooth_header = NULL;
    audio_header = NULL;
    process_header = NULL;
    storage_header = NULL;

    printf("Cleanup completed.\n");
}

static void safe_string_copy(char *dest, const char *src, size_t dest_size)
{
    if (dest && src && dest_size > 0)
    {
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}

static void trim_whitespace(char *str)
{
    if (!str)
        return;

    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r'))
        start++;

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        end--;

    *(end + 1) = '\0';
    if (start != str)
        memmove(str, start, strlen(start) + 1);
}

static void extract_audio_device_name(const char *long_name, char *short_name, size_t size)
{
    if (!long_name || !short_name || size == 0)
        return;

    if (strstr(long_name, "alsa_output") != NULL)
    {
        const char *last_dot = strrchr(long_name, '.');
        if (last_dot)
        {
            snprintf(short_name, size, "Speaker%s", last_dot);
        }
        else
        {
            safe_string_copy(short_name, "Speakers", size);
        }
    }
    else if (strstr(long_name, "alsa_input") != NULL)
    {
        const char *last_dot = strrchr(long_name, '.');
        if (last_dot)
        {
            snprintf(short_name, size, "Microphone%s", last_dot);
        }
        else
        {
            safe_string_copy(short_name, "Microphone", size);
        }
    }
    else if (strstr(long_name, "bluez") != NULL)
    {
        safe_string_copy(short_name, "Bluetooth Audio", size);
    }
    else if (strstr(long_name, "hdmi") != NULL || strstr(long_name, "HDMI") != NULL)
    {
        safe_string_copy(short_name, "HDMI Output", size);
    }
    else
    {
        strncpy(short_name, long_name, size - 1);
        short_name[size - 1] = '\0';
        if (strlen(short_name) == size - 1)
        {
            short_name[size - 4] = '.';
            short_name[size - 3] = '.';
            short_name[size - 2] = '.';
            short_name[size - 1] = '\0';
        }
    }
}

static void get_current_wifi_connection(char *ssid, size_t size)
{
    if (!ssid || size == 0)
        return;

    ssid[0] = '\0';
    FILE *fp = popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes' | cut -d':' -f2", "r");
    if (fp)
    {
        if (fgets(ssid, size, fp))
        {
            trim_whitespace(ssid);
        }
        pclose(fp);
    }
}

static void update_wifi_status()
{
    if (!wifi_status_label)
        return;

    char current_ssid[128] = {0};
    get_current_wifi_connection(current_ssid, sizeof(current_ssid));

    char status_text[256];
    if (strlen(current_ssid) > 0)
    {
        snprintf(status_text, sizeof(status_text), "Connected to: %s", current_ssid);
        if (disconnect_wifi_btn)
            GooeyWidget_MakeVisible(disconnect_wifi_btn, true);
    }
    else if (is_wifi_enabled)
    {
        snprintf(status_text, sizeof(status_text), "Not connected");
        if (disconnect_wifi_btn)
            GooeyWidget_MakeVisible(disconnect_wifi_btn, false);
    }
    else
    {
        snprintf(status_text, sizeof(status_text), "Wi-Fi disabled");
        if (disconnect_wifi_btn)
            GooeyWidget_MakeVisible(disconnect_wifi_btn, false);
    }

    GooeyLabel_SetText(wifi_status_label, status_text);
}

static void *wifi_scan_thread(void *arg)
{
    glps_thread_mutex_lock(&wifi_mutex);
    wifi_scanning = true;
    wifi_count = 0;
    if (wifi_list)
        GooeyList_ClearItems(wifi_list);
    glps_thread_mutex_unlock(&wifi_mutex);

    if (system("nmcli dev wifi rescan > /dev/null 2>&1") != 0)
    {
        glps_thread_mutex_lock(&wifi_mutex);
        wifi_scanning = false;
        if (wifi_list)
            GooeyList_AddItem(wifi_list, "", "Failed to scan networks");
        glps_thread_mutex_unlock(&wifi_mutex);
        update_wifi_status();
        return NULL;
    }
    sleep(2);

    FILE *fp = popen("nmcli -t -f SSID,SIGNAL,SECURITY dev wifi 2>/dev/null", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            trim_whitespace(line);
            if (strlen(line) == 0)
                continue;

            glps_thread_mutex_lock(&wifi_mutex);
            if (wifi_count >= MAX_WIFI_NETWORKS)
            {
                glps_thread_mutex_unlock(&wifi_mutex);
                break;
            }

            char ssid[128] = {0};
            char signal[32] = {0};
            char security[64] = {0};

            char *first_colon = strchr(line, ':');
            char *second_colon = first_colon ? strchr(first_colon + 1, ':') : NULL;

            if (first_colon && second_colon)
            {
                size_t ssid_len = first_colon - line;
                if (ssid_len < sizeof(ssid))
                {
                    strncpy(ssid, line, ssid_len);
                    ssid[ssid_len] = '\0';
                }

                size_t signal_len = second_colon - first_colon - 1;
                if (signal_len < sizeof(signal))
                {
                    strncpy(signal, first_colon + 1, signal_len);
                    signal[signal_len] = '\0';
                }

                safe_string_copy(security, second_colon + 1, sizeof(security));
                trim_whitespace(security);

                if (strlen(ssid) == 0 && wifi_count > 0)
                {
                    glps_thread_mutex_unlock(&wifi_mutex);
                    continue;
                }

                char entry[256];
                const char *lock_icon = (strlen(security) > 0 && strcmp(security, "--") != 0) ? "🔒 " : "";
                snprintf(entry, sizeof(entry), "%s%s (Signal: %s%%) %s",
                         lock_icon, strlen(ssid) > 0 ? ssid : "<Hidden>",
                         strlen(signal) > 0 ? signal : "?", security);

                if (wifi_list)
                    GooeyList_AddItem(wifi_list, "", entry);

                safe_string_copy(wifi_ssids[wifi_count], strlen(ssid) > 0 ? ssid : "<Hidden>",
                                 sizeof(wifi_ssids[wifi_count]));
                wifi_count++;
            }
            glps_thread_mutex_unlock(&wifi_mutex);
        }
        pclose(fp);
    }

    glps_thread_mutex_lock(&wifi_mutex);
    wifi_scanning = false;
    if (wifi_count == 0 && wifi_list)
    {
        GooeyList_AddItem(wifi_list, "", "No networks found");
    }
    glps_thread_mutex_unlock(&wifi_mutex);

    update_wifi_status();
    return NULL;
}

static void refresh_wifi_list()
{
    glps_thread_mutex_lock(&wifi_mutex);
    if (wifi_scanning || !is_wifi_enabled)
    {
        if (!is_wifi_enabled && wifi_list)
            GooeyList_ClearItems(wifi_list);
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }
    if (wifi_list)
    {
        GooeyList_ClearItems(wifi_list);
        GooeyList_AddItem(wifi_list, "", "Scanning...");
    }
    glps_thread_mutex_unlock(&wifi_mutex);

    gthread_t thread;
    if (glps_thread_create(&thread, NULL, wifi_scan_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void *connect_to_network_thread(void *arg)
{
    char *ssid = (char *)arg;
    if (!ssid)
        return NULL;

    char cmd[CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect \"%s\" 2>&1", ssid);

    FILE *fp = popen(cmd, "r");
    if (fp)
    {
        char output[512];
        if (fgets(output, sizeof(output), fp))
        {
            trim_whitespace(output);
            if (wifi_status_label && strstr(output, "successfully"))
                GooeyLabel_SetText(wifi_status_label, "Connection successful");
            else if (wifi_status_label)
                GooeyLabel_SetText(wifi_status_label, "Connection failed");
        }
        pclose(fp);
    }

    sleep(2);
    update_wifi_status();
    free(ssid);
    return NULL;
}

static void connect_to_network_callback(int index, void *user_data)
{
    glps_thread_mutex_lock(&wifi_mutex);
    if (index <= 0 || index > wifi_count)
    {
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }

    const char *ssid = wifi_ssids[index - 1];
    if (strcmp(ssid, "<Hidden>") == 0 || strlen(ssid) == 0)
    {
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }

    char *ssid_copy = strdup(ssid);
    glps_thread_mutex_unlock(&wifi_mutex);

    if (ssid_copy)
    {
        gthread_t thread;
        if (glps_thread_create(&thread, NULL, connect_to_network_thread, ssid_copy) == 0)
        {
            glps_thread_detach(thread);
        }
        else
        {
            free(ssid_copy);
        }
    }
}

static void *wifi_disconnect_thread(void *arg)
{
    if (system("nmcli dev disconnect wlan0 > /dev/null 2>&1") != 0)
    {
        if (wifi_status_label)
            GooeyLabel_SetText(wifi_status_label, "Error: Failed to disconnect");
    }
    sleep(1);
    update_wifi_status();
    return NULL;
}

static void disconnect_wifi_callback(void *user_data)
{
    gthread_t thread;
    if (glps_thread_create(&thread, NULL, wifi_disconnect_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void *wifi_toggle_thread(void *arg)
{
    bool enabled = *(bool *)arg;

    if (system(enabled ? "nmcli radio wifi on" : "nmcli radio wifi off") != 0)
    {
        if (wifi_status_label)
            GooeyLabel_SetText(wifi_status_label, enabled ? "Error: Failed to enable Wi-Fi" : "Error: Failed to disable Wi-Fi");
    }
    sleep(1);
    update_wifi_status();
    return NULL;
}

static void wifi_switch_callback(bool enabled, void *user_data)
{
    is_wifi_enabled = enabled;
    gthread_t thread;
    if (glps_thread_create(&thread, NULL, wifi_toggle_thread, &is_wifi_enabled) == 0)
    {
        glps_thread_detach(thread);
    }
    else
    {
        if (wifi_status_label)
            GooeyLabel_SetText(wifi_status_label, "Error: Failed to toggle Wi-Fi");
    }

    if (enabled)
    {
        if (wifi_list)
            GooeyWidget_MakeVisible(wifi_list, true);
        if (refresh_wifi_btn)
            GooeyWidget_MakeVisible(refresh_wifi_btn, true);
    }
    else
    {
        if (wifi_list)
        {
            GooeyWidget_MakeVisible(wifi_list, false);
            GooeyList_ClearItems(wifi_list);
        }
        if (refresh_wifi_btn)
            GooeyWidget_MakeVisible(refresh_wifi_btn, false);
        update_wifi_status();
    }

    refresh_wifi_list();
}

static bool get_wifi_enabled()
{
    return get_system_wifi_state();
}

static void refresh_button_callback(void *user_data)
{
    refresh_wifi_list();
}

static void update_bluetooth_status()
{
    if (!bt_status_label)
        return;

    char status_text[256];
    if (is_bt_enabled)
    {
        FILE *fp = popen("bluetoothctl info 2>/dev/null | grep 'Connected: yes' | wc -l", "r");
        if (fp)
        {
            char count[8] = {0};
            if (fgets(count, sizeof(count), fp))
            {
                int connected = atoi(count);
                snprintf(status_text, sizeof(status_text),
                         connected > 0 ? "Connected devices: %d" : "No devices connected", connected);
            }
            else
            {
                snprintf(status_text, sizeof(status_text), "Bluetooth enabled");
            }
            pclose(fp);
        }
        else
        {
            snprintf(status_text, sizeof(status_text), "Bluetooth enabled");
        }
    }
    else
    {
        snprintf(status_text, sizeof(status_text), "Bluetooth disabled");
    }

    GooeyLabel_SetText(bt_status_label, status_text);
}

static void *bluetooth_scan_thread(void *arg)
{
    glps_thread_mutex_lock(&bt_mutex);
    bt_scanning = true;
    bt_count = 0;
    if (bluetooth_list)
        GooeyList_ClearItems(bluetooth_list);
    glps_thread_mutex_unlock(&bt_mutex);

    if (system("bluetoothctl --timeout 10 scan on > /dev/null 2>&1 &") != 0)
    {
        glps_thread_mutex_lock(&bt_mutex);
        bt_scanning = false;
        if (bluetooth_list)
            GooeyList_AddItem(bluetooth_list, "", "Failed to scan devices");
        glps_thread_mutex_unlock(&bt_mutex);
        update_bluetooth_status();
        return NULL;
    }
    sleep(3);

    FILE *fp = popen("bluetoothctl devices 2>/dev/null", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            trim_whitespace(line);

            glps_thread_mutex_lock(&bt_mutex);
            if (bt_count >= MAX_BT_DEVICES)
            {
                glps_thread_mutex_unlock(&bt_mutex);
                break;
            }

            if (strncmp(line, "Device ", 7) == 0)
            {
                char address[64] = {0};
                char name[128] = {0};

                char *addr_start = line + 7;
                char *addr_end = strchr(addr_start, ' ');

                if (addr_end)
                {
                    size_t addr_len = addr_end - addr_start;
                    if (addr_len < sizeof(address))
                    {
                        strncpy(address, addr_start, addr_len);
                        address[addr_len] = '\0';
                    }

                    safe_string_copy(name, addr_end + 1, sizeof(name));
                    trim_whitespace(name);

                    char entry[192];
                    snprintf(entry, sizeof(entry), "%s (%s)",
                             strlen(name) > 0 ? name : "Unknown Device", address);

                    if (bluetooth_list)
                        GooeyList_AddItem(bluetooth_list, "", entry);

                    safe_string_copy(bt_devices[bt_count], name, sizeof(bt_devices[bt_count]));
                    safe_string_copy(bt_addresses[bt_count], address, sizeof(bt_addresses[bt_count]));
                    bt_count++;
                }
            }
            glps_thread_mutex_unlock(&bt_mutex);
        }
        pclose(fp);
    }

    system("bluetoothctl scan off > /dev/null 2>&1");

    glps_thread_mutex_lock(&bt_mutex);
    bt_scanning = false;
    if (bt_count == 0 && bluetooth_list)
    {
        GooeyList_AddItem(bluetooth_list, "", "No devices found");
    }
    glps_thread_mutex_unlock(&bt_mutex);

    update_bluetooth_status();
    return NULL;
}

static void refresh_bluetooth_list()
{
    glps_thread_mutex_lock(&bt_mutex);
    if (bt_scanning || !is_bt_enabled)
    {
        if (!is_bt_enabled && bluetooth_list)
            GooeyList_ClearItems(bluetooth_list);
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }
    if (bluetooth_list)
    {
        GooeyList_ClearItems(bluetooth_list);
        GooeyList_AddItem(bluetooth_list, "", "Scanning...");
    }
    glps_thread_mutex_unlock(&bt_mutex);

    gthread_t thread;
    if (glps_thread_create(&thread, NULL, bluetooth_scan_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void *connect_to_bluetooth_thread(void *arg)
{
    char *address = (char *)arg;
    if (!address)
        return NULL;

    char cmd[CMD_BUFFER_SIZE];
    printf("Connecting to Bluetooth device: %s\n", address);

    snprintf(cmd, sizeof(cmd), "bluetoothctl trust %s > /dev/null 2>&1", address);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "bluetoothctl pair %s > /dev/null 2>&1", address);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s > /dev/null 2>&1", address);
    if (system(cmd) != 0)
    {
        if (bt_status_label)
            GooeyLabel_SetText(bt_status_label, "Error: Failed to connect");
    }

    sleep(2);
    update_bluetooth_status();
    free(address);
    return NULL;
}

static void connect_to_bluetooth_callback(int index, void *user_data)
{
    glps_thread_mutex_lock(&bt_mutex);
    if (index <= 0 || index > bt_count)
    {
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }

    const char *address = bt_addresses[index - 1];
    if (strlen(address) == 0)
    {
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }

    char *addr_copy = strdup(address);
    glps_thread_mutex_unlock(&bt_mutex);

    if (addr_copy)
    {
        gthread_t thread;
        if (glps_thread_create(&thread, NULL, connect_to_bluetooth_thread, addr_copy) == 0)
        {
            glps_thread_detach(thread);
        }
        else
        {
            free(addr_copy);
        }
    }
}

static void *bluetooth_toggle_thread(void *arg)
{
    bool enabled = *(bool *)arg;

    if (system(enabled ? "bluetoothctl power on" : "bluetoothctl power off") != 0)
    {
    }
    sleep(1);
    update_bluetooth_status();
    return NULL;
}

static void bluetooth_switch_callback(bool enabled, void *user_data)
{
    is_bt_enabled = enabled;

    gthread_t thread;
    if (glps_thread_create(&thread, NULL, bluetooth_toggle_thread, &is_bt_enabled) == 0)
    {
        glps_thread_detach(thread);
    }
    else
    {
        if (bt_status_label)
            GooeyLabel_SetText(bt_status_label, "Error: Failed to toggle Bluetooth");
    }

    if (enabled)
    {
        if (bluetooth_list)
            GooeyWidget_MakeVisible(bluetooth_list, true);
        if (refresh_bluetooth_btn)
            GooeyWidget_MakeVisible(refresh_bluetooth_btn, true);
        refresh_bluetooth_list();
    }
    else
    {
        if (bluetooth_list)
        {
            GooeyWidget_MakeVisible(bluetooth_list, false);
            GooeyList_ClearItems(bluetooth_list);
        }
        if (refresh_bluetooth_btn)
            GooeyWidget_MakeVisible(refresh_bluetooth_btn, false);
        update_bluetooth_status();
    }
}

static bool get_bluetooth_enabled()
{
    return get_system_bluetooth_state();
}

static void refresh_bluetooth_button_callback(void *user_data)
{
    refresh_bluetooth_list();
}

static void brightness_slider_callback(float value, void *user_data)
{
    if (value < 0.1f)
        value = 0.1f;
    if (value > 1.0f)
        value = 1.0f;

    char cmd[256];
    int success = 0;

    snprintf(cmd, sizeof(cmd),
             "xrandr --output $(xrandr 2>/dev/null | grep ' connected' | head -n1 | cut -d' ' -f1) --brightness %.2f 2>/dev/null",
             value);
    if (system(cmd) == 0)
    {
        success = 1;
    }

    if (!success)
    {
        snprintf(cmd, sizeof(cmd), "brightnessctl set %.0f%% 2>/dev/null", value * 100);
        if (system(cmd) == 0)
        {
            success = 1;
        }
    }

    if (!success)
    {
        int brightness_value = (int)(value * get_max_brightness());
        change_display_brightness(brightness_value);
        success = 1;
    }

    if (!success)
    {
        printf("Warning: Could not set brightness. Try running with sudo or check available methods.\n");
    }
    else
    {
        printf("Brightness set to %.0f%%\n", value * 100);
    }
}

static int get_current_volume_level()
{
    return get_system_volume();
}

static bool get_current_mute_state()
{
    FILE *fp = popen("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -q 'Mute: yes' && echo 1 || echo 0", "r");
    if (fp)
    {
        char mute_str[16] = {0};
        if (fgets(mute_str, sizeof(mute_str), fp))
        {
            trim_whitespace(mute_str);
            pclose(fp);
            return (strcmp(mute_str, "1") == 0);
        }
        pclose(fp);
    }
    return false;
}

static void master_volume_callback(long value, void *user_data)
{
    char cmd[128];

    current_volume = value;
    change_system_volume(value);

    char status[64];
    snprintf(status, sizeof(status), "Volume: %d%%", value);
}

static void mute_switch_callback(bool muted, void *user_data)
{
    is_muted = muted;
    if (muted)
    {
        execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ 1");
    }
    else
    {
        execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ 0");
    }
}

static void *refresh_audio_devices_thread(void *arg)
{
    glps_thread_mutex_lock(&audio_mutex);

    audio_device_count = 0;

    char default_sink[128] = {0};
    FILE *fp = popen("pactl get-default-sink 2>/dev/null", "r");
    if (fp)
    {
        if (fgets(default_sink, sizeof(default_sink), fp))
        {
            trim_whitespace(default_sink);
        }
        pclose(fp);
    }

    fp = popen("pactl list sinks short 2>/dev/null", "r");
    if (fp)
    {
        char line[512];
        while (fgets(line, sizeof(line), fp) && audio_device_count < MAX_AUDIO_DEVICES)
        {
            trim_whitespace(line);

            char device_id[64], name[128], state[32];
            if (sscanf(line, "%63s %127s %31s", device_id, name, state) >= 2)
            {
                safe_string_copy(audio_devices[audio_device_count], device_id, sizeof(audio_devices[audio_device_count]));

                char short_name[128];
                extract_audio_device_name(name, short_name, sizeof(short_name));
                safe_string_copy(audio_descriptions[audio_device_count], short_name, sizeof(audio_descriptions[audio_device_count]));

                audio_device_count++;
            }
        }
        pclose(fp);
    }

    if (audio_device_count > 0 && audio_devices_dropdown)
    {
        const char *options[MAX_AUDIO_DEVICES] = {0};
        for (int i = 0; i < audio_device_count; i++)
        {
            options[i] = audio_descriptions[i];
        }
        GooeyDropdown_Update(audio_devices_dropdown, options, audio_device_count);
    }

    int current_system_volume = get_current_volume_level();
    bool current_system_mute = get_current_mute_state();

    if (master_volume_slider)
    {
        master_volume_slider->value = current_system_volume ;
        current_volume = current_system_volume;
    }

    if (mute_switch)
    {
        mute_switch->is_toggled = current_system_mute;
        is_muted = current_system_mute;
    }

    glps_thread_mutex_unlock(&audio_mutex);
    return NULL;
}

static void refresh_audio_callback(void *user_data)
{
    gthread_t thread;
    if (glps_thread_create(&thread, NULL, refresh_audio_devices_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void audio_device_selection_callback(int index, void *user_data)
{
    if (index < 0 || index >= audio_device_count)
        return;

    glps_thread_mutex_lock(&audio_mutex);
    const char *device_id = audio_devices[index];

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pactl set-default-sink %s 2>/dev/null", device_id);
    if (system(cmd) != 0)
    {
        glps_thread_mutex_unlock(&audio_mutex);
        return;
    }

    glps_thread_mutex_unlock(&audio_mutex);
}

static void *refresh_processes_thread(void *arg)
{
    glps_thread_mutex_lock(&process_mutex);
    process_count = 0;
    if (process_list)
        GooeyList_ClearItems(process_list);

    FILE *fp = popen("ps -eo pid,user,pcpu,pmem,comm --sort=-pcpu 2>/dev/null | head -21", "r");
    if (fp)
    {
        char line[512];
        int count = 0;
        while (fgets(line, sizeof(line), fp) && process_count < MAX_PROCESSES)
        {
            if (count > 0)
            {
                trim_whitespace(line);
                char pid_str[16], user[32], cpu_str[16], mem_str[16], command[128];
                if (sscanf(line, "%15s %31s %15s %15s %127[^\n]", pid_str, user, cpu_str, mem_str, command) == 5)
                {
                    process_pids[process_count] = atoi(pid_str);
                    safe_string_copy(process_users[process_count], user, sizeof(process_users[process_count]));
                    process_cpu[process_count] = atof(cpu_str);
                    process_mem[process_count] = atof(mem_str);
                    safe_string_copy(process_names[process_count], command, sizeof(process_names[process_count]));

                    char entry[256];
                    snprintf(entry, sizeof(entry), "PID: %s | %s | CPU: %s%% | MEM: %s%%",
                             pid_str, command, cpu_str, mem_str);

                    if (process_list)
                        GooeyList_AddItem(process_list, "", entry);

                    process_count++;
                }
            }
            count++;
        }
        pclose(fp);
    }

    if (process_count == 0 && process_list)
    {
        GooeyList_AddItem(process_list, "", "No processes found");
    }

    glps_thread_mutex_unlock(&process_mutex);
    return NULL;
}

static void refresh_processes_callback(void *user_data)
{
    gthread_t thread;
    if (glps_thread_create(&thread, NULL, refresh_processes_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void process_selection_callback(int index, void *user_data)
{
    if (index <= 0)
        return;

    glps_thread_mutex_lock(&process_mutex);
    if (index - 1 < process_count)
    {
        selected_process_pid = process_pids[index - 1];
        printf("Selected PID: %d - %s\n", selected_process_pid, process_names[index - 1]);
        if (kill_process_btn)
            GooeyWidget_MakeVisible(kill_process_btn, true);
    }
    glps_thread_mutex_unlock(&process_mutex);
}

static void kill_process_callback(void *user_data)
{
    if (selected_process_pid <= 0)
        return;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "kill -9 %d 2>/dev/null", selected_process_pid);
    if (system(cmd) != 0)
    {
        printf("Error: Failed to kill process %d. Ensure you have permission.\n", selected_process_pid);
    }

    selected_process_pid = -1;
    if (kill_process_btn)
        GooeyWidget_MakeVisible(kill_process_btn, false);
    refresh_processes_callback(NULL);
}

static void *refresh_storage_thread(void *arg)
{
    storage_count = 0;
    if (storage_list)
        GooeyList_ClearItems(storage_list);

    FILE *fp = popen("df -h 2>/dev/null | grep -E '^/dev/'", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp) && storage_count < 16)
        {
            trim_whitespace(line);
            char filesystem[32], size[16], used[16], avail[16], use[8], mount[128];
            if (sscanf(line, "%31s %15s %15s %15s %7s %127[^\n]", filesystem, size, used, avail, use, mount) == 6)
            {
                safe_string_copy(storage_filesystems[storage_count], filesystem, sizeof(storage_filesystems[storage_count]));
                safe_string_copy(storage_sizes[storage_count], size, sizeof(storage_sizes[storage_count]));
                safe_string_copy(storage_used[storage_count], used, sizeof(storage_used[storage_count]));
                safe_string_copy(storage_available[storage_count], avail, sizeof(storage_available[storage_count]));
                safe_string_copy(storage_mounts[storage_count], mount, sizeof(storage_mounts[storage_count]));

                char entry[256];
                snprintf(entry, sizeof(entry), "%s: %s used (%s free) on %s",
                         filesystem, used, avail, mount);

                if (storage_list)
                    GooeyList_AddItem(storage_list, "", entry);

                storage_count++;
            }
        }
        pclose(fp);
    }

    if (storage_count == 0 && storage_list)
    {
        GooeyList_AddItem(storage_list, "", "No storage devices found");
    }

    return NULL;
}

static void refresh_storage_callback(void *user_data)
{
    gthread_t thread;
    if (glps_thread_create(&thread, NULL, refresh_storage_thread, NULL) == 0)
    {
        glps_thread_detach(thread);
    }
}

static void create_system_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 200;

    GooeyImage *gooeyde_logo = GooeyImage_Create("/usr/local/share/gooeyde/assets/gooeyde_logo.png", 50, y_ref - 128, 128, 128, NULL, NULL);
    if (!gooeyde_logo)
        return;

    char hostname[256] = "Unknown";
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        safe_string_copy(hostname, "Unknown", sizeof(hostname));
    }
    GooeyLabel *host_label = GooeyLabel_Create("Hostname:", 24.0f, 50, y_ref + 60);
    host_value = GooeyLabel_Create(hostname, 24.0f, 200, y_ref + 60);

    char *username = getenv("USER");
    GooeyLabel *user_label = GooeyLabel_Create("Username:", 24.0f, 50, y_ref + 100);
    user_value = GooeyLabel_Create(username ? username : "Unknown", 24.0f, 200, y_ref + 100);

    char os_name[256] = "Unknown";
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0)
            {
                char *start = strchr(line, '\"');
                char *end = strrchr(line, '\"');
                if (start && end && end > start)
                {
                    size_t len = end - start - 1;
                    if (len < sizeof(os_name))
                    {
                        strncpy(os_name, start + 1, len);
                        os_name[len] = '\0';
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *os_label = GooeyLabel_Create("OS:", 24.0f, 50, y_ref + 140);
    os_value = GooeyLabel_Create(os_name, 24.0f, 200, y_ref + 140);

    char cpu_model[256] = "Unknown";
    fp = fopen("/proc/cpuinfo", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "model name", 10) == 0)
            {
                char *colon = strchr(line, ':');
                if (colon && strlen(colon) > 2)
                {
                    safe_string_copy(cpu_model, colon + 2, sizeof(cpu_model));
                    trim_whitespace(cpu_model);
                }
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *cpu_label = GooeyLabel_Create("CPU:", 24.0f, 50, y_ref + 180);
    cpu_value = GooeyLabel_Create(cpu_model, 24.0f, 200, y_ref + 180);

    char ram_info[64] = "Unknown";
    fp = fopen("/proc/meminfo", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "MemTotal:", 9) == 0)
            {
                char *mem = line + 9;
                while (*mem == ' ')
                    mem++;
                long kb = atol(mem);
                if (kb > 0)
                {
                    snprintf(ram_info, sizeof(ram_info), "%.2f GB", kb / 1024.0 / 1024.0);
                }
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *ram_label = GooeyLabel_Create("RAM:", 24.0f, 50, y_ref + 220);
    ram_value = GooeyLabel_Create(ram_info, 24.0f, 200, y_ref + 220);

    char kernel_version[128] = "Unknown";
    fp = popen("uname -r 2>/dev/null", "r");
    if (fp)
    {
        if (fgets(kernel_version, sizeof(kernel_version), fp))
        {
            trim_whitespace(kernel_version);
        }
        pclose(fp);
    }
    GooeyLabel *kernel_label = GooeyLabel_Create("Kernel:", 24.0f, 50, y_ref + 260);
    kernel_value = GooeyLabel_Create(kernel_version, 24.0f, 200, y_ref + 260);

    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)gooeyde_logo);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)host_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)host_value);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)user_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)user_value);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)os_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)os_value);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)cpu_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)cpu_value);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)ram_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)ram_value);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)kernel_label);
    GooeyTabs_AddWidget(win, settings_tabs, 0, (GooeyWidget *)kernel_value);
}

static void create_audio_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 130;

    audio_header = GooeyCanvas_Create(0, 0, window_width, 80, NULL, NULL);
    GooeyCanvas_DrawRectangle(audio_header, 0, 0, window_width, 80, win->active_theme->widget_base, true, 0.0f, false, 0.0f);
    GooeyLabel *section_title = GooeyLabel_Create("Settings/Audio", 24.0f, 50, 70);
    GooeyLabel *master_label = GooeyLabel_Create("Master Volume:", 18.0f, 50, y_ref + 10);
    master_volume_slider = GooeySlider_Create(300, y_ref, window_width - 400, 50, 100, false, master_volume_callback, NULL);

    GooeyLabel *mute_label = GooeyLabel_Create("Mute:", 18.0f, 50, y_ref + 70);
    mute_switch = GooeySwitch_Create(150, y_ref + 45, is_muted, 0, mute_switch_callback, NULL);

    GooeyLabel *device_label = GooeyLabel_Create("Audio Device:", 18.0f, 50, y_ref + 140);
    audio_devices_dropdown = GooeyDropdown_Create(210, y_ref + 115, window_width - 450, 40, NULL, 0, audio_device_selection_callback, NULL);

    refresh_audio_btn = GooeyButton_Create("Refresh Devices", window_width - 220, y_ref + 115, 80, 40, refresh_audio_callback, NULL);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)section_title);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)audio_header);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)master_label);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)master_volume_slider);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)mute_label);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)mute_switch);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)device_label);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)audio_devices_dropdown);
    GooeyTabs_AddWidget(win, settings_tabs, 3, (GooeyWidget *)refresh_audio_btn);

    refresh_audio_callback(NULL);
}

static void create_process_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 50;

    process_header = GooeyCanvas_Create(0, 0, window_width, 80, NULL, NULL);
    GooeyCanvas_DrawRectangle(process_header, 0, 0, window_width, 80, win->active_theme->widget_base, true, 0.0f, false, 0.0f);
    GooeyLabel *section_title = GooeyLabel_Create("Settings/Processes", 24.0f, 50, 70);

    refresh_processes_btn = GooeyButton_Create("Refresh", window_width - 210, y_ref - 25, 100, 40, refresh_processes_callback, NULL);
    kill_process_btn = GooeyButton_Create("Kill Process", window_width - 340, y_ref - 25, 120, 40, kill_process_callback, NULL);
    if (kill_process_btn)
        GooeyWidget_MakeVisible(kill_process_btn, false);

    process_list = GooeyList_Create(0, y_ref + 30, window_width - 50, window_height - 150, process_selection_callback, NULL);

    GooeyTabs_AddWidget(win, settings_tabs, 4, (GooeyWidget *)process_header);
    GooeyTabs_AddWidget(win, settings_tabs, 4, (GooeyWidget *)section_title);
    GooeyTabs_AddWidget(win, settings_tabs, 4, (GooeyWidget *)refresh_processes_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 4, (GooeyWidget *)kill_process_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 4, (GooeyWidget *)process_list);

    refresh_processes_callback(NULL);
}

static void create_storage_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 50;

    storage_header = GooeyCanvas_Create(0, 0, window_width, 80, NULL, NULL);
    GooeyCanvas_DrawRectangle(storage_header, 0, 0, window_width, 80, win->active_theme->widget_base, true, 0.0f, false, 0.0f);
    GooeyLabel *section_title = GooeyLabel_Create("Settings/Storage",24.0f, 50, 70);

    refresh_storage_btn = GooeyButton_Create("Refresh", window_width - 160, y_ref - 30, 100, 40, refresh_storage_callback, NULL);

    storage_list = GooeyList_Create(0, y_ref + 30, window_width - 50, window_height - 150, NULL, NULL);

    GooeyTabs_AddWidget(win, settings_tabs, 5, (GooeyWidget *)storage_header);
    GooeyTabs_AddWidget(win, settings_tabs, 5, (GooeyWidget *)section_title);
    GooeyTabs_AddWidget(win, settings_tabs, 5, (GooeyWidget *)refresh_storage_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 5, (GooeyWidget *)storage_list);

    refresh_storage_callback(NULL);
}

static void create_network_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 50;
    wifi_header = GooeyCanvas_Create(0, 0, window_width, 90, NULL, NULL);
    GooeyCanvas_DrawRectangle(wifi_header, 0, 0, window_width, 90, win->active_theme->widget_base, true, 0.0f, false, 0.0f);
    GooeyLabel *section_title = GooeyLabel_Create("Settings/Wi-Fi", 24.0f, 50, 70);
    is_wifi_enabled = get_wifi_enabled();
    wifi_switch = GooeySwitch_Create(200, y_ref - 25, is_wifi_enabled, 0, wifi_switch_callback, NULL);

    refresh_wifi_btn = GooeyButton_Create("Refresh", window_width - 160, y_ref - 10, 100, 40, refresh_button_callback, NULL);
    disconnect_wifi_btn = GooeyButton_Create("Disconnect", window_width - 290, y_ref - 10, 120, 40, disconnect_wifi_callback, NULL);

    wifi_status_label = GooeyLabel_Create("Checking status...", 18.0f, window_width - 350, y_ref - 20);

    wifi_list = GooeyList_Create(0, y_ref + 40, window_width - 5, window_height - 150, connect_to_network_callback, NULL);

    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_header);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)section_title);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_switch);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)refresh_wifi_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)disconnect_wifi_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_status_label);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_list);

    if (is_wifi_enabled)
    {
        if (wifi_list)
            GooeyWidget_MakeVisible(wifi_list, true);
        if (refresh_wifi_btn)
            GooeyWidget_MakeVisible(refresh_wifi_btn, true);
        refresh_wifi_list();
    }
    else
    {
        if (wifi_list)
        {
            GooeyList_AddItem(wifi_list, "", "Wi-Fi is disabled");
            GooeyWidget_MakeVisible(wifi_list, false);
        }
        if (refresh_wifi_btn)
            GooeyWidget_MakeVisible(refresh_wifi_btn, false);
    }

    update_wifi_status();
}

static void create_bluetooth_ui()
{
    if (!settings_tabs || !win)
        return;

    int y_ref = 50;
    bluetooth_header = GooeyCanvas_Create(0, 0, window_width, 90, NULL, NULL);
    GooeyCanvas_DrawRectangle(bluetooth_header, 0, 0, window_width, 90, win->active_theme->widget_base, true, 0.0f, false, 0.0f);
    GooeyLabel *section_title = GooeyLabel_Create("Settings/Bluetooth", 24.0f, 50, 70);

    is_bt_enabled = get_bluetooth_enabled();
    bluetooth_switch = GooeySwitch_Create(250, y_ref - 25, is_bt_enabled, 0, bluetooth_switch_callback, NULL);

    refresh_bluetooth_btn = GooeyButton_Create("Scan for devices...", window_width - 210, y_ref - 10, 100, 40, refresh_bluetooth_button_callback, NULL);

    bt_status_label = GooeyLabel_Create("Checking status...", 18.0f, window_width - 350, y_ref - 20);

    bluetooth_list = GooeyList_Create(0, y_ref + 35, window_width - 50, window_height - 150, connect_to_bluetooth_callback, NULL);

    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bluetooth_header);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)section_title);

    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bluetooth_switch);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)refresh_bluetooth_btn);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bt_status_label);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bluetooth_list);

    if (is_bt_enabled)
    {
        if (bluetooth_list)
            GooeyWidget_MakeVisible(bluetooth_list, true);
        if (refresh_bluetooth_btn)
            GooeyWidget_MakeVisible(refresh_bluetooth_btn, true);
        refresh_bluetooth_list();
    }
    else
    {
        if (bluetooth_list)
        {
            GooeyList_AddItem(bluetooth_list, "", "Bluetooth is disabled");
            GooeyWidget_MakeVisible(bluetooth_list, false);
        }
        if (refresh_bluetooth_btn)
            GooeyWidget_MakeVisible(refresh_bluetooth_btn, false);
    }

    update_bluetooth_status();
}

int main(int argc, char **argv)
{
    if (!check_system_commands())
    {
        fprintf(stderr, "Warning: Some system commands are missing. Functionality may be limited.\n");
    }

    if (Gooey_Init() != 0)
    {
        fprintf(stderr, "Failed to initialize Gooey library\n");
        return 1;
    }

    glps_thread_mutex_init(&wifi_mutex, NULL);
    glps_thread_mutex_init(&bt_mutex, NULL);
    glps_thread_mutex_init(&process_mutex, NULL);
    glps_thread_mutex_init(&audio_mutex, NULL);

    ScreenInfo screen_info = get_screen_resolution();
    window_width = screen_info.width;
    window_height = screen_info.height - 50;

    win = GooeyWindow_Create("Gooey Settings", 0, 0, window_width, window_height, true);
    if (!win)
    {
        fprintf(stderr, "Failed to create main window\n");
        cleanup_resources();
        return 1;
    }

    GooeyWindow_MakeTransparent(win, 15, 0.9f);

    GooeyTheme *dark_theme = GooeyTheme_LoadFromFile("/usr/local/share/gooeyde/assets/dark.json");
    if (dark_theme)
    {
        GooeyWindow_SetTheme(win, dark_theme);
    }
    else
    {
        printf("Warning: Failed to load dark theme\n");
    }

    win->vk = NULL;

    settings_tabs = GooeyTabs_Create(0, 0, window_width, window_height, true);
    if (!settings_tabs)
    {
        fprintf(stderr, "Failed to create tabs\n");
        cleanup_resources();
        return 1;
    }

    settings_tabs->is_sidebar = 0;

    GooeyTabs_InsertTab(settings_tabs, "System");
    GooeyTabs_InsertTab(settings_tabs, "Wi-Fi");
    GooeyTabs_InsertTab(settings_tabs, "Bluetooth");
    GooeyTabs_InsertTab(settings_tabs, "Audio");
    GooeyTabs_InsertTab(settings_tabs, "Processes");
    GooeyTabs_InsertTab(settings_tabs, "Storage");

    GooeyWindow_RegisterWidget(win, (GooeyWidget *)settings_tabs);

    create_system_ui();
    create_network_ui();
    create_bluetooth_ui();
    create_audio_ui();
    create_process_ui();
    create_storage_ui();

    GooeyWindow_Run(1, win);

    cleanup_resources();
    return 0;
}