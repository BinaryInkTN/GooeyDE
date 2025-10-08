#include "gooey.h"
#include "utils/resolution_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <GLPS/glps_thread.h>
#include <dbus/dbus.h>

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

static DBusConnection *dbus_conn = NULL;
static int dbus_initialized = 0;
static int dbus_thread_running = 1;

static gthread_mutex_t managed_windows_mutex;
static gthread_mutex_t dock_apps_mutex;
static gthread_mutex_t ui_update_mutex;

typedef struct
{
    unsigned long window_id;
    char *title;
    char *state;
    int is_minimized;
    int is_fullscreen;
} ManagedWindow;

static ManagedWindow *managed_windows = NULL;
static int managed_windows_count = 0;
static int managed_windows_capacity = 0;

static GooeyImage **dock_app_buttons = NULL;
static char **dock_app_window_ids = NULL;
static int dock_app_buttons_count = 0;

static GooeyImage *dock_bg = NULL;
static int dock_width = 0;
static int dock_height = 0;
static int dock_x = 0;
static int dock_y = 0;

#define DBUS_SERVICE "dev.binaryink.gshell"
#define DBUS_PATH "/dev/binaryink/gshell"
#define DBUS_INTERFACE "dev.binaryink.gshell"

#define DOCK_APP_SIZE 48
#define DOCK_APP_MARGIN 10
#define DOCK_APP_MAX 8
#define DOCK_APP_BUTTONS_CAPACITY 8

void execute_system_command(const char *command);
int get_system_brightness();
int get_max_brightness();
int get_system_volume();
int get_system_wifi_state();
int get_system_bluetooth_state();
int get_battery_level();
void get_network_status();
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
void wifi_switch_callback(bool value);
void bluetooth_switch_callback(bool value);
void airplane_switch_callback(bool value);
void volume_slider_callback(long value);
void brightness_slider_callback(long value);
void mute_audio_callback();
void refresh_system_info();

int init_dbus_connection();
void cleanup_dbus();
void *dbus_monitor_thread(void *arg);
void process_dbus_message(DBusMessage *message);
void handle_window_list_updated(DBusMessage *message);
void handle_window_state_changed(DBusMessage *message);
void request_window_list();
void send_window_command(const char *window_id, const char *command);
void update_dock_apps();
void create_dock_app_button(const char *window_id, const char *title, const char *state);
void update_dock_app_button(const char *window_id, const char *state);
void remove_dock_app_button(const char *window_id);
void dock_app_button_callback_wrapper();
void cleanup_dock_app_buttons();
ManagedWindow *find_managed_window(const char *window_id);
void add_managed_window(const char *window_id, const char *title, const char *state);
void update_managed_window(const char *window_id, const char *state);
void remove_managed_window(const char *window_id);
const char *get_app_icon_for_title(const char *title);

void dock_app_button_callback_wrapper()
{
    printf("callback wrapper invoked\n");
    int button_index = 1;
    printf("Dock button clicked: index %d\n", button_index);

    glps_thread_mutex_lock(&dock_apps_mutex);

    if (button_index < 0 || button_index >= dock_app_buttons_count)
    {
        printf("Invalid button index: %d\n", button_index);
        glps_thread_mutex_unlock(&dock_apps_mutex);
        return;
    }

    const char *window_id = dock_app_window_ids[button_index];
    if (!window_id)
    {
        printf("No window ID for button index %d\n", button_index);
        glps_thread_mutex_unlock(&dock_apps_mutex);
        return;
    }

    char window_id_copy[64];
    strncpy(window_id_copy, window_id, sizeof(window_id_copy) - 1);
    window_id_copy[sizeof(window_id_copy) - 1] = '\0';

    glps_thread_mutex_unlock(&dock_apps_mutex);

    glps_thread_mutex_lock(&managed_windows_mutex);
    ManagedWindow *window = find_managed_window(window_id_copy);

    if (window)
    {
        if (window->is_minimized)
        {
            printf("Restoring window: %s\n", window->title);
            glps_thread_mutex_unlock(&managed_windows_mutex);
            send_window_command(window_id_copy, "restore");
        }
        else
        {
            printf("Minimizing window: %s\n", window->title);
            glps_thread_mutex_unlock(&managed_windows_mutex);
            send_window_command(window_id_copy, "minimize");
        }
    }
    else
    {
        printf("Window not found: %s\n", window_id_copy);
        glps_thread_mutex_unlock(&managed_windows_mutex);
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
            handle_window_list_updated(message);
        }
        else if (strcmp(member, "WindowStateChanged") == 0)
        {
            handle_window_state_changed(message);
        }
    }
}

