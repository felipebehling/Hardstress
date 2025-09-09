#include "ui.h"
#include "core.h"   // Para controller_thread_func
#include "metrics.h"// Para detect_cpu_count
#include "utils.h"  // Para thread_create

/* --- Protótipos de Funções Estáticas (privadas a este arquivo) --- */
static gboolean on_draw_cpu(GtkWidget *widget, cairo_t *cr, gpointer user_data);
static gboolean on_draw_iters(GtkWidget *widget, cairo_t *cr, gpointer user_data);
static void on_btn_start_clicked(GtkButton *b, gpointer ud);
static void on_btn_stop_clicked(GtkButton *b, gpointer ud);
static void on_btn_export_clicked(GtkButton *b, gpointer ud);
static gboolean on_window_delete(GtkWidget *w, GdkEvent *e, gpointer ud);
static void on_window_destroy(GtkWidget *w, gpointer ud);
static gboolean ui_tick(gpointer ud);
static void set_controls_sensitive(AppContext *app, gboolean state);
static void export_csv_dialog(AppContext *app);
static gboolean gui_update_started(gpointer ud);
// A linha abaixo foi modificada
gboolean gui_update_stopped(gpointer ud);

/* --- Implementações --- */

static void on_window_destroy(GtkWidget *w, gpointer ud) {
    (void)w;
    AppContext *app = (AppContext*)ud;

    // Limpeza que antes estava em main.c
    if (atomic_load(&app->running)) {
        atomic_store(&app->running, 0);
    }
    g_mutex_clear(&app->cpu_mutex);
    g_mutex_clear(&app->history_mutex);
    g_mutex_clear(&app->temp_mutex);
    free(app);
    
    // Finaliza o loop do GTK
    gtk_main_quit();
}


void gui_log(AppContext *app, const char *fmt, ...){
    va_list ap; va_start(ap, fmt);
    char *s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    if (!s) return;
    GtkTextIter end; gtk_text_buffer_get_end_iter(app->log_buffer, &end);
    gtk_text_buffer_insert(app->log_buffer, &end, s, -1);
    g_free(s);
}

static gboolean gui_update_started(gpointer ud){
    AppContext *app = (AppContext*)ud;
    gtk_widget_set_sensitive(app->btn_stop, TRUE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Rodando...");
    gui_log(app, "[GUI] Teste iniciado: threads=%d mem/thread=%zu dur=%ds pin=%d\n",
            app->threads, app->mem_mib_per_thread, app->duration_sec, app->pin_affinity);
    return G_SOURCE_REMOVE;
}

// MODIFICADO: Removido 'static' para que a função seja visível por core.c
gboolean gui_update_stopped(gpointer ud){
    AppContext *app = (AppContext*)ud;
    set_controls_sensitive(app, TRUE);
    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Parado");
    gui_log(app, "[GUI] Teste parado.\n");
    return G_SOURCE_REMOVE;
}

static void on_btn_start_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    if (atomic_load(&app->running)) return;

    char *end;
    long threads = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_threads)), &end, 10);
    if (*end != '\0' || threads < 0){ gui_log(app, "[GUI] threads invalido\n"); return; }
    long mem = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_mem)), &end, 10);
    if (*end != '\0' || mem <= 0){ gui_log(app, "[GUI] memoria invalida\n"); return; }
    long dur = strtol(gtk_entry_get_text(GTK_ENTRY(app->entry_dur)), &end, 10);
    if (*end != '\0' || dur < 0){ gui_log(app, "[GUI] duracao invalida\n"); return; }

    app->threads = (threads == 0) ? detect_cpu_count() : (int)threads;
    app->mem_mib_per_thread = (size_t)mem;
    app->duration_sec = (int)dur;
    app->pin_affinity = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_pin));
    app->kernel_fpu_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_fpu));
    app->kernel_int_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_int));
    app->kernel_stream_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_stream));
    app->kernel_ptr_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_ptr));
    app->csv_realtime_en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_csv_realtime));
    
    if (!app->kernel_fpu_en && !app->kernel_int_en && !app->kernel_stream_en && !app->kernel_ptr_en) {
        gui_log(app, "[GUI] ERRO: Pelo menos um kernel de stress deve ser selecionado.\n");
        return;
    }

    set_controls_sensitive(app, FALSE);
    g_idle_add(gui_update_started, app);
    thread_create(&app->controller_thread, (thread_func_t)controller_thread_func, app);
}

static void on_btn_stop_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    if (!atomic_load(&app->running)) return;
    atomic_store(&app->running, 0);
    gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gui_log(app, "[GUI] stop requested by user\n");
}

static void on_btn_export_clicked(GtkButton *b, gpointer ud){
    (void)b;
    AppContext *app = (AppContext*)ud;
    export_csv_dialog(app);
}

