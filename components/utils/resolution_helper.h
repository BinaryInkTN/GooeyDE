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

static ScreenInfo get_screen_resolution()
{
    ScreenInfo info = {0};
    info.display = XOpenDisplay(NULL);
    if (!info.display)
    {
        fprintf(stderr, "Unable to open X display\n");
        return info;
    }

    info.screen = DefaultScreen(info.display);

    int event_base, error_base;
    if (XRRQueryExtension(info.display, &event_base, &error_base))
    {
        XRRScreenResources *resources = XRRGetScreenResources(info.display, RootWindow(info.display, info.screen));
        if (resources)
        {

            for (int i = 0; i < resources->noutput; i++)
            {
                XRROutputInfo *output_info = XRRGetOutputInfo(info.display, resources, resources->outputs[i]);
                if (output_info && output_info->connection == RR_Connected && output_info->crtc)
                {
                    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(info.display, resources, output_info->crtc);
                    if (crtc_info && crtc_info->width > 0 && crtc_info->height > 0)
                    {

                        info.width = crtc_info->width;
                        info.height = crtc_info->height;
                        if (crtc_info)
                            XRRFreeCrtcInfo(crtc_info);
                        if (output_info)
                            XRRFreeOutputInfo(output_info);
                        break;
                    }
                    if (crtc_info)
                        XRRFreeCrtcInfo(crtc_info);
                }
                if (output_info)
                    XRRFreeOutputInfo(output_info);
            }
            XRRFreeScreenResources(resources);
        }
    }

    if (info.width == 0 || info.height == 0)
    {
        info.width = DisplayWidth(info.display, info.screen);
        info.height = DisplayHeight(info.display, info.screen);
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