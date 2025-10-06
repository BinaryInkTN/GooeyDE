#include <gooey.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    bool wifi_enabled;
    bool bluetooth_enabled;
    long brightness_level;
    long volume_level;
    char current_network[64];
} AppSettings;

static AppSettings settings;

static GooeySwitch *wifi_switch = NULL;
static GooeySwitch *bluetooth_switch = NULL;
static GooeySlider *brightness_slider = NULL;
static GooeySlider *volume_slider = NULL;
static GooeyList *network_list = NULL;
static GooeySwitch *mute_switch = NULL;

void execute_system_command(const char *command)
{
    system(command);
}

long get_system_brightness()
{
    FILE *fp = popen("cat /sys/class/backlight/*/brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        long brightness = 50;
        fscanf(fp, "%ld", &brightness);
        pclose(fp);
        return brightness;
    }
    return 50;
}

long get_max_brightness()
{
    FILE *fp = popen("cat /sys/class/backlight/*/max_brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        long max_brightness = 100;
        fscanf(fp, "%ld", &max_brightness);
        pclose(fp);
        return max_brightness;
    }
    return 100;
}

long get_system_volume()
{
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+(?=%)' | head -1", "r");
    if (fp)
    {
        long volume = 50;
        fscanf(fp, "%ld", &volume);
        pclose(fp);
        return volume;
    }
    return 50;
}

bool get_system_wifi_state()
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
    return false;
}

bool get_system_bluetooth_state()
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
    return false;
}

bool get_system_mute_state()
{
    FILE *fp = popen("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -q 'Mute: yes' && echo muted", "r");
    if (fp)
    {
        char state[16];
        if (fgets(state, sizeof(state), fp))
        {
            pclose(fp);
            return strstr(state, "muted") != NULL;
        }
        pclose(fp);
    }
    return false;
}

void get_current_network()
{
    FILE *fp = popen("nmcli -t -f NAME connection show --active 2>/dev/null | head -1", "r");
    if (fp)
    {
        char network[64];
        if (fgets(network, sizeof(network), fp))
        {
            network[strcspn(network, "\n")] = 0;
            strncpy(settings.current_network, network, sizeof(settings.current_network) - 1);
        }
        else
        {
            strcpy(settings.current_network, "Not connected");
        }
        pclose(fp);
    }
    else
    {
        strcpy(settings.current_network, "Not connected");
    }
}

void scan_wifi_networks()
{
    if (network_list)
    {
        GooeyList_ClearItems(network_list);

        FILE *fp = popen("nmcli -t -f SSID dev wifi list 2>/dev/null", "r");
        if (fp)
        {
            char ssid[128];
            int count = 0;
            while (fgets(ssid, sizeof(ssid), fp) && count < 10)
            {
                ssid[strcspn(ssid, "\n")] = 0;
                if (strlen(ssid) > 0)
                {
                    GooeyList_AddItem(network_list, ssid, "Click to connect");
                    count++;
                }
            }
            pclose(fp);
        }

        if (network_list->item_count == 0)
        {
            GooeyList_AddItem(network_list, "No networks found", "Enable Wi-Fi to scan");
        }

        GooeyList_ShowSeparator(network_list, true);
    }
}

void init_system_settings()
{

    long current_bright = get_system_brightness();
    long max_bright = get_max_brightness();
    settings.brightness_level = (current_bright * 100) / max_bright;

    settings.volume_level = get_system_volume();

    settings.wifi_enabled = get_system_wifi_state();

    settings.bluetooth_enabled = get_system_bluetooth_state();

    get_current_network();

    printf("System settings initialized:\n");
    printf("  Brightness: %ld%%\n", settings.brightness_level);
    printf("  Volume: %ld%%\n", settings.volume_level);
    printf("  WiFi: %s\n", settings.wifi_enabled ? "Enabled" : "Disabled");
    printf("  Bluetooth: %s\n", settings.bluetooth_enabled ? "Enabled" : "Disabled");
    printf("  Network: %s\n", settings.current_network);
}

void on_wifi_toggled(bool state)
{
    settings.wifi_enabled = state;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "nmcli radio wifi %s", state ? "on" : "off");
    execute_system_command(cmd);
    printf("Wi-Fi %s\n", state ? "Enabled" : "Disabled");

    if (state)
    {

        scan_wifi_networks();
    }
}

void on_bluetooth_toggled(bool state)
{
    settings.bluetooth_enabled = state;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "bluetoothctl power %s", state ? "on" : "off");
    execute_system_command(cmd);
    printf("Bluetooth %s\n", state ? "Enabled" : "Disabled");
}