void handle_window_list_updated(DBusMessage *message)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter))
    {
        fprintf(stderr, "No arguments in WindowListUpdated signal\n");
        return;
    }

    glps_thread_mutex_lock(&managed_windows_mutex);

    for (int i = 0; i < managed_windows_count; i++)
    {
        free(managed_windows[i].title);
        free(managed_windows[i].state);
    }
    managed_windows_count = 0;

    DBusMessageIter array_iter;
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY)
    {
        dbus_message_iter_recurse(&iter, &array_iter);

        while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING)
        {
            const char *window_id;
            dbus_message_iter_get_basic(&array_iter, &window_id);

            if (window_id)
            {
                printf("Adding window ID from shell: %s\n", window_id);
                add_managed_window(window_id, window_id, "normal");
            }

            dbus_message_iter_next(&array_iter);
        }
    }

    printf("Received window list update: %d windows\n", managed_windows_count);
    glps_thread_mutex_unlock(&managed_windows_mutex);

    update_dock_apps();
}

void handle_window_state_changed(DBusMessage *message)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter))
    {
        fprintf(stderr, "No arguments in WindowStateChanged signal\n");
        return;
    }

    const char *window_id = NULL;
    const char *state = NULL;

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &window_id);
        dbus_message_iter_next(&iter);
    }

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &state);
    }

    if (window_id && state)
    {
        printf("Window state changed: %s -> %s\n", window_id, state);

        glps_thread_mutex_lock(&managed_windows_mutex);
        update_managed_window(window_id, state);
        glps_thread_mutex_unlock(&managed_windows_mutex);

        update_dock_app_button(window_id, state);
    }
}

void request_window_list()
{
    if (!dbus_initialized)
        return;

    DBusMessage *message;
    DBusPendingCall *pending;

    message = dbus_message_new_method_call(DBUS_SERVICE, DBUS_PATH,
                                           DBUS_INTERFACE, "GetWindowList");
    if (!message)
    {
        fprintf(stderr, "Failed to create DBus method call\n");
        return;
    }

    if (!dbus_connection_send_with_reply(dbus_conn, message, &pending, 2000))
    {
        fprintf(stderr, "Failed to send DBus message\n");
        dbus_message_unref(message);
        return;
    }

    dbus_message_unref(message);

    if (!pending)
    {
        fprintf(stderr, "No pending call\n");
        return;
    }

    dbus_pending_call_block(pending);

    message = dbus_pending_call_steal_reply(pending);
    if (message)
    {
        process_dbus_message(message);
        dbus_message_unref(message);
    }

    dbus_pending_call_unref(pending);
}

void send_window_command(const char *window_id, const char *command)
{
    if (!dbus_initialized || !window_id || !command)
        return;

    DBusMessage *message;
    DBusMessageIter iter;

    message = dbus_message_new_method_call(DBUS_SERVICE, DBUS_PATH,
                                           DBUS_INTERFACE, "SendWindowCommand");
    if (!message)
    {
        fprintf(stderr, "Failed to create DBus method call\n");
        return;
    }

    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &window_id);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &command);

    if (!dbus_connection_send(dbus_conn, message, NULL))
    {
        fprintf(stderr, "Failed to send DBus command\n");
    }
    else
    {
        dbus_connection_flush(dbus_conn);
        printf("Sent command '%s' for window %s\n", command, window_id);
    }

    dbus_message_unref(message);
}

