#include "gooey.h"
#include "utils/resolution_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <GLPS/glps_thread.h>

GooeyTabs *settings_tabs = NULL;
GooeyWindow *win = NULL;
GooeySwitch *wifi_switch = NULL;
GooeySwitch *bluetooth_switch = NULL;
GooeyList *wifi_list = NULL;
GooeyList *bluetooth_list = NULL;
GooeyButton *refresh_wifi_btn = NULL;
GooeyButton *refresh_bluetooth_btn = NULL;

#define MAX_WIFI_NETWORKS 128
#define MAX_BT_DEVICES 128

static char wifi_ssids[MAX_WIFI_NETWORKS][128];
static int wifi_count = 0;
static gthread_mutex_t wifi_mutex;
static bool wifi_scanning = false;

static char bt_devices[MAX_BT_DEVICES][128];
static char bt_addresses[MAX_BT_DEVICES][64];
static int bt_count = 0;
static gthread_mutex_t bt_mutex;
static bool bt_scanning = false;

static void *wifi_scan_thread(void *arg)
{
    glps_thread_mutex_lock(&wifi_mutex);
    wifi_scanning = true;
    wifi_count = 0;
    glps_thread_mutex_unlock(&wifi_mutex);

    FILE *fp = popen("nmcli -t -f SSID,SIGNAL dev wifi", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            glps_thread_mutex_lock(&wifi_mutex);
            if (wifi_count >= MAX_WIFI_NETWORKS)
            {
                glps_thread_mutex_unlock(&wifi_mutex);
                break;
            }

            char ssid[128] = {0};
            char signal[32] = {0};
            char *sep = strchr(line, ':');
            if (sep)
            {
                size_t ssid_len = sep - line;
                strncpy(ssid, line, ssid_len);
                ssid[ssid_len] = '\0';
                strncpy(signal, sep + 1, sizeof(signal) - 1);
                signal[strcspn(signal, "\n")] = 0;

                char entry[160];
                snprintf(entry, sizeof(entry), "%s (Signal: %s%%)",
                         ssid[0] ? ssid : "<Hidden>", signal);

                GooeyList_AddItem(wifi_list, "", entry);
                strncpy(wifi_ssids[wifi_count], ssid[0] ? ssid : "<Hidden>",
                        sizeof(wifi_ssids[wifi_count]) - 1);
                wifi_ssids[wifi_count][sizeof(wifi_ssids[wifi_count]) - 1] = '\0';
                wifi_count++;
            }
            glps_thread_mutex_unlock(&wifi_mutex);
        }
        pclose(fp);
    }

    glps_thread_mutex_lock(&wifi_mutex);
    wifi_scanning = false;
    if (wifi_count == 0)
    {
        GooeyList_AddItem(wifi_list, "", "No networks found");
    }
    glps_thread_mutex_unlock(&wifi_mutex);

    return NULL;
}

static void refresh_wifi_list()
{
    glps_thread_mutex_lock(&wifi_mutex);
    if (wifi_scanning)
    {
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }
    glps_thread_mutex_unlock(&wifi_mutex);

    GooeyList_ClearItems(wifi_list);
    GooeyList_AddItem(wifi_list, "", "Scanning...");

    gthread_t thread;
    glps_thread_create(&thread, NULL, wifi_scan_thread, NULL);
    glps_thread_detach(thread);
}

static void *connect_to_network_thread(void *arg)
{
    char *ssid = (char *)arg;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "nmcli dev wifi connect \"%s\"", ssid);
    system(cmd);
    free(ssid);
    return NULL;
}

static void connect_to_network_callback(int index)
{
    glps_thread_mutex_lock(&wifi_mutex);
    if (index < 0 || index >= wifi_count)
    {
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }

    const char *ssid = wifi_ssids[index];
    if (strcmp(ssid, "<Hidden>") == 0 || strlen(ssid) == 0)
    {
        glps_thread_mutex_unlock(&wifi_mutex);
        return;
    }

    char *ssid_copy = strdup(ssid);
    glps_thread_mutex_unlock(&wifi_mutex);

    gthread_t thread;
    glps_thread_create(&thread, NULL, connect_to_network_thread, ssid_copy);
    glps_thread_detach(thread);
}

