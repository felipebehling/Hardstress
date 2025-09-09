#ifndef UI_H
#define UI_H

#include "hardstress.h"

GtkWidget* create_main_window(AppContext *app);
void gui_log(AppContext *app, const char *fmt, ...);

#endif // UI_H
