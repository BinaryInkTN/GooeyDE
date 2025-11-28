#include <Gooey/gooey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <GLPS/glps_thread.h>
#include "utils/resolution_helper.h"

static ScreenInfo screen_info;
static GooeyWindow *main_window = NULL;
static GooeyContainers *main_container = NULL;

typedef struct {
    char *name;
    char *exec;
    char *icon_path;
    char *categories;
} AppEntry;

static AppEntry *detected_apps = NULL;
static size_t app_count = 0;
static size_t current_page = 0;
#define MAX_APPS 1000

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
static GooeyTextbox *search_input = NULL;

static char search_query[256] = {0};
static AppEntry *filtered_apps = NULL;
static size_t filtered_app_count = 0;
static int use_filtered_apps = 0;

#define MAX_GRID_SIZE 16
static GooeyCanvas *app_buttons[MAX_GRID_SIZE];
static GooeyLabel *app_name_labels[MAX_GRID_SIZE];
static GooeyLabel *app_exec_labels[MAX_GRID_SIZE];
static GooeyImage *app_icons[MAX_GRID_SIZE];

static gthread_t app_detect_thread;
static volatile int apps_loaded = 0;
static volatile int apps_scan_complete = 0;

typedef struct {
    int app_card_width;
    int app_card_height;
    int horizontal_margin;
    int vertical_margin;
    int horizontal_spacing;
    int vertical_spacing;
    int grid_cols;
    int grid_rows;
    int apps_per_page;
    int title_font_size;
    int label_font_size;
    int nav_button_width;
    int nav_button_height;
    int icon_size;
    int search_width;
    int search_height;
} LayoutConfig;

static LayoutConfig layout;

static void initialize_application(void);
static void cleanup_application(void);
static void detect_system_applications(void);
static void scan_application_directory(const char *dir_path);
static int parse_desktop_file(const char *file_path);
static void create_ui(void);
static void update_ui(void);
static void launch_application(size_t app_index);
static void next_page(int x, int y, void* user_data);
static void prev_page(int x, int y, void* user_data);
static void close_application(void* user_data);
static char *trim_string(char *str);
static void free_app_entries(void);
static void on_search_text_changed(char *text);
static void perform_fuzzy_search(const char *query);
static int fuzzy_match(const char *str, const char *pattern);
static char *find_icon_path(const char *icon_name);
static void free_filtered_apps(void);
static void calculate_dynamic_layout(void);
static int is_valid_application(const char *exec);
static int app_entry_compare(const void *a, const void *b);
static void remove_duplicate_apps(void);
static char *expand_icon_path(const char *icon_name);

static void calculate_dynamic_layout(void) {
    int base_width = screen_info.width;
    int base_height = screen_info.height;

    if (base_width >= 2560) {
        layout.grid_cols = 6;
    } else if (base_width >= 1920) {
        layout.grid_cols = 5;
    } else if (base_width >= 1366) {
        layout.grid_cols = 4;
    } else if (base_width >= 1024) {
        layout.grid_cols = 3;
    } else {
        layout.grid_cols = 2;
    }

    if (base_height >= 1440) {
        layout.grid_rows = 4;
    } else if (base_height >= 1080) {
        layout.grid_rows = 3;
    } else if (base_height >= 768) {
        layout.grid_rows = 2;
    } else {
        layout.grid_rows = 2;
    }

    layout.apps_per_page = layout.grid_cols * layout.grid_rows;
    if (layout.apps_per_page > MAX_GRID_SIZE) {
        layout.apps_per_page = MAX_GRID_SIZE;
    }

    layout.app_card_width = (base_width - (layout.grid_cols + 1) * 20) / layout.grid_cols;
    layout.app_card_height = layout.app_card_width / 2.5;

    if (layout.app_card_width < 200) layout.app_card_width = 200;
    if (layout.app_card_width > 350) layout.app_card_width = 350;
    if (layout.app_card_height < 80) layout.app_card_height = 80;
    if (layout.app_card_height > 140) layout.app_card_height = 140;

    layout.horizontal_margin = 20;
    layout.vertical_margin = base_height / 5;

    layout.horizontal_spacing = 20;
    layout.vertical_spacing = 20;

    layout.title_font_size = base_height / 27;
    if (layout.title_font_size < 20) layout.title_font_size = 20;
    if (layout.title_font_size > 32) layout.title_font_size = 32;

    layout.label_font_size = base_height / 54;
    if (layout.label_font_size < 10) layout.label_font_size = 10;
    if (layout.label_font_size > 14) layout.label_font_size = 14;

    layout.nav_button_width = 120;
    layout.nav_button_height = 40;

    layout.icon_size = layout.app_card_height - 30;
    if (layout.icon_size < 32) layout.icon_size = 32;
    if (layout.icon_size > 64) layout.icon_size = 64;

    layout.search_width = base_width / 3;
    layout.search_height = 45;

    printf("Layout: %dx%d -> %dx%d grid, %dx%d cards, %d apps/page\n", 
           base_width, base_height, layout.grid_cols, layout.grid_rows, 
           layout.app_card_width, layout.app_card_height, layout.apps_per_page);
}

