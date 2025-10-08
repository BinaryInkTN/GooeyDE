#include "gooey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <GLPS/glps_thread.h>
#include "utils/resolution_helper.h"

static ScreenInfo screen_info;
static GooeyWindow *main_window = NULL;
static GooeyContainers *main_container = NULL;

typedef struct
{
    char *name;
    char *exec;
    char *icon_path;
} AppEntry;

static AppEntry *detected_apps = NULL;
static size_t app_count = 0;
static size_t current_page = 0;
static const size_t APPS_PER_PAGE = 8;

static int ui_initialized = 0;
static GooeyCanvas *background = NULL;
static GooeyLabel *title_label = NULL;
static GooeyLabel *page_info_label = NULL;
static GooeyImage *close_button = NULL;
static GooeyCanvas *prev_button = NULL;
static GooeyLabel *prev_label = NULL;
static GooeyCanvas *next_button = NULL;
static GooeyLabel *next_label = NULL;

static GooeyLabel *loading_label = NULL;

static GooeyLabel *search_label = NULL;
static GooeyTextbox *search_input = NULL;
static char search_query[256] = {0};
static AppEntry *filtered_apps = NULL;
static size_t filtered_app_count = 0;
static int use_filtered_apps = 0;

static GooeyCanvas *app_buttons[8] = {NULL};
static GooeyLabel *app_name_labels[8] = {NULL};
static GooeyLabel *app_exec_labels[8] = {NULL};
static GooeyImage *app_icons[8] = {NULL};

#define GRID_COLS 2
#define GRID_ROWS 4

static gthread_t app_detect_thread;
static volatile int apps_loaded = 0;

static void initialize_application(void);
static void cleanup_application(void);
static void detect_system_applications(void);
static void scan_application_directory(const char *dir_path);
static void parse_desktop_file(const char *file_path);
static void create_ui(void);
static void update_ui(void);
static void launch_application(size_t app_index);
static void next_page(int x, int y);
static void prev_page(int x, int y);
static void close_application(void);
static char *trim_string(char *str);
static void free_app_entries(void);
static void check_apps_loaded(void);
static void on_search_text_changed(char *text);
static void perform_fuzzy_search(const char *query);
static int fuzzy_match(const char *str, const char *pattern);
static char *find_icon_path(const char *icon_name);
static void free_filtered_apps(void);

