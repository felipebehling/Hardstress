#ifndef UI_H
#define UI_H

#include "hardstress.h"

GtkWidget* create_main_window(AppContext *app);
void gui_log(AppContext *app, const char *fmt, ...);
// ADICIONADO: Declaração da função para ser usada externamente
gboolean gui_update_stopped(gpointer ud);

#endif // UI_H