static void launch_app_callback(int x, int y, void* user_data) {
    size_t grid_index = (size_t)user_data;
    size_t app_index = current_page * layout.apps_per_page + grid_index;
    AppEntry *apps = use_filtered_apps ? filtered_apps : detected_apps;
    size_t count = use_filtered_apps ? filtered_app_count : app_count;

    if (app_index < count) {
        launch_application(app_index);
    }
}

static void initialize_application(void) {
    detected_apps = NULL;
    app_count = 0;
    current_page = 0;
    ui_initialized = 0;
    apps_loaded = 0;
    apps_scan_complete = 0;
    filtered_apps = NULL;
    filtered_app_count = 0;
    use_filtered_apps = 0;
    memset(search_query, 0, sizeof(search_query));
    memset(app_buttons, 0, sizeof(app_buttons));
    memset(app_name_labels, 0, sizeof(app_name_labels));
    memset(app_exec_labels, 0, sizeof(app_exec_labels));
    memset(app_icons, 0, sizeof(app_icons));
    memset(&layout, 0, sizeof(layout));
}

static void cleanup_application(void) {
    free_filtered_apps();
    free_app_entries();
}

static void free_app_entries(void) {
    if (detected_apps) {
        for (size_t i = 0; i < app_count; i++) {
            free(detected_apps[i].name);
            free(detected_apps[i].exec);
            free(detected_apps[i].icon_path);
            free(detected_apps[i].categories);
        }
        free(detected_apps);
        detected_apps = NULL;
        app_count = 0;
    }
}

static void free_filtered_apps(void) {
    if (filtered_apps) {
        for (size_t i = 0; i < filtered_app_count; i++) {
            free(filtered_apps[i].name);
            free(filtered_apps[i].exec);
            free(filtered_apps[i].icon_path);
            free(filtered_apps[i].categories);
        }
        free(filtered_apps);
        filtered_apps = NULL;
        filtered_app_count = 0;
    }
}

