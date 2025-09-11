#ifndef UI_H
#define UI_H

/**
 * @file ui.h
 * @brief Declares functions related to the GTK-based Graphical User Interface.
 *
 * This module is responsible for creating, managing, and updating all GUI
 * elements, including the main window, graphs, input fields, and event log.
 * It also handles user interactions and triggers the core application logic.
 */

#include "hardstress.h"

/**
 * @brief Creates and initializes the main GTK window and all its widgets.
 *
 * This function builds the entire user interface, connecting signals for
 * buttons and other interactive elements to their respective callback handlers.
 *
 * @param app A pointer to the global `AppContext` structure.
 * @return A pointer to the newly created `GtkWindow`.
 */
GtkWidget* create_main_window(AppContext *app);

/**
 * @brief Logs a formatted message to the GUI's event log panel.
 *
 * This function is thread-safe and can be called from any thread to append
 * a timestamped message to the log view in the UI.
 *
 * @param app A pointer to the global `AppContext` structure.
 * @param fmt The `printf`-style format string for the message.
 * @param ... Variable arguments for the format string.
 */
void gui_log(AppContext *app, const char *fmt, ...);

/**
 * @brief A GSourceFunc to update the GUI after a test has stopped.
 *
 * This function is called via `g_idle_add` from the controller thread once
 * a test is complete. It re-enables the configuration controls and updates
 * the status label.
 *
 * @param ud A pointer to the global `AppContext` structure.
 * @return G_SOURCE_REMOVE to ensure the function is only called once.
 */
gboolean gui_update_stopped(gpointer ud);

#endif // UI_H
