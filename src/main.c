#include "hardstress.h"

// Define as cores globais que foram declaradas no .h
const color_t COLOR_BG = {0.12, 0.12, 0.12};
const color_t COLOR_FG = {0.15, 0.65, 0.90};
const color_t COLOR_WARN = {0.8, 0.4, 0.1};
const color_t COLOR_ERR = {0.9, 0.2, 0.2};
const color_t COLOR_TEXT = {1.0, 1.0, 1.0};
const color_t COLOR_TEMP = {1.0, 1.0, 0.8};

int main(int argc, char **argv){
    gtk_init(&argc, &argv);
    
    // Aloca e zera a estrutura principal
    AppContext *app = calloc(1, sizeof(AppContext));
    if (!app) {
        fprintf(stderr, "Falha ao alocar AppContext. Saindo.\n");
        return 1;
    }

    g_mutex_init(&app->cpu_mutex);
    g_mutex_init(&app->history_mutex);
    g_mutex_init(&app->temp_mutex);
    
    // Configurações padrão
    app->mem_mib_per_thread = DEFAULT_MEM_MIB;
    app->duration_sec = DEFAULT_DURATION_SEC;
    app->pin_affinity = 1;
    app->history_len = HISTORY_SAMPLES;
    app->temp_celsius = TEMP_UNAVAILABLE;

    // Cria a janela principal
    app->win = create_main_window(app);

    gui_log(app, "[GUI] pronto\n");
    gtk_widget_show_all(app->win);
    
    // Inicia o loop da interface gráfica
    gtk_main();

    // Limpeza ao sair
    if (atomic_load(&app->running)) atomic_store(&app->running, 0);
    g_mutex_clear(&app->cpu_mutex);
    g_mutex_clear(&app->history_mutex);
    g_mutex_clear(&app->temp_mutex);
    free(app);
    
    return 0;
}