static char *trim_string(char *str) {
    if (!str) return NULL;

    char *end;
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

static char *expand_icon_path(const char *icon_name) {
    if (!icon_name || *icon_name == '\0') return NULL;

    if (icon_name[0] == '/') {
        if (access(icon_name, F_OK) == 0) {
            return strdup(icon_name);
        }
        return NULL;
    }
    const char *icon_dirs[] = {
        "/usr/share/pixmaps",
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/32x32/apps",
        "/usr/share/icons/hicolor/64x64/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/hicolor/256x256/apps",
        "/usr/share/icons/hicolor/scalable/apps",
        "/usr/share/icons/gnome/48x48/apps",
        "/usr/share/icons/gnome/32x32/apps",
        "/usr/share/icons/gnome/64x64/apps",
        "/usr/share/icons/breeze/apps/48",
        "/usr/share/icons/breeze/apps/32",
        "/usr/share/icons/breeze/apps/64",
        "/usr/share/icons/Adwaita/48x48/apps",
        "/usr/share/icons/Adwaita/32x32/apps",
        "/usr/share/icons/Adwaita/64x64/apps",
        "/usr/share/icons",
        "/usr/local/share/pixmaps",
        "/usr/local/share/icons",
        NULL
    };

    const char *extensions[] = {".png", ".svg", ".xpm", ""};

    for (int i = 0; icon_dirs[i] != NULL; i++) {
        struct stat dir_stat;
        if (stat(icon_dirs[i], &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
            continue;
        }

        for (int j = 0; j < 4; j++) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s%s", icon_dirs[i], icon_name, extensions[j]);

            if (access(path, F_OK) == 0) {
                printf("Found icon: %s\n", path);
                return strdup(path);
            }
        }
    }

    char path[1024];
    snprintf(path, sizeof(path), "/usr/share/pixmaps/%s.png", icon_name);
    if (access(path, F_OK) == 0) {
        return strdup(path);
    }

    return NULL;
}

static int is_valid_application(const char *exec) {
    if (!exec || *exec == '\0') return 0;

    const char *hidden_keywords[] = {
        "debian-", "xterm", "uxterm", "gnome-terminal", "konsole", "terminal",
        "xfce4-terminal", "lxterminal", "stterm", "eterm", "terminology",
        "tilix", "terminator", "guake", "tilda", "yakuake", "kgx",
        "io.elementary.terminal", "org.kde.konsole", "com.gexperts.Tilix",
        NULL
    };

    for (int i = 0; hidden_keywords[i] != NULL; i++) {
        if (strstr(exec, hidden_keywords[i]) != NULL) {
            return 0;
        }
    }

    return 1;
}

static int parse_desktop_file(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) return 0;

    char line[1024];
    char name[256] = {0};
    char exec[256] = {0};
    char icon[256] = {0};
    char categories[256] = {0};
    int no_display = 0;
    int hidden = 0;
    int is_desktop_entry = 0;
    int has_main_section = 0;

    while (fgets(line, sizeof(line), file)) {
        char *trimmed_line = trim_string(line);
        if (trimmed_line[0] == '\0') continue;

        if (strcmp(trimmed_line, "[Desktop Entry]") == 0) {
            is_desktop_entry = 1;
            has_main_section = 1;
            continue;
        }

        if (trimmed_line[0] == '[' && is_desktop_entry) {
            break;
        }

        if (is_desktop_entry) {
            if (strncmp(trimmed_line, "Name=", 5) == 0) {
                strncpy(name, trimmed_line + 5, sizeof(name) - 1);
            } else if (strncmp(trimmed_line, "Exec=", 5) == 0) {
                strncpy(exec, trimmed_line + 5, sizeof(exec) - 1);

                char *percent = strchr(exec, '%');
                if (percent) *percent = '\0';
                trim_string(exec);
            } else if (strncmp(trimmed_line, "Icon=", 5) == 0) {
                strncpy(icon, trimmed_line + 5, sizeof(icon) - 1);
                trim_string(icon);
            } else if (strncmp(trimmed_line, "Categories=", 11) == 0) {
                strncpy(categories, trimmed_line + 11, sizeof(categories) - 1);
                trim_string(categories);
            } else if (strncmp(trimmed_line, "NoDisplay=", 10) == 0) {
                no_display = (strcasecmp(trimmed_line + 10, "true") == 0);
            } else if (strncmp(trimmed_line, "Hidden=", 7) == 0) {
                hidden = (strcasecmp(trimmed_line + 7, "true") == 0);
            }
        }
    }
    fclose(file);

    if (!has_main_section || no_display || hidden || 
        name[0] == '\0' || exec[0] == '\0' || !is_valid_application(exec)) {
        return 0;
    }

    if (app_count >= MAX_APPS) {
        fprintf(stderr, "Maximum applications limit reached\n");
        return 0;
    }

    AppEntry *new_apps = realloc(detected_apps, (app_count + 1) * sizeof(AppEntry));
    if (!new_apps) return 0;

    detected_apps = new_apps;
    detected_apps[app_count].name = strdup(name);
    detected_apps[app_count].exec = strdup(exec);
    detected_apps[app_count].icon_path = expand_icon_path(icon);
    detected_apps[app_count].categories = categories[0] ? strdup(categories) : NULL;

    if (!detected_apps[app_count].name || !detected_apps[app_count].exec) {
        free(detected_apps[app_count].name);
        free(detected_apps[app_count].exec);
        free(detected_apps[app_count].icon_path);
        free(detected_apps[app_count].categories);
        return 0;
    }

    app_count++;
    return 1;
}

static void scan_application_directory(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".desktop")) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            parse_desktop_file(full_path);
        }
    }
    closedir(dir);
}

static int app_entry_compare(const void *a, const void *b) {
    const AppEntry *app_a = (const AppEntry *)a;
    const AppEntry *app_b = (const AppEntry *)b;
    return strcasecmp(app_a->name, app_b->name);
}