ManagedWindow *find_managed_window(const char *window_id)
{
    if (!window_id)
        return NULL;

    unsigned long id = strtoul(window_id, NULL, 10);
    for (int i = 0; i < managed_windows_count; i++)
    {
        if (managed_windows[i].window_id == id)
        {
            return &managed_windows[i];
        }
    }
    return NULL;
}

void add_managed_window(const char *window_id, const char *title, const char *state)
{
    if (!window_id)
        return;

    if (managed_windows_count >= managed_windows_capacity)
    {
        int new_capacity = managed_windows_capacity == 0 ? 8 : managed_windows_capacity * 2;
        ManagedWindow *new_array = realloc(managed_windows, new_capacity * sizeof(ManagedWindow));
        if (!new_array)
        {
            fprintf(stderr, "Failed to realloc managed_windows array\n");
            return;
        }
        managed_windows = new_array;
        managed_windows_capacity = new_capacity;
    }

    ManagedWindow *window = &managed_windows[managed_windows_count++];
    window->window_id = strtoul(window_id, NULL, 10);
    window->title = title ? strdup(title) : strdup("Unknown");
    window->state = state ? strdup(state) : strdup("normal");
    window->is_minimized = (state && strcmp(state, "minimized") == 0);
    window->is_fullscreen = (state && strcmp(state, "fullscreen") == 0);
}

void update_managed_window(const char *window_id, const char *state)
{
    ManagedWindow *window = find_managed_window(window_id);
    if (window && state)
    {
        free(window->state);
        window->state = strdup(state);
        window->is_minimized = (strcmp(state, "minimized") == 0);
        window->is_fullscreen = (strcmp(state, "fullscreen") == 0);
    }
}

void remove_managed_window(const char *window_id)
{
    if (!window_id)
        return;

    unsigned long id = strtoul(window_id, NULL, 10);
    for (int i = 0; i < managed_windows_count; i++)
    {
        if (managed_windows[i].window_id == id)
        {
            free(managed_windows[i].title);
            free(managed_windows[i].state);

            for (int j = i; j < managed_windows_count - 1; j++)
            {
                managed_windows[j] = managed_windows[j + 1];
            }
            managed_windows_count--;
            return;
        }
    }
}

const char *get_app_icon_for_title(const char *title)
{
    if (!title)
        return "assets/app_default.png";

    char lower_title[256];
    for (int i = 0; title[i] && i < 255; i++)
    {
        lower_title[i] = tolower(title[i]);
    }
    lower_title[strlen(title) < 255 ? strlen(title) : 255] = '\0';

    if (strstr(lower_title, "firefox") || strstr(lower_title, "browser"))
    {
        return "assets/firefox.png";
    }
    else if (strstr(lower_title, "terminal") || strstr(lower_title, "term"))
    {
        return "assets/terminal.png";
    }
    else if (strstr(lower_title, "file") || strstr(lower_title, "nautilus"))
    {
        return "assets/files.png";
    }
    else if (strstr(lower_title, "text") || strstr(lower_title, "editor"))
    {
        return "assets/text_editor.png";
    }
    else if (strstr(lower_title, "calculator"))
    {
        return "assets/calculator.png";
    }
    else if (strstr(lower_title, "music") || strstr(lower_title, "audio"))
    {
        return "assets/music.png";
    }
    else if (strstr(lower_title, "video") || strstr(lower_title, "player"))
    {
        return "assets/video.png";
    }
    else if (strstr(lower_title, "settings") || strstr(lower_title, "config"))
    {
        return "assets/settings.png";
    }

    return "assets/app_default.png";
}
void cleanup_dock_app_buttons()
{
    glps_thread_mutex_lock(&dock_apps_mutex);

    if (!dock_app_buttons)
    {
        glps_thread_mutex_unlock(&dock_apps_mutex);
        return;
    }

    for (int i = 0; i < dock_app_buttons_count; i++)
    {
        if (dock_app_buttons[i])
        {
        }
        if (dock_app_window_ids[i])
        {
            free(dock_app_window_ids[i]);
            dock_app_window_ids[i] = NULL;
        }
        dock_app_buttons[i] = NULL;
    }

    free(dock_app_buttons);
    free(dock_app_window_ids);

    dock_app_buttons = NULL;
    dock_app_window_ids = NULL;
    dock_app_buttons_count = 0;

    glps_thread_mutex_unlock(&dock_apps_mutex);
}
void test_callback()
{
    printf("Test callback invoked\n");
}

