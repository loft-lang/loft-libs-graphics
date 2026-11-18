/* TRIAL: how often does opening a display fail under Xvfb? */
#include <X11/Xlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    int n = atoi(argv[1]), window = atoi(argv[2]), wait_us = argc > 3 ? atoi(argv[3]) : 0, i, failed = 0;
    for (i = 0; i < n; i++) {
        errno = 0;
        Display *d = XOpenDisplay(NULL);
        if (!d) {
            if (failed < 5) printf("open %d failed: errno %d (%s)\n", i, errno, strerror(errno));
            failed++;
            if (wait_us) usleep(wait_us);
            continue;
        }
        if (window) {
            Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 16, 16, 0, 0, 0);
            XSelectInput(d, w, StructureNotifyMask);
            XMapWindow(d, w);
            for (;;) { XEvent e; XNextEvent(d, &e); if (e.type == MapNotify) break; }
            XDestroyWindow(d, w);
        }
        XCloseDisplay(d);
    }
    printf("pid %d: %d of %d opens failed (window %d)\n", getpid(), failed, n, window);
    return 0;
}
