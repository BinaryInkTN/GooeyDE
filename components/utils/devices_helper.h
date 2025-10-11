#ifndef DEVICES_HELPER_H
#define DEVICES_HELPER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>

/**
 * @brief Checks if required system commands are available
 * 
 * Verifies the presence of essential system commands (nmcli, bluetoothctl,
 * pactl, xrandr) needed for various device operations. Prints warnings for
 * missing commands but continues execution.
 * 
 * @return true if all commands are available, false otherwise
 */
static inline bool check_system_commands(void)
{
    const char *commands[] = {"nmcli", "bluetoothctl", "pactl", "xrandr"};
    bool all_available = true;
    for (int i = 0; i < 4; i++)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "which %s > /dev/null 2>&1", commands[i]);
        if (system(cmd) != 0)
        {
            fprintf(stderr, "Warning: %s not found. Some features may not work.\n", commands[i]);
            all_available = false;
        }
    }
    return all_available;
}

/**
 * @brief Executes a system command and waits for completion
 * 
 * Executes the given shell command and waits for it to complete.
 * 
 * @param command The shell command to execute
 */
static inline void execute_system_command(const char *command)
{
    int status = system(command);
    if (status == -1) {
        perror("Failed to execute command");
    }
}

/**
 * @brief Toggles audio mute state
 * 
 * Toggles the mute state of the default audio sink using PulseAudio.
 * Provides visual feedback via console output.
 */
static inline void mute_audio(void)
{
    execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    printf("Audio mute toggled\n");
}

/**
 * @brief Changes display brightness
 * 
 * Sets the display brightness by writing to the sysfs brightness interface.
 * Requires appropriate permissions to access backlight controls.
 * 
 * @param value Brightness value to set (actual range depends on hardware)
 */
static inline void change_display_brightness(int value)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/class/backlight/*/brightness >/dev/null 2>&1", value);
    execute_system_command(cmd);
    printf("Brightness set to: %d\n", value);
}

/**
 * @brief Changes system volume level
 * 
 * Sets the volume percentage for the default audio sink using PulseAudio.
 * 
 * @param volume Volume level as percentage (0-100)
 */
static inline void change_system_volume(int volume)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", volume);
    execute_system_command(cmd);
    printf("Volume set to: %d%%\n", volume);
}

/**
 * @brief Enables or disables Bluetooth
 * 
 * Controls Bluetooth power state using bluetoothctl command.
 * 
 * @param state true to enable, false to disable Bluetooth
 */
static inline void enable_bluetooth(bool state)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "bluetoothctl power %s", state ? "on" : "off");
    execute_system_command(cmd);
    printf("Bluetooth %s\n", state ? "Enabled" : "Disabled");
}

/**
 * @brief Enables or disables Wi-Fi
 * 
 * Controls Wi-Fi radio state using NetworkManager.
 * 
 * @param state true to enable, false to disable Wi-Fi
 */
static inline void enable_wifi(bool state)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "nmcli radio wifi %s", state ? "on" : "off");
    execute_system_command(cmd);
    printf("WiFi %s\n", state ? "Enabled" : "Disabled");
}

/**
 * @brief Gets current system brightness level
 * 
 * Reads the current brightness value from sysfs interface.
 * Returns a default value of 80 if unable to read from hardware.
 * 
 * @return Current brightness level
 */
static inline int get_system_brightness(void)
{
    FILE *fp = popen("cat /sys/class/backlight/*/brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        int brightness = 80;
        if (fscanf(fp, "%d", &brightness) != 1)
        {
            brightness = 80;
        }
        pclose(fp);
        return brightness;
    }
    return 80;
}

/**
 * @brief Gets maximum brightness capability
 * 
 * Reads the maximum brightness value from sysfs interface.
 * Returns a default value of 100 if unable to read from hardware.
 * 
 * @return Maximum brightness level
 */
static inline int get_max_brightness(void)
{
    FILE *fp = popen("cat /sys/class/backlight/*/max_brightness 2>/dev/null | head -1", "r");
    if (fp)
    {
        int max_brightness = 100;
        if (fscanf(fp, "%d", &max_brightness) != 1)
        {
            max_brightness = 100;
        }
        pclose(fp);
        return max_brightness;
    }
    return 100;
}