static void launch_app_callback_0(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 0;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_1(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 1;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_2(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 2;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_3(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 3;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_4(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 4;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_5(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 5;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_6(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 6;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}
static void launch_app_callback_7(int x, int y)
{
    size_t app_index = current_page * APPS_PER_PAGE + 7;
    if (use_filtered_apps)
    {
        if (app_index < filtered_app_count)
            launch_application(app_index);
    }
    else
    {
        if (app_index < app_count)
            launch_application(app_index);
    }
}

static void initialize_application(void)
{
    detected_apps = NULL;
    app_count = 0;
    current_page = 0;
    ui_initialized = 0;
    apps_loaded = 0;
    filtered_apps = NULL;
    filtered_app_count = 0;
    use_filtered_apps = 0;
    memset(search_query, 0, sizeof(search_query));
}

static void cleanup_application(void)
{
    free_app_entries();
    free_filtered_apps();

    if (main_window)
    {
        GooeyWindow_Cleanup(1, main_window);
        main_window = NULL;
    }
}

static void free_app_entries(void)
{
    if (detected_apps)
    {
        for (size_t i = 0; i < app_count; i++)
        {
            free(detected_apps[i].name);
            free(detected_apps[i].exec);
            free(detected_apps[i].icon_path);
        }
        free(detected_apps);
        detected_apps = NULL;
        app_count = 0;
    }
}

static void free_filtered_apps(void)
{
    if (filtered_apps)
    {

        free(filtered_apps);
        filtered_apps = NULL;
        filtered_app_count = 0;
    }
}

static char *trim_string(char *str)
{
    if (!str)
        return NULL;
    char *end;

    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;
    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n'))
        end--;
    *(end + 1) = '\0';

    return str;
}

static char *find_icon_path(const char *icon_name)
{
    if (!icon_name || strlen(icon_name) == 0)
        return NULL;

    const char *icon_dirs[] = {
        "/usr/share/pixmaps",
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/32x32/apps",
        "/usr/share/icons/hicolor/64x64/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/gnome/48x48/apps",
        "/usr/share/icons",
        NULL};

    if (icon_name[0] == '/')
    {
        if (access(icon_name, F_OK) == 0)
            return strdup(icon_name);
        return NULL;
    }

    const char *extensions[] = {".png", ""};

    for (int i = 0; icon_dirs[i] != NULL; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s%s", icon_dirs[i], icon_name, extensions[j]);
            if (access(path, F_OK) == 0)
            {
                return strdup(path);
            }
        }
    }

    return NULL;
}

static void parse_desktop_file(const char *file_path)
{
    FILE *file = fopen(file_path, "r");
    if (!file)
        return;

    char line[512];
    char name[256] = {0};
    char exec[256] = {0};
    char icon[256] = {0};
    int no_display = 0;
    int hidden = 0;
    int is_desktop_entry = 0;

    while (fgets(line, sizeof(line), file))
    {
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '\0')
            continue;

        if (strcmp(line, "[Desktop Entry]") == 0)
        {
            is_desktop_entry = 1;
            continue;
        }

        if (line[0] == '[' && is_desktop_entry)
            break;

        if (is_desktop_entry)
        {
            if (strncmp(line, "Name=", 5) == 0)
            {
                strncpy(name, line + 5, sizeof(name) - 1);
            }
            else if (strncmp(line, "Exec=", 5) == 0)
            {
                strncpy(exec, line + 5, sizeof(exec) - 1);
                char *percent = strchr(exec, '%');
                if (percent)
                    *percent = '\0';
                trim_string(exec);
            }
            else if (strncmp(line, "Icon=", 5) == 0)
            {
                strncpy(icon, line + 5, sizeof(icon) - 1);
                trim_string(icon);
            }
            else if (strncmp(line, "NoDisplay=", 10) == 0)
            {
                no_display = (strcmp(line + 10, "true") == 0);
            }
            else if (strncmp(line, "Hidden=", 7) == 0)
            {
                hidden = (strcmp(line + 7, "true") == 0);
            }
        }
    }
    fclose(file);

    if (name[0] != '\0' && exec[0] != '\0' && !no_display && !hidden)
    {
        AppEntry *new_apps = realloc(detected_apps, (app_count + 1) * sizeof(AppEntry));
        if (!new_apps)
            return;

        detected_apps = new_apps;
        detected_apps[app_count].name = strdup(name);
        detected_apps[app_count].exec = strdup(exec);

        if (icon[0] != '\0')
        {
            detected_apps[app_count].icon_path = find_icon_path(icon);
        }
        else
        {
            detected_apps[app_count].icon_path = NULL;
        }

        if (!detected_apps[app_count].name || !detected_apps[app_count].exec)
        {
            free(detected_apps[app_count].name);
            free(detected_apps[app_count].exec);
            free(detected_apps[app_count].icon_path);
            return;
        }

        app_count++;
    }
}

static void scan_application_directory(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".desktop"))
        {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            parse_desktop_file(full_path);
        }
    }
    closedir(dir);
}

static void detect_system_applications(void)
{
    printf("Scanning system for applications...\n");

    const char *app_directories[4] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL,
        NULL};

    const char *home_dir = getenv("HOME");
    char user_apps_dir[512] = {0};
    if (home_dir)
    {
        snprintf(user_apps_dir, sizeof(user_apps_dir), "%s/.local/share/applications", home_dir);
        app_directories[2] = user_apps_dir;
    }

    for (int i = 0; app_directories[i] != NULL; i++)
    {
        printf("Scanning directory: %s\n", app_directories[i]);
        scan_application_directory(app_directories[i]);
    }

    printf("Total applications detected: %zu\n", app_count);
    apps_loaded = 1;
}