static void *wifi_toggle_thread(void *arg)
{
    bool enabled = *(bool *)arg;
    free(arg);

    if (enabled)
    {
        system("nmcli radio wifi on");
    }
    else
    {
        system("nmcli radio wifi off");
    }
    return NULL;
}

static void wifi_switch_callback(bool enabled)
{
    bool *enabled_ptr = malloc(sizeof(bool));
    *enabled_ptr = enabled;

    gthread_t thread;
    glps_thread_create(&thread, NULL, wifi_toggle_thread, enabled_ptr);
    glps_thread_detach(thread);

    if (enabled)
    {
        GooeyWidget_MakeVisible(wifi_list, true);
        GooeyWidget_MakeVisible(refresh_wifi_btn, true);
        refresh_wifi_list();
    }
    else
    {
        GooeyWidget_MakeVisible(wifi_list, false);
        GooeyWidget_MakeVisible(refresh_wifi_btn, false);
    }
}

static bool get_wifi_enabled()
{
    FILE *fp = popen("nmcli radio wifi", "r");
    if (!fp)
        return false;
    char state[16] = {0};
    if (fgets(state, sizeof(state), fp))
    {
        pclose(fp);
        return (strncmp(state, "enabled", 7) == 0);
    }
    pclose(fp);
    return false;
}

static void refresh_button_callback()
{
    refresh_wifi_list();
}

static void *bluetooth_scan_thread(void *arg)
{
    glps_thread_mutex_lock(&bt_mutex);
    bt_scanning = true;
    bt_count = 0;
    glps_thread_mutex_unlock(&bt_mutex);

    system("bluetoothctl --timeout 10 scan on > /dev/null 2>&1 &");
    sleep(1);

    FILE *fp = popen("bluetoothctl devices", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
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
                    strncpy(address, addr_start, addr_len);
                    address[addr_len] = '\0';

                    char *name_start = addr_end + 1;
                    strncpy(name, name_start, sizeof(name) - 1);
                    name[strcspn(name, "\n")] = 0;

                    char entry[192];
                    snprintf(entry, sizeof(entry), "%s (%s)",
                             name[0] ? name : "Unknown Device", address);

                    GooeyList_AddItem(bluetooth_list, "", entry);
                    strncpy(bt_devices[bt_count], name, sizeof(bt_devices[bt_count]) - 1);
                    bt_devices[bt_count][sizeof(bt_devices[bt_count]) - 1] = '\0';
                    strncpy(bt_addresses[bt_count], address, sizeof(bt_addresses[bt_count]) - 1);
                    bt_addresses[bt_count][sizeof(bt_addresses[bt_count]) - 1] = '\0';
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
    if (bt_count == 0)
    {
        GooeyList_AddItem(bluetooth_list, "", "No devices found");
    }
    glps_thread_mutex_unlock(&bt_mutex);

    return NULL;
}

static void refresh_bluetooth_list()
{
    glps_thread_mutex_lock(&bt_mutex);
    if (bt_scanning)
    {
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }
    glps_thread_mutex_unlock(&bt_mutex);

    GooeyList_ClearItems(bluetooth_list);
    GooeyList_AddItem(bluetooth_list, "", "Scanning...");

    gthread_t thread;
    glps_thread_create(&thread, NULL, bluetooth_scan_thread, NULL);
    glps_thread_detach(thread);
}

static void *connect_to_bluetooth_thread(void *arg)
{
    char *address = (char *)arg;
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "bluetoothctl trust %s", address);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "bluetoothctl pair %s", address);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "bluetoothctl connect %s", address);
    system(cmd);

    free(address);
    return NULL;
}

