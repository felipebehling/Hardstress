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
gboolean gui_update_stopped(gpointer ud);

/* --- Implementações --- */

static void on_window_destroy(GtkWidget *w, gpointer ud) {
    (void)w;
    AppContext *app = (AppContext*)ud;

    if (atomic_load(&app->running)) {
        atomic_store(&app->running, 0);
    }
    g_mutex_clear(&app->cpu_mutex);
    g_mutex_clear(&app->history_mutex);
    g_mutex_clear(&app->temp_mutex);
    free(app);

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
    return FALSE;
}

static gboolean ui_tick(gpointer ud){
    AppContext *app = (AppContext*)ud;
    if (!atomic_load(&app->running)) {
        if (strcmp(gtk_label_get_text(GTK_LABEL(app->status_label)), "Parado") != 0) {
            gtk_label_set_text(GTK_LABEL(app->status_label), "Parado");
        }
        return TRUE;
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
    gtk_window_set_default_size(GTK_WINDOW(win), 1200, 800);
    gtk_window_set_title(GTK_WINDOW(win), "HardStress GUI");

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(win), paned);
    gtk_paned_set_position(GTK_PANED(paned), 280);

    // PAINEL ESQUERDO
    GtkWidget *left_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(left_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(left_grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(left_grid), 10);
    gtk_paned_pack1(GTK_PANED(paned), left_grid, FALSE, FALSE);

    int row = 0;
    gtk_grid_attach(GTK_GRID(left_grid), gtk_label_new("Threads (0=auto):"), 0, row, 1, 1);
    app->entry_threads = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->entry_threads), "0");
    gtk_grid_attach(GTK_GRID(left_grid), app->entry_threads, 1, row++, 1, 1);

    gtk_grid_attach(GTK_GRID(left_grid), gtk_label_new("Mem (MiB/thread):"), 0, row, 1, 1);
    app->entry_mem = gtk_entry_new();
    char mem_buf[32]; snprintf(mem_buf, sizeof(mem_buf), "%zu", app->mem_mib_per_thread);
    gtk_entry_set_text(GTK_ENTRY(app->entry_mem), mem_buf);
    gtk_grid_attach(GTK_GRID(left_grid), app->entry_mem, 1, row++, 1, 1);

    gtk_grid_attach(GTK_GRID(left_grid), gtk_label_new("Duração (s, 0=inf):"), 0, row, 1, 1);
    app->entry_dur = gtk_entry_new();
    char dur_buf[32]; snprintf(dur_buf, sizeof(dur_buf), "%d", app->duration_sec);
    gtk_entry_set_text(GTK_ENTRY(app->entry_dur), dur_buf);
    gtk_grid_attach(GTK_GRID(left_grid), app->entry_dur, 1, row++, 1, 1);

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
    gtk_grid_attach(GTK_GRID(left_grid), kernel_frame, 0, row++, 2, 1);

    app->check_pin = gtk_check_button_new_with_label("Fixar threads em CPUs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_pin), TRUE);
    gtk_grid_attach(GTK_GRID(left_grid), app->check_pin, 0, row++, 2, 1);

    app->check_csv_realtime = gtk_check_button_new_with_label("Log CSV em tempo real");
    gtk_grid_attach(GTK_GRID(left_grid), app->check_csv_realtime, 0, row++, 2, 1);

    app->btn_start = gtk_button_new_with_label("Start");
    app->btn_stop = gtk_button_new_with_label("Stop");
    gtk_widget_set_sensitive(app->btn_stop, FALSE);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(button_box), app->btn_start, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), app->btn_stop, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(left_grid), button_box, 0, row++, 2, 1);

    app->btn_export = gtk_button_new_with_label("Exportar CSV (Snapshot)");
    gtk_grid_attach(GTK_GRID(left_grid), app->btn_export, 0, row++, 2, 1);

    app->status_label = gtk_label_new("Pronto");
    gtk_grid_attach(GTK_GRID(left_grid), app->status_label, 0, row++, 2, 1);

    // PAINEL DIREITO
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(right_box), 10);
    gtk_paned_pack2(GTK_PANED(paned), right_box, TRUE, TRUE);

    app->cpu_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->cpu_drawing, -1, 100);
    gtk_box_pack_start(GTK_BOX(right_box), app->cpu_drawing, FALSE, FALSE, 0);

    app->iters_drawing = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->iters_drawing, -1, 200);
    gtk_box_pack_start(GTK_BOX(right_box), app->iters_drawing, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    app->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    gtk_box_pack_start(GTK_BOX(right_box), scrolled, TRUE, TRUE, 0);

    // SINAIS
    g_signal_connect(win, "destroy", G_CALLBACK(on_window_destroy), app);
    g_signal_connect(win, "delete-event", G_CALLBACK(on_window_delete), app);
    g_signal_connect(app->btn_start, "clicked", G_CALLBACK(on_btn_start_clicked), app);
    g_signal_connect(app->btn_stop, "clicked", G_CALLBACK(on_btn_stop_clicked), app);
    g_signal_connect(app->btn_export, "clicked", G_CALLBACK(on_btn_export_clicked), app);
    g_signal_connect(app->cpu_drawing, "draw", G_CALLBACK(on_draw_cpu), app);
    g_signal_connect(app->iters_drawing, "draw", G_CALLBACK(on_draw_iters), app);

    g_timeout_add(1000, ui_tick, app);

    return win;
}