static void *detect_apps_thread_func(void *arg)
{
    detect_system_applications();
    update_ui();
        
    return NULL;
}

static int fuzzy_match(const char *str, const char *pattern)
{
    if (!pattern || !*pattern)
        return 1;
    if (!str || !*str)
        return 0;

    const char *s = str;
    const char *p = pattern;

    while (*s && *p)
    {
        if (tolower(*p) == tolower(*s))
        {
            p++;
        }
        s++;
    }

    return *p == '\0';
}

static void perform_fuzzy_search(const char *query)
{
    free_filtered_apps();

    if (!query || strlen(query) == 0)
    {
        use_filtered_apps = 0;
        current_page = 0;
        return;
    }

    size_t match_count = 0;
    for (size_t i = 0; i < app_count; i++)
    {
        if (fuzzy_match(detected_apps[i].name, query) ||
            fuzzy_match(detected_apps[i].exec, query))
        {
            match_count++;
        }
    }

    if (match_count == 0)
    {
        use_filtered_apps = 1;
        filtered_app_count = 0;
        current_page = 0;
        return;
    }

    filtered_apps = malloc(match_count * sizeof(AppEntry));
    if (!filtered_apps)
    {
        use_filtered_apps = 0;
        return;
    }

    size_t idx = 0;
    for (size_t i = 0; i < app_count; i++)
    {
        if (fuzzy_match(detected_apps[i].name, query) ||
            fuzzy_match(detected_apps[i].exec, query))
        {

            filtered_apps[idx].name = detected_apps[i].name;
            filtered_apps[idx].exec = detected_apps[i].exec;
            filtered_apps[idx].icon_path = detected_apps[i].icon_path;
            idx++;
        }
    }

    filtered_app_count = match_count;
    use_filtered_apps = 1;
    current_page = 0;
}

static void on_search_text_changed(char *text)
{
    if (text)
    {
        strncpy(search_query, text, sizeof(search_query) - 1);
        search_query[sizeof(search_query) - 1] = '\0';
    }
    else
    {
        search_query[0] = '\0';
    }

    perform_fuzzy_search(search_query);
    update_ui();
}

static void launch_application(size_t app_index)
{
    AppEntry *apps = use_filtered_apps ? filtered_apps : detected_apps;
    size_t count = use_filtered_apps ? filtered_app_count : app_count;

    if (app_index >= count)
        return;

    printf("Launching: %s (%s)\n", apps[app_index].name, apps[app_index].exec);

    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("sh", "sh", "-c", apps[app_index].exec, NULL);
        fprintf(stderr, "Failed to launch application: %s (%s)\n",
                apps[app_index].exec, strerror(errno));
        exit(1);
    }
    else if (pid < 0)
    {
        fprintf(stderr, "Failed to fork process: %s\n", strerror(errno));
        return;
    }

    close_application();
}

static void next_page(int x, int y)
{
    size_t total_count = use_filtered_apps ? filtered_app_count : app_count;
    size_t total_pages = (total_count + APPS_PER_PAGE - 1) / APPS_PER_PAGE;
    if (current_page < total_pages - 1)
    {
        current_page++;
        update_ui();
    }
}

static void prev_page(int x, int y)
{
    if (current_page > 0)
    {
        current_page--;
        update_ui();
    }
}

static void close_application(void)
{
    cleanup_application();
    exit(0);
}

static void check_apps_loaded(void)
{
    static int last_loaded_state = 0;

    if (apps_loaded != last_loaded_state)
    {
        last_loaded_state = apps_loaded;
        update_ui();
    }
}