void on_brightness_changed(long value)
{
    settings.brightness_level = value;
    long max_bright = get_max_brightness();
    long actual_level = (value * max_bright) / 100;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %ld | sudo tee /sys/class/backlight/*/brightness >/dev/null 2>&1", actual_level);
    execute_system_command(cmd);
    printf("Brightness set to: %ld%%\n", value);
}

void on_volume_changed(long value)
{
    settings.volume_level = value;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %ld%%", value);
    execute_system_command(cmd);
    printf("Volume set to: %ld%%\n", value);
}

void on_mute_toggled(bool state)
{
    execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    printf("Audio %s\n", state ? "Unmuted" : "Muted");
}

void on_network_selected(int index)
{
    if (network_list && settings.wifi_enabled)
    {
        printf("Connecting to network index: %d\n", index);
    }
}

void on_about_clicked()
{
    printf("Linux Settings App - Controls real system settings\n");
}

void on_save_clicked()
{
    printf("All settings applied to system!\n");
}

void on_refresh_clicked()
{
    printf("Refreshing settings from system...\n");
    init_system_settings();

    if (wifi_switch && GooeySwitch_GetState(wifi_switch) != settings.wifi_enabled)
    {
        GooeySwitch_Toggle(wifi_switch);
    }
    if (bluetooth_switch && GooeySwitch_GetState(bluetooth_switch) != settings.bluetooth_enabled)
    {
        GooeySwitch_Toggle(bluetooth_switch);
    }
    if (brightness_slider)
    {
        brightness_slider->value = settings.brightness_level;
    }
    if (volume_slider)
    {
        volume_slider->value = settings.volume_level;
    }
    if (mute_switch && GooeySwitch_GetState(mute_switch) != get_system_mute_state())
    {
        GooeySwitch_Toggle(mute_switch);
    }

    scan_wifi_networks();
    get_current_network();
}

void create_section_header(const char *text, int x, int y)
{

    GooeyButton_Create(text, x, y, 300, 25, NULL);
}

void create_modern_toggle(const char *label, int x, int y, GooeySwitch **switch_ptr, bool initial_state, void (*callback)(bool))
{
    create_section_header(label, x, y);
    *switch_ptr = GooeySwitch_Create(x + 320, y, initial_state, true, callback);
}

void create_modern_slider(const char *label, int x, int y, GooeySlider **slider_ptr, long initial_value, void (*callback)(long))
{
    create_section_header(label, x, y);
    *slider_ptr = GooeySlider_Create(x, y + 35, 400, 0, 100, true, callback);
    (*slider_ptr)->value = initial_value;

    char value_text[32];
    snprintf(value_text, sizeof(value_text), "%ld%%", initial_value);
    GooeyButton_Create(value_text, x + 420, y + 35, 60, 25, NULL);
}

void create_network_tab(GooeyWindow *win, GooeyTabs *tabs)
{
    int y_pos = 40;

    create_section_header("CONNECTIVITY", 50, y_pos);
    y_pos += 40;

    create_modern_toggle("Wi-Fi", 50, y_pos, &wifi_switch, settings.wifi_enabled, on_wifi_toggled);
    y_pos += 60;

    create_modern_toggle("Bluetooth", 50, y_pos, &bluetooth_switch, settings.bluetooth_enabled, on_bluetooth_toggled);
    y_pos += 80;

    create_section_header("AVAILABLE NETWORKS", 50, y_pos);
    y_pos += 30;

    network_list = GooeyList_Create(50, y_pos, 500, 200, on_network_selected);
    scan_wifi_networks();
    y_pos += 220;

    GooeyButton_Create("⟳ Refresh", 50, y_pos, 120, 35, on_refresh_clicked);

    GooeyTabs_AddWidget(win, tabs, 0, wifi_switch);
    GooeyTabs_AddWidget(win, tabs, 0, bluetooth_switch);
    GooeyTabs_AddWidget(win, tabs, 0, network_list);
}

void create_display_tab(GooeyWindow *win, GooeyTabs *tabs)
{
    int y_pos = 40;

    create_section_header("DISPLAY SETTINGS", 50, y_pos);
    y_pos += 40;

    create_modern_slider("SCREEN BRIGHTNESS", 50, y_pos, &brightness_slider, settings.brightness_level, on_brightness_changed);
    y_pos += 100;

    create_section_header("NIGHT LIGHT", 50, y_pos);
    y_pos += 30;
    GooeySwitch *night_light = GooeySwitch_Create(50, y_pos, false, true, NULL);
    y_pos += 60;

    create_section_header("AUTO ROTATE", 50, y_pos);
    y_pos += 30;
    GooeySwitch *auto_rotate = GooeySwitch_Create(50, y_pos, true, true, NULL);

    GooeyTabs_AddWidget(win, tabs, 1, brightness_slider);
    GooeyTabs_AddWidget(win, tabs, 1, night_light);
    GooeyTabs_AddWidget(win, tabs, 1, auto_rotate);
}

void create_sound_tab(GooeyWindow *win, GooeyTabs *tabs)
{
    int y_pos = 40;

    create_section_header("SOUND SETTINGS", 50, y_pos);
    y_pos += 40;

    create_modern_slider("SYSTEM VOLUME", 50, y_pos, &volume_slider, settings.volume_level, on_volume_changed);
    y_pos += 100;

    create_modern_toggle("MUTE AUDIO", 50, y_pos, &mute_switch, get_system_mute_state(), on_mute_toggled);
    y_pos += 80;

    create_section_header("SOUND EFFECTS", 50, y_pos);
    y_pos += 30;
    GooeySwitch *sound_effects = GooeySwitch_Create(50, y_pos, true, true, NULL);
    y_pos += 60;

    create_section_header("INPUT VOLUME", 50, y_pos);
    y_pos += 35;
    GooeySlider *input_volume = GooeySlider_Create(50, y_pos, 400, 0, 100, true, NULL);
    input_volume->value = 75;
    GooeyButton_Create("75%", 470, y_pos, 60, 25, NULL);

    GooeyTabs_AddWidget(win, tabs, 2, volume_slider);
    GooeyTabs_AddWidget(win, tabs, 2, mute_switch);
    GooeyTabs_AddWidget(win, tabs, 2, sound_effects);
    GooeyTabs_AddWidget(win, tabs, 2, input_volume);
}

void create_system_tab(GooeyWindow *win, GooeyTabs *tabs)
{
    int y_pos = 40;

    create_section_header("SYSTEM INFORMATION", 50, y_pos);
    y_pos += 40;

    GooeyList *system_list = GooeyList_Create(50, y_pos, 500, 180, NULL);
    GooeyList_AddItem(system_list, "Current Network", settings.current_network);
    GooeyList_AddItem(system_list, "Brightness Level", "Adjust in Display tab");
    GooeyList_AddItem(system_list, "Volume Level", "Adjust in Sound tab");
    GooeyList_AddItem(system_list, "Wi-Fi Status", settings.wifi_enabled ? "Enabled" : "Disabled");
    GooeyList_AddItem(system_list, "Bluetooth Status", settings.bluetooth_enabled ? "Enabled" : "Disabled");
    GooeyList_ShowSeparator(system_list, true);
    y_pos += 200;

    create_section_header("SYSTEM CONTROLS", 50, y_pos);
    y_pos += 40;

    GooeyButton_Create("ℹ️ About", 50, y_pos, 140, 45, on_about_clicked);
    GooeyButton_Create("⟳ Refresh All", 210, y_pos, 140, 45, on_refresh_clicked);
    GooeyButton_Create("💾 Apply", 370, y_pos, 140, 45, on_save_clicked);
    y_pos += 65;

    create_section_header("POWER MANAGEMENT", 50, y_pos);
    y_pos += 40;

    GooeyButton_Create("🔄 Restart", 50, y_pos, 120, 40, NULL);
    GooeyButton_Create("⏻ Shutdown", 190, y_pos, 120, 40, NULL);
    GooeyButton_Create("🌙 Suspend", 330, y_pos, 120, 40, NULL);

    GooeyTabs_AddWidget(win, tabs, 3, system_list);
}

int main()
{
    Gooey_Init();

    init_system_settings();

    GooeyWindow *win = GooeyWindow_Create("⚙️ Linux System Settings", 900, 700, true);

    GooeyTabs *tabs = GooeyTabs_Create(0, 0, 900, 700, false);

    GooeyTabs_InsertTab(tabs, "🌐 Network");
    GooeyTabs_InsertTab(tabs, "🖥️ Display");
    GooeyTabs_InsertTab(tabs, "🔊 Sound");
    GooeyTabs_InsertTab(tabs, "⚙️ System");

    create_network_tab(win, tabs);
    create_display_tab(win, tabs);
    create_sound_tab(win, tabs);
    create_system_tab(win, tabs);

    GooeyTabs_SetActiveTab(tabs, 0);

    printf("Linux Settings App Started\n");
    printf("Note: Some operations may require sudo permissions\n");

    GooeyWindow_Run(1, win);
    GooeyWindow_Cleanup(1, win);

    return 0;
}