#include "hardstress.h"
#include <stdio.h>
#include <stdarg.h>

/*
 * This is a stub implementation of gui_log for the test suite.
 * The real implementation is in ui.c and depends on the GTK UI.
 * This stub allows us to link test executables without pulling in the UI.
 */
void gui_log(AppContext *app, const char *fmt, ...) {
    // This is a test stub, so we can either do nothing
    // or print the log to the console for debugging purposes.
    // For now, we'll just print to stdout.
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