static void connect_to_bluetooth_callback(int index)
{
    glps_thread_mutex_lock(&bt_mutex);
    if (index < 0 || index >= bt_count)
    {
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }

    const char *address = bt_addresses[index-1];
    if (strlen(address) == 0)
    {
        glps_thread_mutex_unlock(&bt_mutex);
        return;
    }

    char *addr_copy = strdup(address);
    glps_thread_mutex_unlock(&bt_mutex);

    gthread_t thread;
    glps_thread_create(&thread, NULL, connect_to_bluetooth_thread, addr_copy);
    glps_thread_detach(thread);
}

static void *bluetooth_toggle_thread(void *arg)
{
    bool enabled = *(bool *)arg;
    free(arg);

    if (enabled)
    {
        system("bluetoothctl power on");
    }
    else
    {
        system("bluetoothctl power off");
    }
    return NULL;
}

static void bluetooth_switch_callback(bool enabled)
{
    bool *enabled_ptr = malloc(sizeof(bool));
    *enabled_ptr = enabled;

    gthread_t thread;
    glps_thread_create(&thread, NULL, bluetooth_toggle_thread, enabled_ptr);
    glps_thread_detach(thread);

    if (enabled)
    {
        GooeyWidget_MakeVisible(bluetooth_list, true);
        GooeyWidget_MakeVisible(refresh_bluetooth_btn, true);
        refresh_bluetooth_list();
    }
    else
    {
        GooeyWidget_MakeVisible(bluetooth_list, false);
        GooeyWidget_MakeVisible(refresh_bluetooth_btn, false);
    }
}

static bool get_bluetooth_enabled()
{
    FILE *fp = popen("bluetoothctl show | grep 'Powered:' | awk '{print $2}'", "r");
    if (!fp)
        return false;
    char state[16] = {0};
    if (fgets(state, sizeof(state), fp))
    {
        pclose(fp);
        return (strncmp(state, "yes", 3) == 0);
    }
    pclose(fp);
    return false;
}

static void refresh_bluetooth_button_callback()
{
    refresh_bluetooth_list();
}

static void create_system_ui()
{
    int y_ref = 200;

    GooeyImage *gooeyde_logo = GooeyImage_Create("assets/gooeyde_logo.png", 50, y_ref - 128, 128, 128, NULL);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    GooeyLabel *host_label = GooeyLabel_Create("Hostname:", 0.5f, 50, y_ref + 60);
    GooeyLabel *host_value = GooeyLabel_Create(hostname, 0.5f, 200, y_ref + 60);

    char *username = getenv("USER");
    GooeyLabel *user_label = GooeyLabel_Create("Username:", 0.5f, 50, y_ref + 100);
    GooeyLabel *user_value = GooeyLabel_Create(username ? username : "Unknown", 0.5f, 200, y_ref + 100);

    FILE *fp = fopen("/etc/os-release", "r");
    char os_name[256] = "Unknown";
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
                    strncpy(os_name, start + 1, len);
                    os_name[len] = '\0';
                }
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *os_label = GooeyLabel_Create("OS:", 0.5f, 50, y_ref + 140);
    GooeyLabel *os_value = GooeyLabel_Create(os_name, 0.5f, 200, y_ref + 140);

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
                if (colon)
                {
                    strncpy(cpu_model, colon + 2, sizeof(cpu_model) - 1);
                    cpu_model[strcspn(cpu_model, "\n")] = 0;
                }
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *cpu_label = GooeyLabel_Create("CPU:", 0.5f, 50, y_ref + 180);
    GooeyLabel *cpu_value = GooeyLabel_Create(cpu_model, 0.5f, 200, y_ref + 180);

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
                snprintf(ram_info, sizeof(ram_info), "%.2f GB", kb / 1024.0 / 1024.0);
                break;
            }
        }
        fclose(fp);
    }
    GooeyLabel *ram_label = GooeyLabel_Create("RAM:", 0.5f, 50, y_ref + 220);
    GooeyLabel *ram_value = GooeyLabel_Create(ram_info, 0.5f, 200, y_ref + 220);

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
}