/**
 * @brief Gets current system volume level
 * 
 * Queries PulseAudio for the current volume level of default sink.
 * Returns a default value of 75 if unable to determine volume.
 * 
 * @return Current volume level as percentage (0-100)
 */
static inline int get_system_volume(void)
{
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+(?=%)' | head -1", "r");
    if (fp)
    {
        int volume = 75;
        if (fscanf(fp, "%d", &volume) != 1)
        {
            volume = 75;
        }
        pclose(fp);
        return volume;
    }
    return 75;
}

/**
 * @brief Gets Wi-Fi enabled state
 * 
 * Checks NetworkManager to determine if Wi-Fi radio is enabled.
 * 
 * @return 1 if Wi-Fi is enabled, 0 otherwise
 */
static inline int get_system_wifi_state(void)
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

/**
 * @brief Gets Bluetooth enabled state
 * 
 * Checks Bluetooth controller status to determine if Bluetooth is powered on.
 * First verifies Bluetooth hardware is present before checking power state.
 * 
 * @return 1 if Bluetooth is enabled, 0 otherwise
 */
static inline int get_system_bluetooth_state(void)
{
    FILE *fp_check = popen("bluetoothctl list 2>/dev/null | grep -q . && echo present", "r");
    int is_present = 0;
    if (fp_check)
    {
        char present[16];
        if (fgets(present, sizeof(present), fp_check))
        {
            is_present = strstr(present, "present") != NULL;
        }
        pclose(fp_check);
    }
    if (!is_present)
    {
        return 0;
    }

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

/**
 * @brief Gets battery charge level
 * 
 * Reads battery capacity from sysfs power supply interface.
 * Returns 100 if no battery is detected (assumes desktop system).
 * 
 * @return Battery charge level as percentage (0-100)
 */
static inline int get_battery_level(void)
{
    FILE *check_fp = popen("ls /sys/class/power_supply/ | grep -E '^BAT' | head -1", "r");
    char battery_name[64] = {0};
    int has_battery = 0;
    if (check_fp)
    {
        if (fgets(battery_name, sizeof(battery_name), check_fp))
        {
            battery_name[strcspn(battery_name, "\n")] = 0;
            has_battery = strlen(battery_name) > 0;
        }
        pclose(check_fp);
    }

    if (!has_battery)
    {
        return 100;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "cat /sys/class/power_supply/%s/capacity 2>/dev/null", battery_name);

    FILE *fp = popen(cmd, "r");
    if (fp)
    {
        int level = 85;
        if (fscanf(fp, "%d", &level) != 1)
        {
            level = 85;
        }
        pclose(fp);
        return level;
    }
    return 85;
}

/**
 * @brief Gets current network connection status
 * 
 * Checks NetworkManager for active connections and provides a human-readable
 * status string. Handles cases where no network interfaces are available.
 * 
 * @param network_status Output buffer for status message (minimum 128 bytes)
 */
static inline void get_network_status(char* network_status)
{
    if (!network_status) return;
    
    FILE *check_fp = popen("ip link show | grep -v 'lo:' | grep -E '^[0-9]+:' | head -1", "r");
    char iface_line[128] = {0};
    int has_iface = 0;
    if (check_fp)
    {
        if (fgets(iface_line, sizeof(iface_line), check_fp))
        {
            has_iface = strlen(iface_line) > 0;
        }
        pclose(check_fp);
    }

    if (!has_iface)
    {
        strcpy(network_status, "No network card detected");
        return;
    }

    FILE *fp = popen("nmcli -t -f NAME connection show --active 2>/dev/null | head -1", "r");
    if (fp)
    {
        char network[128];
        if (fgets(network, sizeof(network), fp))
        {
            network[strcspn(network, "\n")] = 0;
            if (strlen(network) > 0)
            {
                snprintf(network_status, 128, "Connected: %s", network);
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

#endif /* DEVICES_HELPER_H */