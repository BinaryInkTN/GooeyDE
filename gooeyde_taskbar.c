#include "gooey.h"

void run_test_app() {
    if (fork() == 0) {
        execl("./canvas_test", "./canvas_test", NULL);
        perror("Failed to launch test app");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    Gooey_Init();
    GooeyWindow *win = GooeyWindow_Create("Gooey Taskbar", 1024, 768, true);
    GooeyImage* wallpaper = GooeyImage_Create("bg.jpg", 0, 50, 1024, 768, false);
    GooeyCanvas* canvas = GooeyCanvas_Create(0, 0, 1024, 768, NULL);
    GooeyImage* apps_icon = GooeyImage_Create("apps.png", 10, 10, 30, 30, NULL);
    GooeyCanvas_DrawRectangle(canvas, 0, 0, 1024, 50, 0x222222, true, 1.0f, true, 1.0f);
    GooeyLabel* time_label = GooeyLabel_Create("17:35 PM", 0.3f, 920, 30);
    GooeyLabel_SetColor(time_label, 0xFFFFFF);
    GooeyCanvas_DrawLine(canvas, 50, 0, 50, 50, 0xFFFFFF);
    GooeyImage* test_app = GooeyImage_Create("test_app.png", 65, 10, 30, 30, run_test_app);
    GooeyWindow_RegisterWidget(win, test_app);
    GooeyWindow_RegisterWidget(win, time_label);
    GooeyWindow_RegisterWidget(win, canvas);
    GooeyWindow_RegisterWidget(win, wallpaper);
    GooeyWindow_RegisterWidget(win, apps_icon);
    GooeyWindow_Run(1, win);
    GooeyWindow_Cleanup(1, win);
    return 0;
}