static void create_network_ui()
{
    int y_ref = 50;

    GooeyLabel *wifi_label = GooeyLabel_Create("Wi-Fi:", 0.5f, 50, y_ref + 10);
    bool wifi_enabled = get_wifi_enabled();
    wifi_switch = GooeySwitch_Create(150, y_ref - 15, wifi_enabled, 0, wifi_switch_callback);

    refresh_wifi_btn = GooeyButton_Create("Refresh", 250, y_ref - 20, 100, 40, refresh_button_callback);

    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_label);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_switch);
    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)refresh_wifi_btn);

    wifi_list = GooeyList_Create(0, y_ref + 40, 650, 510, connect_to_network_callback);

    if (wifi_enabled)
    {
        GooeyWidget_MakeVisible(wifi_list, true);
        GooeyWidget_MakeVisible(refresh_wifi_btn, true);
        refresh_wifi_list();
    }
    else
    {
        GooeyList_AddItem(wifi_list, "", "Wi-Fi is disabled");
        GooeyWidget_MakeVisible(wifi_list, false);
        GooeyWidget_MakeVisible(refresh_wifi_btn, false);
    }

    GooeyTabs_AddWidget(win, settings_tabs, 1, (GooeyWidget *)wifi_list);
}

static void create_bluetooth_ui()
{
    int y_ref = 50;

    GooeyLabel *bt_label = GooeyLabel_Create("Bluetooth:", 0.5f, 50, y_ref + 10);
    bool bt_enabled = get_bluetooth_enabled();
    bluetooth_switch = GooeySwitch_Create(200, y_ref - 15, bt_enabled, 0, bluetooth_switch_callback);

    refresh_bluetooth_btn = GooeyButton_Create("Scan", 300, y_ref - 20, 100, 40, refresh_bluetooth_button_callback);

    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bt_label);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bluetooth_switch);
    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)refresh_bluetooth_btn);

    bluetooth_list = GooeyList_Create(0, y_ref + 40, 650, 510, connect_to_bluetooth_callback);

    if (bt_enabled)
    {
        GooeyWidget_MakeVisible(bluetooth_list, true);
        GooeyWidget_MakeVisible(refresh_bluetooth_btn, true);
        refresh_bluetooth_list();
    }
    else
    {
        GooeyList_AddItem(bluetooth_list, "", "Bluetooth is disabled");
        GooeyWidget_MakeVisible(bluetooth_list, false);
        GooeyWidget_MakeVisible(refresh_bluetooth_btn, false);
    }

    GooeyTabs_AddWidget(win, settings_tabs, 2, (GooeyWidget *)bluetooth_list);
}

int main(int argc, char **argv)
{
    Gooey_Init();

    glps_thread_mutex_init(&wifi_mutex, NULL);
    glps_thread_mutex_init(&bt_mutex, NULL);

    ScreenInfo screen_info = get_screen_resolution();

    win = GooeyWindow_Create("Gooey Settings", 800, 600, true);
    GooeyTheme *dark_mode = GooeyTheme_LoadFromFile("assets/dark.json");
    GooeyWindow_SetTheme(win, dark_mode);
    if (!win)
    {
        fprintf(stderr, "Failed to create main window\n");
        glps_thread_mutex_destroy(&wifi_mutex);
        glps_thread_mutex_destroy(&bt_mutex);
        return 1;
    }

    win->vk = NULL;

    settings_tabs = GooeyTabs_Create(0, 0, 800, 600, true);
    GooeyTabs_Sidebar_Open(settings_tabs);

    GooeyTabs_InsertTab(settings_tabs, "System");
    GooeyTabs_InsertTab(settings_tabs, "Wi-Fi");
    GooeyTabs_InsertTab(settings_tabs, "Bluetooth");

    GooeyWindow_RegisterWidget(win, (GooeyWidget *)settings_tabs);

    create_system_ui();
    create_network_ui();
    create_bluetooth_ui();

    GooeyWindow_Run(1, win);

    GooeyWindow_Cleanup(1, win);
    glps_thread_mutex_destroy(&wifi_mutex);
    glps_thread_mutex_destroy(&bt_mutex);

    return 0;
}