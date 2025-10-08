#include "gooey_shell.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "GLPS/glps_thread.h"

int main()
{
    GooeyShellState *desktop = GooeyShell_Init();
    if (!desktop)
    {
        fprintf(stderr, "Failed to initialize desktop environment\n");
        return 1;
    }

    printf("GooeyShell Desktop Environment Started\n");
    printf("Press Alt+F4 to close focused window\n");
    printf("Press Alt+F11 to toggle fullscreen\n");


    printf("Launching taskbar as desktop app...\n");
    pid_t taskbar_pid = fork();
    if (taskbar_pid == 0)
    {

        setenv("GOOEY_DESKTOP_APP", "1", 1);
        execl("./gooeyde_desktop", "./gooeyde_desktop", NULL);
        perror("Failed to launch taskbar");
        exit(1);
    }
    //GooeyShell_AddFullscreenApp(desktop, "gooeyde_appmenu", 1);

    sleep(2);
/*  printf("Launching canvas test as regular window...\n");
    pid_t canvas_pid = fork();
    if (canvas_pid == 0)
    {

        execl("./canvas_test", "./canvas_test", NULL);
        perror("Failed to launch canvas test");
        exit(1);
    }*/
  

    printf("Launched applications:\n");
    printf("- Taskbar (desktop app): PID %d\n", taskbar_pid);
   // printf("- Canvas test (regular window): PID %d\n", canvas_pid);
    printf("Entering main event loop...\n");

    GooeyShell_RunEventLoop(desktop);

    GooeyShell_Cleanup(desktop);
    return 0;
}