static void remove_duplicate_apps(void) {
    if (app_count <= 1) return;

    qsort(detected_apps, app_count, sizeof(AppEntry), app_entry_compare);

    size_t new_count = 1;
    for (size_t i = 1; i < app_count; i++) {
        if (strcasecmp(detected_apps[i].name, detected_apps[new_count - 1].name) != 0) {
            if (i != new_count) {
                detected_apps[new_count] = detected_apps[i];
            }
            new_count++;
        } else {

            free(detected_apps[i].name);
            free(detected_apps[i].exec);
            free(detected_apps[i].icon_path);
            free(detected_apps[i].categories);
        }
    }

    app_count = new_count;
}

static void detect_system_applications(void) {
    printf("Scanning system for applications...\n");

    const char *app_directories[] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        "/var/lib/flatpak/exports/share/applications",
        "/var/lib/snapd/desktop/applications",
        NULL
    };

    const char *home_dir = getenv("HOME");
    char user_apps_dir[512] = {0};
    if (home_dir) {
        snprintf(user_apps_dir, sizeof(user_apps_dir), "%s/.local/share/applications", home_dir);
    }

    for (int i = 0; app_directories[i] != NULL; i++) {
        struct stat dir_stat;
        if (stat(app_directories[i], &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode)) {
            printf("Scanning: %s\n", app_directories[i]);
            scan_application_directory(app_directories[i]);
        }
    }

    if (home_dir) {
        struct stat dir_stat;
        if (stat(user_apps_dir, &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode)) {
            printf("Scanning: %s\n", user_apps_dir);
            scan_application_directory(user_apps_dir);
        }
    }

    remove_duplicate_apps();

    printf("Found %zu applications\n", app_count);
    apps_scan_complete = 1;
}

static void *detect_apps_thread_func(void *arg) {
    detect_system_applications();
    apps_loaded = 1;
    update_ui();
    return NULL;
}

static int fuzzy_match(const char *str, const char *pattern) {
    if (!pattern || !*pattern) return 1;
    if (!str || !*str) return 0;

    const char *s = str;
    const char *p = pattern;

    while (*s && *p) {
        if (tolower(*p) == tolower(*s)) {
            p++;
        }
        s++;
    }

    return *p == '\0';
}

static void perform_fuzzy_search(const char *query) {
    free_filtered_apps();

    if (!query || strlen(query) == 0) {
        use_filtered_apps = 0;
        current_page = 0;
        return;
    }

    size_t match_count = 0;
    for (size_t i = 0; i < app_count; i++) {
        if (fuzzy_match(detected_apps[i].name, query) ||
            (detected_apps[i].categories && fuzzy_match(detected_apps[i].categories, query))) {
            match_count++;
        }
    }

    if (match_count == 0) {
        use_filtered_apps = 1;
        filtered_app_count = 0;
        current_page = 0;
        return;
    }

    filtered_apps = malloc(match_count * sizeof(AppEntry));
    if (!filtered_apps) {
        use_filtered_apps = 0;
        return;
    }

    size_t idx = 0;
    for (size_t i = 0; i < app_count; i++) {
        if (fuzzy_match(detected_apps[i].name, query) ||
            (detected_apps[i].categories && fuzzy_match(detected_apps[i].categories, query))) {
            filtered_apps[idx].name = strdup(detected_apps[i].name);
            filtered_apps[idx].exec = strdup(detected_apps[i].exec);
            filtered_apps[idx].icon_path = detected_apps[i].icon_path ? 
                strdup(detected_apps[i].icon_path) : NULL;
            filtered_apps[idx].categories = detected_apps[i].categories ? 
                strdup(detected_apps[i].categories) : NULL;
            idx++;
        }
    }

    filtered_app_count = match_count;
    use_filtered_apps = 1;
    current_page = 0;
}

static void on_search_text_changed(char *text) {
    if (text) {
        strncpy(search_query, text, sizeof(search_query) - 1);
        search_query[sizeof(search_query) - 1] = '\0';
    } else {
        search_query[0] = '\0';
    }

    perform_fuzzy_search(search_query);
    update_ui();
}

static void launch_application(size_t app_index) {
    AppEntry *apps = use_filtered_apps ? filtered_apps : detected_apps;
    size_t count = use_filtered_apps ? filtered_app_count : app_count;

    if (app_index >= count) return;

    printf("Launching: %s (%s)\n", apps[app_index].name, apps[app_index].exec);

    pid_t pid = fork();
    if (pid == 0) {

        setsid(); 
        execlp("sh", "sh", "-c", apps[app_index].exec, NULL);
        fprintf(stderr, "Failed to launch: %s\n", strerror(errno));
        exit(1);
    } else if (pid > 0) {

        printf("Launched PID: %d\n", pid);
    } else {
        fprintf(stderr, "Fork failed: %s\n", strerror(errno));
    }

    close_application(NULL);
}