static void create_ui(void)
{
    if (ui_initialized)
        return;

    const int button_width = screen_info.width / 3;
    const int button_height = screen_info.height / 12;
    const int grid_margin_x = (screen_info.width - (GRID_COLS * button_width + (GRID_COLS - 1) * 30)) / 2;
    const int grid_margin_y = screen_info.height / 6 + 80;
    const int grid_spacing_x = 30;
    const int grid_spacing_y = 30;

    main_container = GooeyContainer_Create(0, 0, screen_info.width, screen_info.height);
    if (!main_container)
        return;

    GooeyContainer_InsertContainer(main_container);
    GooeyContainer_SetActiveContainer(main_container, 0);

    background = GooeyCanvas_Create(0, 0, screen_info.width, screen_info.height, NULL);
    if (background)
    {
        GooeyCanvas_DrawRectangle(background, 0, 0, screen_info.width, screen_info.height,
                                  0x222222, true, 1.0f, true, 1.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, background);
    }

    title_label = GooeyLabel_Create("Installed Applications", 0.7f,
                                    screen_info.width / 2 - 150, 50);
    if (title_label)
    {
        GooeyLabel_SetColor(title_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, title_label);
    }

    const int search_width = screen_info.width / 2;
    const int search_height = 40;
    const int search_x = (screen_info.width - search_width) / 2 + 40;
    const int search_y = 80;

    search_label = GooeyLabel_Create("Search:", 0.4f, search_x - 80, search_y + search_height / 2 + 5);
    if (search_label)
    {
        GooeyLabel_SetColor(search_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, search_label);
    }

    search_input = GooeyTextBox_Create(search_x, search_y,
                                       search_width, search_height,
                                       "Type to search applications...", false,
                                       on_search_text_changed);
    if (search_input)
    {
        GooeyContainer_AddWidget(main_window, main_container, 0, search_input);
    }

    close_button = GooeyImage_Create("assets/cross.png", screen_info.width - 50, 15, 24, 24, close_application);
    if (close_button)
    {
        GooeyContainer_AddWidget(main_window, main_container, 0, close_button);
    }

    page_info_label = GooeyLabel_Create("", 0.4f, screen_info.width / 2 - 140, search_y + search_height + 53);
    if (page_info_label)
    {
        GooeyLabel_SetColor(page_info_label, 0xAAAAAA);
        GooeyContainer_AddWidget(main_window, main_container, 0, page_info_label);
    }

    prev_button = GooeyCanvas_Create(grid_margin_x, grid_margin_y - 50, 120, 35, prev_page);
    if (prev_button)
    {
        GooeyCanvas_DrawRectangle(prev_button, 0, 0, 120, 35, 0x444444, true, 1.0f, true, 1.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, prev_button);
        GooeyWidget_MakeVisible(prev_button, 0);
    }

    prev_label = GooeyLabel_Create("Previous", 0.4f, grid_margin_x + 20, grid_margin_y - 27);
    if (prev_label)
    {
        GooeyLabel_SetColor(prev_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, prev_label);
        GooeyWidget_MakeVisible(prev_label, 0);
    }

    next_button = GooeyCanvas_Create(screen_info.width - grid_margin_x - 120, grid_margin_y - 50, 120, 35, next_page);
    if (next_button)
    {
        GooeyCanvas_DrawRectangle(next_button, 0, 0, 120, 35, 0x444444, true, 1.0f, true, 1.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, next_button);
        GooeyWidget_MakeVisible(next_button, 0);
    }

    next_label = GooeyLabel_Create("Next", 0.4f, screen_info.width - grid_margin_x - 90, grid_margin_y - 27);
    if (next_label)
    {
        GooeyLabel_SetColor(next_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, next_label);
        GooeyWidget_MakeVisible(next_label, 0);
    }

    loading_label = GooeyLabel_Create("Loading applications...", 0.5f,
                                      screen_info.width / 2 - 150, screen_info.height / 2 - 20);
    if (loading_label)
    {
        GooeyLabel_SetColor(loading_label, 0xCCCCCC);
        GooeyContainer_AddWidget(main_window, main_container, 0, loading_label);
        GooeyWidget_MakeVisible(loading_label, 1);
    }

    void (*launch_callbacks[8])(int x, int y) = {
        launch_app_callback_0, launch_app_callback_1, launch_app_callback_2, launch_app_callback_3,
        launch_app_callback_4, launch_app_callback_5, launch_app_callback_6, launch_app_callback_7};

    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            const int grid_index = row * GRID_COLS + col;
            const size_t app_index = current_page * APPS_PER_PAGE + grid_index;
            const char *initial_icon = NULL;
            if (app_index < (use_filtered_apps ? filtered_app_count : app_count))
            {
                AppEntry *apps = use_filtered_apps ? filtered_apps : detected_apps;
                initial_icon = apps[app_index].icon_path;
            }

            const int x = grid_margin_x + col * (button_width + grid_spacing_x);
            const int y = grid_margin_y + row * (button_height + grid_spacing_y);

            app_buttons[grid_index] = GooeyCanvas_Create(x, y, button_width, button_height, launch_callbacks[grid_index]);
            if (app_buttons[grid_index])
            {
                GooeyCanvas_DrawRectangle(app_buttons[grid_index], 0, 0, button_width, button_height, 0x444444, true, 1.0f, true, 1.0f);
                GooeyCanvas_DrawRectangle(app_buttons[grid_index], 2, 2, button_width - 4, button_height - 4, 0x666666, true, 1.0f, true, 1.0f);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_buttons[grid_index]);
                GooeyWidget_MakeVisible(app_buttons[grid_index], 0);
            }

            app_icons[grid_index] = GooeyImage_Create(initial_icon, x + 10, y + 10, 32, 32, NULL);
            if (app_icons[grid_index])
            {
                GooeyContainer_AddWidget(main_window, main_container, 0, app_icons[grid_index]);
                GooeyWidget_MakeVisible(app_icons[grid_index], 0);
            }

            app_name_labels[grid_index] = GooeyLabel_Create("", 0.4f, x + 52, y + button_height / 3);
            if (app_name_labels[grid_index])
            {
                GooeyLabel_SetColor(app_name_labels[grid_index], 0xFFFFFF);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_name_labels[grid_index]);
                GooeyWidget_MakeVisible(app_name_labels[grid_index], 0);
            }

            app_exec_labels[grid_index] = GooeyLabel_Create("", 0.3f, x + 52, y + button_height / 1.5);
            if (app_exec_labels[grid_index])
            {
                GooeyLabel_SetColor(app_exec_labels[grid_index], 0xCCCCCC);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_exec_labels[grid_index]);
                GooeyWidget_MakeVisible(app_exec_labels[grid_index], 0);
            }
        }
    }

    ui_initialized = 1;
}