static void set_controls_sensitive(AppContext *app, gboolean state){
    gtk_widget_set_sensitive(app->entry_threads, state);
    gtk_widget_set_sensitive(app->entry_mem, state);
    gtk_widget_set_sensitive(app->entry_dur, state);
    gtk_widget_set_sensitive(app->check_pin, state);
    gtk_widget_set_sensitive(app->check_fpu, state);
    gtk_widget_set_sensitive(app->check_int, state);
    gtk_widget_set_sensitive(app->check_stream, state);
    gtk_widget_set_sensitive(app->check_ptr, state);
    gtk_widget_set_sensitive(app->check_csv_realtime, state);
    gtk_widget_set_sensitive(app->btn_start, state);
}

static void export_csv_dialog(AppContext *app){
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Exportar CSV", GTK_WINDOW(app->win),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancelar", GTK_RESPONSE_CANCEL, "_Salvar", GTK_RESPONSE_ACCEPT, NULL);

    char default_name[64];
    snprintf(default_name, sizeof(default_name), "HardStress_ManualExport_%.0f.csv", (double)time(NULL));
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), default_name);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT){
        char *fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        FILE *f = fopen(fname, "w");
        if (!f){
            gui_log(app, "[GUI] Falha ao abrir %s para escrita\n", fname);
            g_free(fname);
            gtk_widget_destroy(dialog);
            return;
        }

        fprintf(f, "timestamp");
        for (int c=0;c<app->cpu_count;c++) fprintf(f, ",cpu%d_usage", c);
        for (int t=0;t<app->threads;t++) fprintf(f, ",thread%d_iters", t);
        fprintf(f, ",temp_celsius\n");

        g_mutex_lock(&app->history_mutex);
        for (int s=0;s<app->history_len;s++){
            int idx = (app->history_pos + 1 + s) % app->history_len;
            fprintf(f, "%.3f", (double)s * (CPU_SAMPLE_INTERVAL_MS / 1000.0));
             for (int c=0;c<app->cpu_count;c++) fprintf(f, ",-1.0");
            if(app->thread_history) {
              for (int t=0;t<app->threads;t++) fprintf(f, ",%u", app->thread_history[t][idx]);
            }
            fprintf(f, ",-1.0\n");
        }
        g_mutex_unlock(&app->history_mutex);

        fclose(f);
        gui_log(app, "[GUI] CSV (snapshot da memória) exportado para %s\n", fname);
        g_free(fname);
    }
    gtk_widget_destroy(dialog);
}

