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


    GooeyShell_RunEventLoop(desktop);

    GooeyShell_Cleanup(desktop);
    return 0;
}