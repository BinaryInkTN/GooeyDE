#ifndef RESOLUTION_HELPER_H
#define RESOLUTION_HELPER_H

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int width;
    int height;
    int screen;
    Display *display;
} ScreenInfo;

  ScreenInfo get_screen_resolution(void) {
    ScreenInfo info = {1920, 1080}; // Default fallback
    
    FILE *fp = popen("xrandr 2>/dev/null | grep ' connected primary'", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            // Parse resolution from xrandr output
            char *res_start = strstr(line, " connected primary ");
            if (res_start) {
                res_start += 19; // Move past " connected primary "
                char *res_end = strchr(res_start, ' ');
                if (res_end) {
                    char resolution[32];
                    size_t len = res_end - res_start;
                    if (len < sizeof(resolution)) {
                        strncpy(resolution, res_start, len);
                        resolution[len] = '\0';
                        
                        // Parse width and height
                        if (sscanf(resolution, "%dx%d", &info.width, &info.height) == 2) {
                            pclose(fp);
                            return info;
                        }
                    }
                }
            }
        }
        pclose(fp);
    }
    
    // Fallback: try any connected display
    fp = popen("xrandr 2>/dev/null | grep ' connected' | head -1", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            // Look for resolution pattern
            char *res_start = strstr(line, " connected ");
            if (res_start) {
                res_start += 11;
                char *res_end = strchr(res_start, ' ');
                if (res_end) {
                    char resolution[32];
                    size_t len = res_end - res_start;
                    if (len < sizeof(resolution)) {
                        strncpy(resolution, res_start, len);
                        resolution[len] = '\0';
                        
                        // Parse width and height
                        if (sscanf(resolution, "%dx%d", &info.width, &info.height) == 2) {
                            pclose(fp);
                            return info;
                        }
                    }
                }
            }
        }
        pclose(fp);
    }
    
    return info;
}


static void cleanup_screen_info(ScreenInfo *info)
{
    if (info->display)
    {
        XCloseDisplay(info->display);
        info->display = NULL;
    }
}

#endif