static gboolean on_draw_cpu(GtkWidget *widget, cairo_t *cr, gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    GtkAllocation alloc; gtk_widget_get_allocation(widget, &alloc);
    int w = alloc.width, h = alloc.height;
    int n = app->cpu_count > 0 ? app->cpu_count : 1;
    int bw = w / n;

    g_mutex_lock(&app->cpu_mutex);
    for (int i=0;i<n;i++){
        double u = (app->cpu_usage && i < app->cpu_count) ? app->cpu_usage[i] : 0.0;
        int x = i * bw;
        int bar_h = (int)(u * h);
        cairo_set_source_rgb(cr, COLOR_BG.r, COLOR_BG.g, COLOR_BG.b);
        cairo_rectangle(cr, x, 0, bw-2, h); cairo_fill(cr);
        cairo_set_source_rgb(cr, COLOR_FG.r, COLOR_FG.g, COLOR_FG.b);
        cairo_rectangle(cr, x+1, h - bar_h, bw-4, bar_h); cairo_fill(cr);
        char txt[32]; snprintf(txt, sizeof(txt), "%.0f%%", u*100.0);
        cairo_set_source_rgb(cr, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, x+4, 12); cairo_show_text(cr, txt);
    }
    g_mutex_unlock(&app->cpu_mutex);

    g_mutex_lock(&app->temp_mutex);
    double temp = app->temp_celsius;
    g_mutex_unlock(&app->temp_mutex);

    if (temp > TEMP_UNAVAILABLE){
        char tbuf[64]; snprintf(tbuf, sizeof(tbuf), "Temp: %.2f C", temp);
        cairo_set_source_rgb(cr, COLOR_TEMP.r, COLOR_TEMP.g, COLOR_TEMP.b);
        cairo_move_to(cr, 6, h - 6);
        cairo_show_text(cr, tbuf);
    }
    return FALSE;
}

static gboolean on_draw_iters(GtkWidget *widget, cairo_t *cr, gpointer user_data){
    AppContext *app = (AppContext*)user_data;
    if (!atomic_load(&app->running) || !app->workers) return FALSE;

    GtkAllocation alloc; gtk_widget_get_allocation(widget, &alloc);
    int W = alloc.width, H = alloc.height;
    int tcount = app->threads > 0 ? app->threads : 1;
    int margin = 4;
    int area_h = (H - (tcount+1)*margin) / tcount;

    g_mutex_lock(&app->history_mutex);
    for (int t=0; t < app->threads; t++){
        int y0 = margin + t*(area_h+margin);
        worker_status_t status = atomic_load(&app->workers[t].status);

        cairo_set_source_rgb(cr, 0.06,0.06,0.06);
        cairo_rectangle(cr, 0, y0, W, area_h); cairo_fill(cr);

        if (status == WORKER_ALLOC_FAIL) {
            cairo_set_source_rgb(cr, COLOR_ERR.r, COLOR_ERR.g, COLOR_ERR.b);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 14);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, "ALLOC FAILED", &extents);
            cairo_move_to(cr, W/2.0 - extents.width/2.0, y0 + area_h/2.0);
            cairo_show_text(cr, "ALLOC FAILED");
        } else {
            cairo_set_source_rgb(cr, COLOR_WARN.r, COLOR_WARN.g, COLOR_WARN.b);
            cairo_set_line_width(cr, 1.0);
            int samples = app->history_len;
            double step = (samples > 1) ? ((double)W / (samples - 1)) : W;

            int start_idx = (app->history_pos + 1) % samples;
            unsigned last_v = app->thread_history ? app->thread_history[t][start_idx] : 0;

            for (int s = 0; s < samples; s++) {
                int current_idx = (start_idx + s) % samples;
                unsigned current_v = app->thread_history ? app->thread_history[t][current_idx] : 0;

                double y_val = ((double)(current_v - last_v)) / ITER_SCALE;
                double y = y0 + area_h - y_val * area_h;
                if (y < y0) y = y0; else if (y > y0 + area_h) y = y0 + area_h;

                if (s == 0) cairo_move_to(cr, s * step, y);
                else cairo_line_to(cr, s * step, y);

                last_v = current_v;
            }
            cairo_stroke(cr);
        }

        cairo_set_source_rgb(cr, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, 6, y0 + 12);
        char lbl[64]; snprintf(lbl, sizeof(lbl), "T%02d iters/s (x%.0f)", t, ITER_SCALE / (CPU_SAMPLE_INTERVAL_MS/1000.0));
        cairo_show_text(cr, lbl);
    }
    g_mutex_unlock(&app->history_mutex);
    return FALSE;
}