static void next_page(int x, int y, void* user_data) {
    size_t total_count = use_filtered_apps ? filtered_app_count : app_count;
    size_t total_pages = (total_count + layout.apps_per_page - 1) / layout.apps_per_page;
    if (current_page < total_pages - 1) {
        current_page++;
        update_ui();
    }
}

static void prev_page(int x, int y, void* user_data) {
    if (current_page > 0) {
        current_page--;
        update_ui();
    }
}

static void close_application(void* user_data) {
    cleanup_application();
    exit(0);
}

static void create_ui(void) {
    if (ui_initialized) return;

    calculate_dynamic_layout();

    const int grid_width = (layout.grid_cols * layout.app_card_width) + 
                          ((layout.grid_cols - 1) * layout.horizontal_spacing);
    const int grid_margin_x = (screen_info.width - grid_width) / 2;
    const int grid_margin_y = layout.vertical_margin;

    main_container = GooeyContainer_Create(0, 0, screen_info.width, screen_info.height);
    if (!main_container) return;

    GooeyContainer_InsertContainer(main_container);
    GooeyContainer_SetActiveContainer(main_container, 0);

    background = GooeyCanvas_Create(0, 0, screen_info.width, screen_info.height, NULL, NULL);
    if (background) {
        GooeyCanvas_DrawRectangle(background, 0, 0, screen_info.width, screen_info.height,
                                  0x1a1a2e, true, 1.0f, true, 1.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, background);
    }

    int title_x = screen_info.width / 2 - 150;
    title_label = GooeyLabel_Create("Application Launcher", layout.title_font_size, title_x, 50);
    if (title_label) {
        GooeyLabel_SetColor(title_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, title_label);
    }

    int search_x = (screen_info.width - layout.search_width) / 2;
    int search_y = 100;

    search_input = GooeyTextBox_Create(search_x, search_y,
                                       layout.search_width, layout.search_height,
                                       "Search applications...", false,
                                       on_search_text_changed, NULL);
    if (search_input) {
        GooeyContainer_AddWidget(main_window, main_container, 0, search_input);
    }

    close_button = GooeyImage_Create("/usr/local/share/gooeyde/assets/cross.png", 
                                   screen_info.width - 50, 20, 32, 32, 
                                   close_application, NULL);
    if (close_button) {
        GooeyContainer_AddWidget(main_window, main_container, 0, close_button);
    }

    page_info_label = GooeyLabel_Create("", layout.label_font_size, 
                                       screen_info.width / 2, 
                                       screen_info.height - 40);
    if (page_info_label) {
        GooeyLabel_SetColor(page_info_label, 0x888888);
        GooeyContainer_AddWidget(main_window, main_container, 0, page_info_label);
    }

    const int nav_button_y = screen_info.height - 80;

    prev_button = GooeyCanvas_Create(grid_margin_x, nav_button_y, 
                                   layout.nav_button_width, layout.nav_button_height, 
                                   prev_page, NULL);
    if (prev_button) {
        GooeyCanvas_DrawRectangle(prev_button, 0, 0, 
                                 layout.nav_button_width, layout.nav_button_height, 
                                 0x4a4a8a, true, 1.0f, true, 8.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, prev_button);
        GooeyWidget_MakeVisible(prev_button, 0);
    }

    prev_label = GooeyLabel_Create("Previous", layout.label_font_size, 
                                  grid_margin_x + layout.nav_button_width / 2 - 10,
                                  nav_button_y + layout.nav_button_height / 2 + 5);
    if (prev_label) {
        GooeyLabel_SetColor(prev_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, prev_label);
        GooeyWidget_MakeVisible(prev_label, 0);
    }

    next_button = GooeyCanvas_Create(grid_margin_x + grid_width - layout.nav_button_width, 
                                   nav_button_y, 
                                   layout.nav_button_width, layout.nav_button_height, 
                                   next_page, NULL);
    if (next_button) {
        GooeyCanvas_DrawRectangle(next_button, 0, 0, 
                                 layout.nav_button_width, layout.nav_button_height, 
                                 0x4a4a8a, true, 1.0f, true, 8.0f);
        GooeyContainer_AddWidget(main_window, main_container, 0, next_button);
        GooeyWidget_MakeVisible(next_button, 0);
    }

    next_label = GooeyLabel_Create("Next", layout.label_font_size, 
                                 grid_margin_x + grid_width - layout.nav_button_width / 2 - 10,
                                 nav_button_y + layout.nav_button_height / 2 + 5);
    if (next_label) {
        GooeyLabel_SetColor(next_label, 0xFFFFFF);
        GooeyContainer_AddWidget(main_window, main_container, 0, next_label);
        GooeyWidget_MakeVisible(next_label, 0);
    }

    loading_label = GooeyLabel_Create("Loading applications...", layout.title_font_size,
                                      screen_info.width / 2, screen_info.height / 2);
    if (loading_label) {
        GooeyLabel_SetColor(loading_label, 0xCCCCCC);
        GooeyContainer_AddWidget(main_window, main_container, 0, loading_label);
        GooeyWidget_MakeVisible(loading_label, 1);
    }

    for (int row = 0; row < layout.grid_rows; row++) {
        for (int col = 0; col < layout.grid_cols; col++) {
            const int grid_index = row * layout.grid_cols + col;
            if (grid_index >= MAX_GRID_SIZE) break;

            const int x = grid_margin_x + col * (layout.app_card_width + layout.horizontal_spacing);
            const int y = grid_margin_y + row * (layout.app_card_height + layout.vertical_spacing);

            app_buttons[grid_index] = GooeyCanvas_Create(x, y, 
                                                        layout.app_card_width, layout.app_card_height, 
                                                        launch_app_callback, (void*)(size_t)grid_index);
            if (app_buttons[grid_index]) {
                GooeyCanvas_DrawRectangle(app_buttons[grid_index], 0, 0, 
                                         layout.app_card_width, layout.app_card_height, 
                                         0x2d2d44, true, 1.0f, true, 12.0f);
                GooeyCanvas_DrawRectangle(app_buttons[grid_index], 1, 1, 
                                         layout.app_card_width - 2, layout.app_card_height - 2, 
                                         0x3a3a5a, true, 1.0f, true, 11.0f);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_buttons[grid_index]);
                GooeyWidget_MakeVisible(app_buttons[grid_index], 0);
            }

            app_icons[grid_index] = GooeyImage_Create(NULL, 
                                                     x + 10, y + 25, 
                                                     layout.icon_size, layout.icon_size, 
                                                     NULL, NULL);
            if (app_icons[grid_index]) {
                GooeyContainer_AddWidget(main_window, main_container, 0, app_icons[grid_index]);
                GooeyWidget_MakeVisible(app_icons[grid_index], 0);
            }

            int text_x = x + 25 + layout.icon_size;
            app_name_labels[grid_index] = GooeyLabel_Create("", layout.label_font_size, 
                                                           text_x, y + 40);
            if (app_name_labels[grid_index]) {
                GooeyLabel_SetColor(app_name_labels[grid_index], 0xFFFFFF);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_name_labels[grid_index]);
                GooeyWidget_MakeVisible(app_name_labels[grid_index], 0);
            }

            app_exec_labels[grid_index] = GooeyLabel_Create("", layout.label_font_size - 2, 
                                                           text_x, y + layout.app_card_height - 60);
            if (app_exec_labels[grid_index]) {
                GooeyLabel_SetColor(app_exec_labels[grid_index], 0xAAAAAA);
                GooeyContainer_AddWidget(main_window, main_container, 0, app_exec_labels[grid_index]);
                GooeyWidget_MakeVisible(app_exec_labels[grid_index], 0);
            }
        }
    }

    ui_initialized = 1;
}

