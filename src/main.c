/**
 * @file main.c
 * @brief The main entry point for the HardStress application.
 *
 * This file contains the `main` function, which initializes the application,
 * including GTK, the main AppContext structure, and the user interface.
 * It also defines the global color constants for the UI theme.
 */
#include "hardstress.h"
#include "ui.h" // Required for create_main_window

// Define the global colors that were declared in the header.
const color_t COLOR_BG = {0.12, 0.12, 0.12};
const color_t COLOR_FG = {0.15, 0.65, 0.90};
const color_t COLOR_WARN = {0.8, 0.4, 0.1};
const color_t COLOR_ERR = {0.9, 0.2, 0.2};
const color_t COLOR_TEXT = {1.0, 1.0, 1.0};
const color_t COLOR_TEMP = {1.0, 1.0, 0.8};

/**
 * @brief The main entry point of the HardStress application.
 *
 * This function performs the following steps:
 * 1. Initializes the GTK toolkit.
 * 2. Allocates and initializes the main `AppContext` structure, which holds
 *    the entire application state.
 * 3. Initializes mutexes for thread-safe data access.
 * 4. Sets default configuration values.
 * 5. Calls `create_main_window` to build the GUI.
 * 6. Shows the main window and starts the GTK main event loop.
 *
 * Cleanup of resources is handled in the `on_window_destroy` callback in `ui.c`.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of command-line argument strings.
 * @return 0 on successful execution, 1 on failure.
 */
int main(int argc, char **argv){
    gtk_init(&argc, &argv);
    
    // Allocate and zero out the main application structure
    AppContext *app = calloc(1, sizeof(AppContext));
    if (!app) {
        fprintf(stderr, "Failed to allocate AppContext. Exiting.\n");
        return 1;
    }

    // Initialize mutexes
    g_mutex_init(&app->cpu_mutex);
    g_mutex_init(&app->history_mutex);
    g_mutex_init(&app->temp_mutex);
    
    // Set default configuration
    app->mem_mib_per_thread = DEFAULT_MEM_MIB;
    app->duration_sec = DEFAULT_DURATION_SEC;
    app->pin_affinity = 1;
    app->history_len = HISTORY_SAMPLES;
    app->temp_celsius = TEMP_UNAVAILABLE;

    // Create the main window
    app->win = create_main_window(app);

    gui_log(app, "[GUI] Ready\n");
    gtk_widget_show_all(app->win);
    
    // Start the GTK main event loop
    gtk_main();

    // NOTE: Cleanup is handled in the on_window_destroy callback in ui.c
    
    return 0;
}