void create_dock_app_button(const char *window_id, const char *title, const char *state)
{
    if (!window_id || !title)
        return;

    glps_thread_mutex_lock(&dock_apps_mutex);

    if (!dock_app_buttons)
    {
        dock_app_buttons = malloc(DOCK_APP_BUTTONS_CAPACITY * sizeof(GooeyImage *));
        dock_app_window_ids = malloc(DOCK_APP_BUTTONS_CAPACITY * sizeof(char *));

        if (!dock_app_buttons || !dock_app_window_ids)
        {
            fprintf(stderr, "Failed to alloc dock arrays\n");
            glps_thread_mutex_unlock(&dock_apps_mutex);
            return;
        }

        for (int i = 0; i < DOCK_APP_BUTTONS_CAPACITY; i++)
        {
            dock_app_buttons[i] = NULL;
            dock_app_window_ids[i] = NULL;
        }

        dock_app_buttons_count = 0;
    }

    int button_x = dock_x + (dock_app_buttons_count * (DOCK_APP_SIZE + DOCK_APP_MARGIN)) + DOCK_APP_MARGIN;
    int button_y = dock_y + (dock_height - DOCK_APP_SIZE) / 2;

    if (dock_app_buttons_count >= DOCK_APP_MAX)
    {
        printf("Too many apps in dock, skipping: %s\n", title);
        glps_thread_mutex_unlock(&dock_apps_mutex);
        return;
    }

    dock_app_window_ids[dock_app_buttons_count] = strdup(window_id);
    if (!dock_app_window_ids[dock_app_buttons_count])
    {
        fprintf(stderr, "Failed to duplicate window_id\n");
        glps_thread_mutex_unlock(&dock_apps_mutex);
        return;
    }

    const char *icon_path = "assets/app_default.png";

    dock_app_buttons[dock_app_buttons_count] = (GooeyImage *)GooeyImage_Create(
        icon_path,
        button_x,
        button_y,
        DOCK_APP_SIZE,
        DOCK_APP_SIZE,
        NULL);

    GooeyCanvas *test_c = GooeyCanvas_Create(button_x,
                                             button_y,
                                             DOCK_APP_SIZE,
                                             DOCK_APP_SIZE, dock_app_button_callback_wrapper);
    GooeyCanvas_DrawRectangle(test_c, button_x,
                              button_y,
                              DOCK_APP_SIZE,
                              DOCK_APP_SIZE, 0xFF0000, true, 0.0f, true, 0.0f);
    GooeyWindow_RegisterWidget(win, test_c);
    if (dock_app_buttons[dock_app_buttons_count])
    {
        GooeyWindow_RegisterWidget(win, dock_app_buttons[dock_app_buttons_count]);
        dock_app_buttons_count++;

        printf("Created dock app button: %s (%s) at %d,%d with default icon\n", title, state, button_x, button_y);
    }
    else
    {
        free(dock_app_window_ids[dock_app_buttons_count]);
        dock_app_window_ids[dock_app_buttons_count] = NULL;
        fprintf(stderr, "Failed to create dock app button for: %s\n", title);
    }

    glps_thread_mutex_unlock(&dock_apps_mutex);
}
void update_dock_app_button(const char *window_id, const char *state)
{
    if (!window_id || !state)
        return;

    glps_thread_mutex_lock(&dock_apps_mutex);

    for (int i = 0; i < dock_app_buttons_count; i++)
    {
        if (dock_app_window_ids[i] && strcmp(dock_app_window_ids[i], window_id) == 0)
        {
            if (strcmp(state, "minimized") == 0)
            {
                printf("Dock app %s is minimized\n", window_id);
            }
            else if (strcmp(state, "normal") == 0 || strcmp(state, "restored") == 0)
            {
                printf("Dock app %s is restored\n", window_id);
            }
            glps_thread_mutex_unlock(&dock_apps_mutex);
            return;
        }
    }

    glps_thread_mutex_unlock(&dock_apps_mutex);
}
void remove_dock_app_button(const char *window_id)
{
    if (!window_id)
        return;

    glps_thread_mutex_lock(&dock_apps_mutex);

    for (int i = 0; i < dock_app_buttons_count; i++)
    {
        if (dock_app_window_ids[i] && strcmp(dock_app_window_ids[i], window_id) == 0)
        {
            printf("Removing dock app button %s\n", window_id);

            free(dock_app_window_ids[i]);
            dock_app_window_ids[i] = NULL;

            for (int j = i; j < dock_app_buttons_count - 1; j++)
            {
                dock_app_buttons[j] = dock_app_buttons[j + 1];
                dock_app_window_ids[j] = dock_app_window_ids[j + 1];
            }

            dock_app_buttons[dock_app_buttons_count - 1] = NULL;
            dock_app_window_ids[dock_app_buttons_count - 1] = NULL;

            dock_app_buttons_count--;

            glps_thread_mutex_unlock(&dock_apps_mutex);
            update_dock_apps();
            return;
        }
    }

    glps_thread_mutex_unlock(&dock_apps_mutex);
}
void update_dock_apps()
{
    glps_thread_mutex_lock(&managed_windows_mutex);
    printf("Updating dock with %d windows\n", managed_windows_count);

    int window_count = managed_windows_count;
    ManagedWindow *windows_copy = malloc(window_count * sizeof(ManagedWindow));
    if (!windows_copy)
    {
        glps_thread_mutex_unlock(&managed_windows_mutex);
        return;
    }

    for (int i = 0; i < window_count; i++)
    {
        windows_copy[i].window_id = managed_windows[i].window_id;
        windows_copy[i].title = strdup(managed_windows[i].title);
        windows_copy[i].state = strdup(managed_windows[i].state);
        windows_copy[i].is_minimized = managed_windows[i].is_minimized;
        windows_copy[i].is_fullscreen = managed_windows[i].is_fullscreen;
    }

    glps_thread_mutex_unlock(&managed_windows_mutex);

    cleanup_dock_app_buttons();

    for (int i = 0; i < window_count; i++)
    {
        char window_id_str[32];
        snprintf(window_id_str, sizeof(window_id_str), "%lu", windows_copy[i].window_id);
        create_dock_app_button(window_id_str, windows_copy[i].title, windows_copy[i].state);
    }

    for (int i = 0; i < window_count; i++)
    {
        free(windows_copy[i].title);
        free(windows_copy[i].state);
    }
    free(windows_copy);
}