static void update_ui(void)
{
    if (!ui_initialized)
        return;

    if (!apps_loaded)
    {
        if (loading_label)
            GooeyWidget_MakeVisible(loading_label, 1);

        for (int i = 0; i < 8; i++)
        {
            if (app_buttons[i])
                GooeyWidget_MakeVisible(app_buttons[i], 0);
            if (app_name_labels[i])
                GooeyWidget_MakeVisible(app_name_labels[i], 0);
            if (app_exec_labels[i])
                GooeyWidget_MakeVisible(app_exec_labels[i], 0);
            if (app_icons[i])
                GooeyWidget_MakeVisible(app_icons[i], 0);
        }
        if (prev_button)
            GooeyWidget_MakeVisible(prev_button, 0);
        if (prev_label)
            GooeyWidget_MakeVisible(prev_label, 0);
        if (next_button)
            GooeyWidget_MakeVisible(next_button, 0);
        if (next_label)
            GooeyWidget_MakeVisible(next_label, 0);
        if (page_info_label)
            GooeyLabel_SetText(page_info_label, "Scanning for applications...");
        return;
    }
    else
    {
        if (loading_label)
            GooeyWidget_MakeVisible(loading_label, 0);
    }

    size_t total_count = use_filtered_apps ? filtered_app_count : app_count;
    size_t total_pages = (total_count + APPS_PER_PAGE - 1) / APPS_PER_PAGE;

    char page_info[128];
    if (use_filtered_apps && search_query[0] != '\0')
    {
        snprintf(page_info, sizeof(page_info), "Page %zu/%zu - %zu applications (filtered: '%s')",
                 current_page + 1, total_pages, total_count, search_query);
    }
    else
    {
        snprintf(page_info, sizeof(page_info), "Page %zu/%zu - %zu applications",
                 current_page + 1, total_pages, total_count);
    }

    if (page_info_label)
        GooeyLabel_SetText(page_info_label, page_info);

    if (prev_button && prev_label)
    {
        int prev_visible = (current_page > 0);
        GooeyWidget_MakeVisible(prev_button, prev_visible);
        GooeyWidget_MakeVisible(prev_label, prev_visible);
    }

    if (next_button && next_label)
    {
        int next_visible = (current_page < total_pages - 1);
        GooeyWidget_MakeVisible(next_button, next_visible);
        GooeyWidget_MakeVisible(next_label, next_visible);
    }

    AppEntry *current_apps = use_filtered_apps ? filtered_apps : detected_apps;

    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            const int grid_index = row * GRID_COLS + col;
            const size_t app_index = current_page * APPS_PER_PAGE + grid_index;
            int is_visible = (app_index < total_count);

            if (app_buttons[grid_index])
            {
                GooeyWidget_MakeVisible(app_buttons[grid_index], is_visible);
            }

            if (app_icons[grid_index])
            {
                if (is_visible && current_apps[app_index].icon_path)
                {

                    GooeyImage_SetImage(app_icons[grid_index], current_apps[app_index].icon_path);
                    GooeyWidget_MakeVisible(app_icons[grid_index], 1);
                }
                else
                {

                    GooeyWidget_MakeVisible(app_icons[grid_index], 0);
                }
            }

            if (app_name_labels[grid_index])
            {
                if (is_visible)
                {
                    char display_name[48];
                    const char *app_name = current_apps[app_index].name;
                    if (strlen(app_name) > 35)
                    {
                        strncpy(display_name, app_name, 32);
                        display_name[32] = '\0';
                        strcat(display_name, "...");
                    }
                    else
                    {
                        strcpy(display_name, app_name);
                    }
                    GooeyLabel_SetText(app_name_labels[grid_index], display_name);
                }
                GooeyWidget_MakeVisible(app_name_labels[grid_index], is_visible);
            }

            if (app_exec_labels[grid_index])
            {
                if (is_visible)
                {
                    char display_exec[64];
                    const char *app_exec = current_apps[app_index].exec;
                    if (strlen(app_exec) > 50)
                    {
                        strncpy(display_exec, app_exec, 47);
                        display_exec[47] = '\0';
                        strcat(display_exec, "...");
                    }
                    else
                    {
                        strcpy(display_exec, app_exec);
                    }
                    GooeyLabel_SetText(app_exec_labels[grid_index], display_exec);
                }
                GooeyWidget_MakeVisible(app_exec_labels[grid_index], is_visible);
            }
        }
    }
}

int main(int argc, char **argv)
{
    initialize_application();

    Gooey_Init();

    screen_info = get_screen_resolution();

    main_window = GooeyWindow_Create("Gooey Application Menu", screen_info.width, screen_info.height, true);
    if (!main_window)
    {
        fprintf(stderr, "Failed to create main window\n");
        cleanup_application();
        return 1;
    }

    main_window->vk = NULL;

    create_ui();

    glps_thread_create(&app_detect_thread, NULL, detect_apps_thread_func, NULL);
    update_ui();

    GooeyWindow_Run(1, main_window);

    glps_thread_join(app_detect_thread, NULL);
    cleanup_application();
    cleanup_screen_info(&screen_info);

    return 0;
}