static gboolean on_window_delete(GtkWidget *w, GdkEvent *e, gpointer ud){
    (void)w; (void)e;
    AppContext *app = (AppContext*)ud;
    if (atomic_load(&app->running)){
        gui_log(app, "[GUI] fechando: solicitando parada...\n");
        atomic_store(&app->running, 0);
        struct timespec r = {1, 500000000}; nanosleep(&r,NULL);
    }
    return FALSE; // Permite que a janela feche
}

static gboolean ui_tick(gpointer ud){
    AppContext *app = (AppContext*)ud;
    if (!atomic_load(&app->running)) {
        // Redundante com gui_update_stopped mas garante o estado final
        if (strcmp(gtk_label_get_text(GTK_LABEL(app->status_label)), "Parado") != 0) {
            gtk_label_set_text(GTK_LABEL(app->status_label), "Parado");
        }
        return TRUE; // Mantém o timer rodando
    }
    static unsigned long long last_total = 0;
    unsigned long long cur = atomic_load(&app->total_iters);
    unsigned long long diff = cur - last_total;
    last_total = cur;
    char buf[128];
    snprintf(buf, sizeof(buf), "iters/s=%llu errs=%d", diff, atomic_load(&app->errors));
    gtk_label_set_text(GTK_LABEL(app->status_label), buf);
    return TRUE;
}

GtkWidget* create_main_window(AppContext *app) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 800);
    gtk_window_set_title(GTK_WINDOW(win), "HardStress GUI - Complete");

    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(win), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    int row = 0;
    // --- Basic Config ---
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Threads (0=auto):"), 0, row, 1, 1);
    app->entry_threads = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->entry_threads), "0");
    gtk_grid_attach(GTK_GRID(grid), app->entry_threads, 1, row++, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Mem (MiB/thread):"), 0, row, 1, 1);
    app->entry_mem = gtk_entry_new();
    char mem_buf[32]; snprintf(mem_buf, sizeof(mem_buf), "%zu", app->mem_mib_per_thread);
    gtk_entry_set_text(GTK_ENTRY(app->entry_mem), mem_buf);
    gtk_grid_attach(GTK_GRID(grid), app->entry_mem, 1, row++, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Duração (s, 0=inf):"), 0, row, 1, 1);
    app->entry_dur = gtk_entry_new();
    char dur_buf[32]; snprintf(dur_buf, sizeof(dur_buf), "%d", app->duration_sec);
    gtk_entry_set_text(GTK_ENTRY(app->entry_dur), dur_buf);
    gtk_grid_attach(GTK_GRID(grid), app->entry_dur, 1, row++, 1, 1);

    // --- Kernels ---
    GtkWidget *kernel_frame = gtk_frame_new("Kernels de Stress");
    GtkWidget *kernel_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_container_add(GTK_CONTAINER(kernel_frame), kernel_box);
    app->check_fpu = gtk_check_button_new_with_label("FPU (Ponto Flutuante)");
    app->check_int = gtk_check_button_new_with_label("Inteiros (ALU)");
    app->check_stream = gtk_check_button_new_with_label("Memoria (Stream)");
    app->check_ptr = gtk_check_button_new_with_label("Memoria (Ponteiros)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_fpu), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_int), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_stream), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_ptr), TRUE);
    gtk_box_pack_start(GTK_BOX(kernel_box), app->check_fpu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kernel_box), app->check_int, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kernel_box), app->check_stream, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(kernel_box), app->check_ptr, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), kernel_frame, 0, row++, 2, 1);


    app->check_pin = gtk_check_button_new_with_label("Pin threads to CPUs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_pin), TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->check_pin, 0, row++, 2, 1);
    
    app->check_csv_realtime = gtk_check_button_new_with_label("Log CSV em tempo real");
    gtk_grid_attach(GTK_GRID(grid), app->check_csv_realtime, 0, row++, 2, 1);

    // --- Controls ---
    app->btn_start = gtk_button_new_with_label("Start");
    app->btn_stop = gtk_button_new_with_label("Stop"); gtk_widget_set_sensitive(app->btn_stop, FALSE);
    gtk_grid_attach(GTK_GRID(grid), app->btn_start, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->btn_stop, 1, row++, 1, 1);

    app->btn_export = gtk_button_new_with_label("Export CSV (Em Memoria)");
    gtk_grid_attach(GTK_GRID(grid), app->btn_export, 0, row++, 2, 1);

    app->status_label = gtk_label_new("idle");
    gtk_grid_attach(GTK_GRID(grid), app->status_label, 0, row++, 2, 1);

    // --- Drawings ---
    app->cpu_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->cpu_drawing, 0, 100);
    gtk_grid_attach(GTK_GRID(grid), app->cpu_drawing, 0, row++, 2, 1);

    app->iters_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->iters_drawing, 0, 200);
    gtk_grid_attach(GTK_GRID(grid), app->iters_drawing, 0, row++, 2, 1);

    // --- Log ---
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL); gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_grid_attach(GTK_GRID(grid), scrolled, 0, row++, 2, 1);
    GtkWidget *text = gtk_text_view_new(); gtk_text_view_set_editable(GTK_