static void update_ui(void) {
    if (!ui_initialized) return;

    if (!apps_loaded) {
        if (loading_label) GooeyWidget_MakeVisible(loading_label, 1);

        for (int i = 0; i < layout.apps_per_page; i++) {
            if (app_buttons[i]) GooeyWidget_MakeVisible(app_buttons[i], 0);
            if (app_name_labels[i]) GooeyWidget_MakeVisible(app_name_labels[i], 0);
            if (app_exec_labels[i]) GooeyWidget_MakeVisible(app_exec_labels[i], 0);
            if (app_icons[i]) GooeyWidget_MakeVisible(app_icons[i], 0);
        }

        if (prev_button) GooeyWidget_MakeVisible(prev_button, 0);
        if (prev_label) GooeyWidget_MakeVisible(prev_label, 0);
        if (next_button) GooeyWidget_MakeVisible(next_button, 0);
        if (next_label) GooeyWidget_MakeVisible(next_label, 0);

        if (page_info_label) {
            if (apps_scan_complete) {
                GooeyLabel_SetText(page_info_label, "Processing applications...");
            } else {
                GooeyLabel_SetText(page_info_label, "Scanning for applications...");
            }
        }
        return;
    } else {
        if (loading_label) GooeyWidget_MakeVisible(loading_label, 0);
    }

    size_t total_count = use_filtered_apps ? filtered_app_count : app_count;
    size_t total_pages = (total_count + layout.apps_per_page - 1) / layout.apps_per_page;

    char page_info[128];
    if (use_filtered_apps && search_query[0] != '\0') {
        snprintf(page_info, sizeof(page_info), "Page %zu/%zu • %zu apps • Filter: '%s'",
                 current_page + 1, total_pages, total_count, search_query);
    } else {
        snprintf(page_info, sizeof(page_info), "Page %zu/%zu • %zu applications",
                 current_page + 1, total_pages, total_count);
    }

    if (page_info_label) GooeyLabel_SetText(page_info_label, page_info);

    if (prev_button && prev_label) {
        int prev_visible = (current_page > 0);
        GooeyWidget_MakeVisible(prev_button, prev_visible);
        GooeyWidget_MakeVisible(prev_label, prev_visible);
    }

    if (next_button && next_label) {
        int next_visible = (current_page < total_pages - 1);
        GooeyWidget_MakeVisible(next_button, next_visible);
        GooeyWidget_MakeVisible(next_label, next_visible);
    }

    AppEntry *current_apps = use_filtered_apps ? filtered_apps : detected_apps;

    for (int i = 0; i < layout.apps_per_page; i++) {
        const size_t app_index = current_page * layout.apps_per_page + i;
        int is_visible = (app_index < total_count);

        if (app_buttons[i]) GooeyWidget_MakeVisible(app_buttons[i], is_visible);

        if (app_icons[i]) {
            if (is_visible && current_apps[app_index].icon_path) {
                GooeyImage_SetImage(app_icons[i], current_apps[app_index].icon_path);
                GooeyWidget_MakeVisible(app_icons[i], 1);
            } else {
                GooeyWidget_MakeVisible(app_icons[i], 0);
            }
        }

        if (app_name_labels[i]) {
            if (is_visible) {

                int max_chars = (layout.app_card_width - (15 + layout.icon_size)) / (layout.label_font_size / 2);
                char display_name[100];
                const char *app_name = current_apps[app_index].name;

                if ((int)strlen(app_name) > max_chars) {
                    strncpy(display_name, app_name, max_chars - 3);
                    display_name[max_chars - 3] = '\0';
                    strcat(display_name, "...");
                } else {
                    strcpy(display_name, app_name);
                }
                GooeyLabel_SetText(app_name_labels[i], display_name);
            }
            GooeyWidget_MakeVisible(app_name_labels[i], is_visible);
        }

        if (app_exec_labels[i]) {
            if (is_visible) {

                int max_chars = (layout.app_card_width - (15 + layout.icon_size)) / ((layout.label_font_size - 2) / 2);
                char display_exec[100];
                const char *app_exec = current_apps[app_index].exec;

                if ((int)strlen(app_exec) > max_chars) {
                    strncpy(display_exec, app_exec, max_chars - 3);
                    display_exec[max_chars - 3] = '\0';
                    strcat(display_exec, "...");
                } else {
                    strcpy(display_exec, app_exec);
                }
                GooeyLabel_SetText(app_exec_labels[i], display_exec);
            }
            GooeyWidget_MakeVisible(app_exec_labels[i], is_visible);
        }
    }
}

int main(int argc, char **argv) {
    initialize_application();

    Gooey_Init();

    screen_info = get_screen_resolution();
    printf("Screen resolution: %dx%d\n", screen_info.width, screen_info.height);

    main_window = GooeyWindow_Create("Gooey Application Launcher", screen_info.width, screen_info.height, true);
    if (!main_window) {
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
    GooeyWindow_Cleanup(1, main_window);

    return 0;
}