void execute_system_command(const char *command)
{
    if (fork() == 0)
    {
        execl("/bin/sh", "sh", "-c", command, NULL);
        perror("Failed to execute command");
        exit(1);
    }
}

int get_system_brightness()
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

int get_max_brightness()
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

int get_system_volume()
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
        if (fscanf(fp, "%d", &level) != 1)
        {
            level = 85;
        }
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
    get_network_status();

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

    glps_thread_mutex_unlock(&ui_update_mutex);
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

void run_systemsettings()
{
    if (fork() == 0)
    {
        execl("./gooeyde_systemsettings", "./gooeyde_systemsettings", NULL);
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
    settings_button = GooeyButton_Create("Open", slider_x, current_y, 100, 30, run_systemsettings);

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

void set_control_panel_visibility(int visible)
{
    if (!control_panel_bg)
        return;

    glps_thread_mutex_lock(&ui_update_mutex);

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

    glps_thread_mutex_unlock(&ui_update_mutex);
}

void wifi_switch_callback(bool value)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "nmcli radio wifi %s", value ? "on" : "off");
    execute_system_command(cmd);
    printf("WiFi %s\n", value ? "Enabled" : "Disabled");

    get_network_status();

    glps_thread_mutex_lock(&ui_update_mutex);
    if (network_label)
    {
        GooeyLabel_SetText(network_label, network_status);
    }
    glps_thread_mutex_unlock(&ui_update_mutex);

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

void brightness_slider_callback(long value)
{
    current_brightness = value;
    int max_bright = get_max_brightness();
    int actual_level = (value * max_bright) / 100;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/class/backlight/*/brightness >/dev/null 2>&1", actual_level);
    execute_system_command(cmd);
    printf("Brightness set to: %ld%%\n", value);

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
    execute_system_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    printf("Audio mute toggled\n");
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
    glps_thread_mutex_unlock(&ui_update_mutex);

    printf("System info refreshed\n");
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

int main(int argc, char **argv)
{
    printf("Starting Gooey Desktop...\n");

    glps_thread_mutex_init(&managed_windows_mutex, NULL);
    glps_thread_mutex_init(&dock_apps_mutex, NULL);
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

    GooeyImage *wallpaper = GooeyImage_Create("assets/bg.png", 0, 50, screen_info.width, screen_info.height - 50, NULL);
    GooeyCanvas *canvas = GooeyCanvas_Create(0, 0, screen_info.width, 50, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, screen_info.width, 50, 0x222222, true, 1.0f, true, 1.0f);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);

    GooeyImage *apps_icon = GooeyImage_Create("assets/apps.png", 10, 10, 30, 30, run_app_menu);
    GooeyImage *settings_icon = GooeyImage_Create("assets/settings.png", screen_info.width - 210, 10, 30, 30, toggle_control_panel);
    time_label = GooeyLabel_Create("Loading...", 0.3f, screen_info.width - 150, 18);
    GooeyLabel_SetColor(time_label, 0xFFFFFF);
    date_label = GooeyLabel_Create("Loading...", 0.25f, screen_info.width - 150, 35);
    GooeyLabel_SetColor(date_label, 0xCCCCCC);

    create_status_icons();

    GooeyWindow_RegisterWidget(win, canvas);
    GooeyWindow_RegisterWidget(win, wallpaper);
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

            usleep(100000);
            request_window_list();
        }
    }
    else
    {
        fprintf(stderr, "DBus initialization failed, window management disabled\n");
    }

    dock_width = screen_info.width * 0.4;
    dock_height = screen_info.height * 0.07;
    if (dock_width < 300)
        dock_width = 300;
    if (dock_height < 40)
        dock_height = 40;
    dock_x = (screen_info.width - dock_width) / 2;
    dock_y = screen_info.height - dock_height - (screen_info.height * 0.10);
    if (dock_y < 0)
        dock_y = 0;

    dock_bg = GooeyImage_Create("assets/dock.png", dock_x, dock_y, dock_width, dock_height, NULL);
    GooeyWindow_RegisterWidget(win, dock_bg);

    printf("Desktop started successfully\n");
    printf("Screen resolution: %dx%d\n", screen_info.width, screen_info.height);
    printf("Click the settings icon to toggle control panel visibility\n");
    printf("Open apps will appear in the dock at the bottom\n");
    run_systemsettings();
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
    cleanup_dock_app_buttons();

    glps_thread_mutex_lock(&managed_windows_mutex);
    for (int i = 0; i < managed_windows_count; i++)
    {
        free(managed_windows[i].title);
        free(managed_windows[i].state);
    }
    free(managed_windows);
    glps_thread_mutex_unlock(&managed_windows_mutex);

    glps_thread_mutex_destroy(&managed_windows_mutex);
    glps_thread_mutex_destroy(&dock_apps_mutex);
    glps_thread_mutex_destroy(&ui_update_mutex);

    GooeyWindow_Cleanup(1, win);
    printf("Desktop shutdown complete\n");
    return 0;
}