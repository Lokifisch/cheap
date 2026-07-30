#include "ui.h"
#include "enchant_data.h"
#include "optimizer.h"
#include <string.h>
#include <stdio.h>

#define ENCHANTMENT_LIMIT_INCLUSIVE 10

typedef struct {
    GtkWidget *window;

    GtkDropDown *item_dropdown;
    int *dropdown_item_index; /* dropdown position -> index into g_items, or -1 for the placeholder */

    GtkWidget *enchant_placeholder;
    GtkWidget *enchant_scroller;
    GtkWidget *enchant_grid;

    GtkWidget *check_incompatible;
    GtkWidget *check_many;
    GtkWidget *mode_xp;
    GtkWidget *mode_prior_work;

    GtkWidget *calculate_button;
    GtkWidget *spinner;
    GtkWidget *status_label;

    GtkWidget *results_box;
    GtkWidget *results_header;
    GtkWidget *results_list;
    GtkWidget *results_timing;

    int current_item_index; /* index into g_items; meaningless until has_item_selected */
    gboolean current_is_book;
    gboolean has_item_selected;

    gboolean suppress_toggle_handling;
} AppState;

typedef struct {
    AppState *app;

    EnchantSelection *selections;
    int n;
    int item_index;
    gboolean is_book;
    OptimizeMode mode;

    gint64 start_time_us;

    OptimizeResult *result; /* filled in by the worker thread */
    gint64 elapsed_us;
} CalcJob;

static void rebuild_enchant_grid(AppState *app, int item_index, gboolean is_book);
static void update_calculate_button_state(AppState *app);
static void clear_results(AppState *app);

/* ---------- icons ---------- */

static GdkTexture *get_item_icon(int item_index) {
    static GdkTexture *cache[64];
    static gboolean loaded[64];

    if (item_index < 0 || item_index >= g_item_count) return NULL;
    if (!loaded[item_index]) {
        loaded[item_index] = TRUE;
        char path[256];
        snprintf(path, sizeof(path), "/dev/lokifisch/cheap/icons/%s.gif", g_items[item_index].id);
        cache[item_index] = gdk_texture_new_from_resource(path);
    }
    return cache[item_index];
}

static int book_item_index(void) {
    static int idx = -2;
    if (idx == -2) idx = item_index_by_id("book");
    return idx;
}

/* ---------- selection bookkeeping ---------- */

static void for_each_level_button(AppState *app, void (*fn)(AppState *, GtkWidget *, int, int, gpointer), gpointer data) {
    if (!app->enchant_grid) return;
    for (GtkWidget *child = gtk_widget_get_first_child(app->enchant_grid);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        if (!GTK_IS_TOGGLE_BUTTON(child)) continue;
        int enchant_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "enchant-index"));
        int level = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "level"));
        fn(app, child, enchant_index, level, data);
    }
}

static void count_cb(AppState *app, GtkWidget *btn, int enchant_index, int level, gpointer data) {
    (void)app; (void)enchant_index; (void)level;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) (*(int *)data)++;
}

static int count_selected(AppState *app) {
    int n = 0;
    for_each_level_button(app, count_cb, &n);
    return n;
}

typedef struct { EnchantSelection *out; int count; } GatherCtx;

static void gather_cb(AppState *app, GtkWidget *btn, int enchant_index, int level, gpointer data) {
    (void)app;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
    GatherCtx *ctx = data;
    ctx->out[ctx->count].enchant_index = enchant_index;
    ctx->out[ctx->count].level = level;
    ctx->count++;
}

static int gather_selections(AppState *app, EnchantSelection *out /* sized >= g_enchant_count */) {
    GatherCtx ctx = { out, 0 };
    for_each_level_button(app, gather_cb, &ctx);
    return ctx.count;
}

static void clear_all_cb(AppState *app, GtkWidget *btn, int enchant_index, int level, gpointer data) {
    (void)app; (void)enchant_index; (void)level; (void)data;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), FALSE);
    }
}

static void clear_all_selections(AppState *app) {
    app->suppress_toggle_handling = TRUE;
    for_each_level_button(app, clear_all_cb, NULL);
    app->suppress_toggle_handling = FALSE;
    update_calculate_button_state(app);
}

typedef struct { int enchant_index; int level; uint64_t incompatible_mask; gboolean allow_incompatible; } ConflictCtx;

static void conflict_off_cb(AppState *app, GtkWidget *btn, int enchant_index, int level, gpointer data) {
    (void)app;
    ConflictCtx *ctx = data;
    if (enchant_index == ctx->enchant_index && level == ctx->level) return; /* the button just turned on */

    gboolean same_enchant = (enchant_index == ctx->enchant_index);
    gboolean conflicts = !ctx->allow_incompatible && (ctx->incompatible_mask & (1ull << enchant_index));

    if ((same_enchant || conflicts) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), FALSE);
    }
}

static void show_too_many_dialog(AppState *app) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "You've selected more than %d enchantments. Very large selections can take a long time "
        "to compute. Check \"Allow more than %d enchantments\" if you want to proceed anyway.",
        ENCHANTMENT_LIMIT_INCLUSIVE, ENCHANTMENT_LIMIT_INCLUSIVE);
    gtk_alert_dialog_show(dialog, GTK_WINDOW(app->window));
    g_object_unref(dialog);
}

static void on_level_button_toggled(GtkToggleButton *btn, gpointer user_data) {
    AppState *app = user_data;
    if (app->suppress_toggle_handling) return;

    int enchant_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "enchant-index"));
    int level = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "level"));

    if (gtk_toggle_button_get_active(btn)) {
        gboolean allow_many = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_many));
        int total = count_selected(app);
        if (!allow_many && total > ENCHANTMENT_LIMIT_INCLUSIVE) {
            app->suppress_toggle_handling = TRUE;
            gtk_toggle_button_set_active(btn, FALSE);
            app->suppress_toggle_handling = FALSE;
            show_too_many_dialog(app);
            update_calculate_button_state(app);
            return;
        }

        gboolean allow_incompatible = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->check_incompatible));
        ConflictCtx ctx = {
            .enchant_index = enchant_index,
            .level = level,
            .incompatible_mask = g_enchants[enchant_index].incompatible_mask,
            .allow_incompatible = allow_incompatible,
        };
        app->suppress_toggle_handling = TRUE;
        for_each_level_button(app, conflict_off_cb, &ctx);
        app->suppress_toggle_handling = FALSE;
    }

    update_calculate_button_state(app);
}

static void on_incompatible_toggled(GtkCheckButton *btn, gpointer user_data) {
    AppState *app = user_data;
    if (!gtk_check_button_get_active(btn)) clear_all_selections(app);
}

static void on_many_toggled(GtkCheckButton *btn, gpointer user_data) {
    AppState *app = user_data;
    if (!gtk_check_button_get_active(btn)) clear_all_selections(app);
}

static void update_calculate_button_state(AppState *app) {
    int n = count_selected(app);
    gtk_widget_set_sensitive(app->calculate_button, n > 0);
}

/* ---------- enchant grid construction ---------- */

static void add_row_css(GtkWidget *widget, int group_parity) {
    gtk_widget_add_css_class(widget, group_parity ? "eo-row-a" : "eo-row-b");
}

static void rebuild_enchant_grid(AppState *app, int item_index, gboolean is_book) {
    app->current_item_index = item_index;
    app->current_is_book = is_book;
    app->has_item_selected = TRUE;

    if (app->enchant_grid) {
        gtk_box_remove(GTK_BOX(app->enchant_scroller), app->enchant_grid);
    }

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    app->enchant_grid = grid;
    gtk_box_append(GTK_BOX(app->enchant_scroller), grid);

    int applicable[64];
    int applicable_count = enchants_for_item(item_index, is_book, applicable);

    int max_level = 0;
    for (int i = 0; i < applicable_count; i++) {
        if (g_enchants[applicable[i]].level_max > max_level) max_level = g_enchants[applicable[i]].level_max;
    }

    uint64_t allowed_mask = 0;
    for (int i = 0; i < applicable_count; i++) allowed_mask |= (1ull << applicable[i]);

    gboolean placed[64] = {0};
    int row = 0;
    int group_parity = 0;

    for (int i = 0; i < applicable_count; i++) {
        int e = applicable[i];
        if (placed[e]) continue;

        uint64_t group = incompatible_group(e, allowed_mask);
        for (int j = 0; j < applicable_count; j++) {
            int e2 = applicable[j];
            if (!(group & (1ull << e2)) || placed[e2]) continue;
            placed[e2] = TRUE;

            GtkWidget *name_label = gtk_label_new(g_enchants[e2].display);
            gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
            gtk_widget_set_margin_start(name_label, 4);
            gtk_widget_set_margin_end(name_label, 8);
            add_row_css(name_label, group_parity);
            gtk_grid_attach(GTK_GRID(grid), name_label, 0, row, 1, 1);

            for (int level = 1; level <= g_enchants[e2].level_max; level++) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", level);
                GtkWidget *button = gtk_toggle_button_new_with_label(buf);
                gtk_widget_set_size_request(button, 32, -1);
                g_object_set_data(G_OBJECT(button), "enchant-index", GINT_TO_POINTER(e2));
                g_object_set_data(G_OBJECT(button), "level", GINT_TO_POINTER(level));
                add_row_css(button, group_parity);
                g_signal_connect(button, "toggled", G_CALLBACK(on_level_button_toggled), app);
                gtk_grid_attach(GTK_GRID(grid), button, level, row, 1, 1);
            }
            row++;
        }
        group_parity = !group_parity;
    }

    update_calculate_button_state(app);
    clear_results(app);
}

/* ---------- item dropdown ---------- */

static void dropdown_factory_setup(GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data) {
    (void)factory; (void)user_data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *image = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
    gtk_widget_set_size_request(image, 20, 20);
    GtkWidget *label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(list_item, box);
}

static void dropdown_factory_bind(GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    AppState *app = user_data;
    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *image = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(image);

    guint pos = gtk_list_item_get_position(list_item);
    GtkStringObject *str_obj = GTK_STRING_OBJECT(gtk_list_item_get_item(list_item));
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(str_obj));

    int item_index = app->dropdown_item_index[pos];
    GdkTexture *icon = get_item_icon(item_index);
    if (icon) {
        gtk_image_set_from_paintable(GTK_IMAGE(image), GDK_PAINTABLE(icon));
        gtk_widget_set_visible(image, TRUE);
    } else {
        gtk_widget_set_visible(image, FALSE);
    }
}

static void on_item_selected(GObject *dropdown_obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *app = user_data;
    GtkDropDown *dropdown = GTK_DROP_DOWN(dropdown_obj);
    guint pos = gtk_drop_down_get_selected(dropdown);

    if (pos == GTK_INVALID_LIST_POSITION || app->dropdown_item_index[pos] < 0) {
        app->has_item_selected = FALSE;
        gtk_widget_set_visible(app->enchant_placeholder, TRUE);
        gtk_widget_set_visible(app->enchant_scroller, FALSE);
        if (app->enchant_grid) {
            gtk_box_remove(GTK_BOX(app->enchant_scroller), app->enchant_grid);
            app->enchant_grid = NULL;
        }
        update_calculate_button_state(app);
        clear_results(app);
        return;
    }

    int item_index = app->dropdown_item_index[pos];
    gboolean is_book = (strcmp(g_items[item_index].id, "book") == 0);

    gtk_widget_set_visible(app->enchant_placeholder, FALSE);
    gtk_widget_set_visible(app->enchant_scroller, TRUE);
    rebuild_enchant_grid(app, item_index, is_book);
}

/* ---------- results display ---------- */

static void clear_results(AppState *app) {
    gtk_widget_set_visible(app->results_box, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "");
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(app->results_list)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(app->results_list), child);
    }
}

static GtkWidget *build_operand_widget(AppState *app, const EnchantSelection *selections, const ItemState *node) {
    int icon_item_index;
    const char *name;
    if (node->is_target_item) {
        icon_item_index = app->current_is_book ? book_item_index() : app->current_item_index;
        name = app->current_is_book ? "Book" : g_items[app->current_item_index].display;
    } else {
        icon_item_index = book_item_index();
        name = "Book";
    }

    GString *text = g_string_new(name);
    gboolean first = TRUE;
    for (int i = 0; i < 64; i++) {
        if (!(node->enchant_mask & (1ull << i))) continue;
        int ei = selections[i].enchant_index;
        int level = selections[i].level;
        g_string_append(text, first ? " (" : ", ");
        g_string_append(text, g_enchants[ei].display);
        if (g_enchants[ei].level_max > 1) g_string_append_printf(text, " %d", level);
        first = FALSE;
    }
    if (!first) g_string_append(text, ")");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GdkTexture *icon = get_item_icon(icon_item_index);
    if (icon) {
        GtkWidget *image = gtk_image_new_from_paintable(GDK_PAINTABLE(icon));
        gtk_image_set_pixel_size(GTK_IMAGE(image), 20);
        gtk_box_append(GTK_BOX(box), image);
    }
    GtkWidget *label = gtk_label_new(text->str);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);
    g_string_free(text, TRUE);
    return box;
}

static void populate_results(AppState *app, CalcJob *job) {
    OptimizeResult *result = job->result;

    char timing[128];
    if (job->elapsed_us < 1000) {
        snprintf(timing, sizeof(timing), "Completed in %ld\xc2\xb5s", (long)job->elapsed_us);
    } else if (job->elapsed_us < 1000000) {
        snprintf(timing, sizeof(timing), "Completed in %ldms", (long)(job->elapsed_us / 1000));
    } else {
        snprintf(timing, sizeof(timing), "Completed in %.1fs", job->elapsed_us / 1000000.0);
    }
    gtk_label_set_text(GTK_LABEL(app->results_timing), timing);

    if (!result->found) {
        gtk_label_set_text(GTK_LABEL(app->results_header), "No solution found within the anvil's level-39 cap.");
        gtk_widget_set_visible(app->results_box, TRUE);
        return;
    }

    char header[256];
    snprintf(header, sizeof(header), "Optimal order: %ld levels total, %ld XP",
              result->total_levels, result->total_xp);
    gtk_label_set_text(GTK_LABEL(app->results_header), header);

    for (int i = 0; i < result->step_count; i++) {
        ItemState *step = result->steps[i];
        long xp = experience_for_level(step->merge_cost);
        long prior_work_after = (1L << step->work) - 1;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_margin_top(row, 6);
        gtk_widget_set_margin_bottom(row, 6);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);

        GtkWidget *combine_line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        char step_num[16];
        snprintf(step_num, sizeof(step_num), "%d.", i + 1);
        GtkWidget *num_label = gtk_label_new(step_num);
        gtk_widget_add_css_class(num_label, "eo-section");
        gtk_box_append(GTK_BOX(combine_line), num_label);
        gtk_box_append(GTK_BOX(combine_line), gtk_label_new("Combine"));
        gtk_box_append(GTK_BOX(combine_line), build_operand_widget(app, job->selections, step->left));
        gtk_box_append(GTK_BOX(combine_line), gtk_label_new("with"));
        gtk_box_append(GTK_BOX(combine_line), build_operand_widget(app, job->selections, step->right));
        gtk_box_append(GTK_BOX(row), combine_line);

        char detail[160];
        snprintf(detail, sizeof(detail),
                 "Cost: %ld levels (%ld xp)   \xc2\xb7   Resulting prior-work penalty: %ld levels",
                 step->merge_cost, xp, prior_work_after);
        GtkWidget *detail_label = gtk_label_new(detail);
        gtk_widget_add_css_class(detail_label, "dim-label");
        gtk_label_set_xalign(GTK_LABEL(detail_label), 0.0);
        gtk_box_append(GTK_BOX(row), detail_label);

        gtk_list_box_append(GTK_LIST_BOX(app->results_list), row);
    }

    gtk_widget_set_visible(app->results_box, TRUE);
}

/* ---------- background computation ---------- */

static gboolean on_calc_done(gpointer data);

static gpointer calc_thread_func(gpointer data) {
    CalcJob *job = data;
    job->result = optimize(job->item_index, job->is_book, job->selections, job->n, job->mode);
    job->elapsed_us = g_get_monotonic_time() - job->start_time_us;
    /* g_idle_add is thread-safe: this marshals the result back onto the
     * main loop, since GTK widgets may only be touched from the main thread. */
    g_idle_add_full(G_PRIORITY_DEFAULT, (GSourceFunc)on_calc_done, job, NULL);
    return NULL;
}

static gboolean on_calc_done(gpointer data) {
    CalcJob *job = data;
    AppState *app = job->app;

    gtk_widget_set_sensitive(app->calculate_button, TRUE);
    gtk_spinner_stop(GTK_SPINNER(app->spinner));
    gtk_widget_set_visible(app->spinner, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "");

    populate_results(app, job);

    optimize_result_free(job->result);
    g_free(job->selections);
    g_free(job);
    update_calculate_button_state(app);
    return G_SOURCE_REMOVE;
}

static void on_calculate_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *app = user_data;

    EnchantSelection selections[64];
    int n = gather_selections(app, selections);
    if (n == 0) return;

    if (n >= 6) {
        gtk_label_set_text(GTK_LABEL(app->status_label),
            "Large selections may take a while to compute\xe2\x80\xa6");
    }

    CalcJob *job = g_new0(CalcJob, 1);
    job->app = app;
    job->selections = g_memdup2(selections, sizeof(EnchantSelection) * (size_t)n);
    job->n = n;
    job->item_index = app->current_item_index;
    job->is_book = app->current_is_book;
    job->mode = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->mode_xp))
                    ? OPTIMIZE_MODE_XP : OPTIMIZE_MODE_PRIOR_WORK;
    job->start_time_us = g_get_monotonic_time();

    gtk_widget_set_sensitive(app->calculate_button, FALSE);
    gtk_widget_set_visible(app->spinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(app->spinner));
    clear_results(app);

    GThread *thread = g_thread_new("optimize", calc_thread_func, job);
    g_thread_unref(thread);
}

/* ---------- window assembly ---------- */

static const char *CSS =
    ".eo-row-a { background-color: alpha(currentColor, 0.04); }"
    ".eo-row-b { background-color: transparent; }"
    ".eo-title { font-size: 1.4em; font-weight: bold; }"
    ".eo-section { font-weight: bold; margin-top: 6px; }";

void ui_build_window(GtkApplication *app_gtk) {
    AppState *app = g_new0(AppState, 1);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, CSS, -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    app->window = gtk_application_window_new(app_gtk);
    gtk_window_set_title(GTK_WINDOW(app->window), "CHEAP \xe2\x80\x94 Minecraft Enchantment Ordering Tool");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 720, 760);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(app->window), scroller);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), root);

    GtkWidget *title = gtk_label_new("CHEAP \xe2\x80\x94 Minecraft Enchantment Ordering Tool");
    gtk_widget_add_css_class(title, "eo-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(root), title);

    GtkWidget *subtitle = gtk_label_new(
        "Cheapest Hierarchical Enchant-Anvil Planner \xe2\x80\x94 finds the cheapest anvil order to "
        "combine enchanted books onto an item, minimizing total XP or the final prior-work penalty.");
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
    gtk_box_append(GTK_BOX(root), subtitle);

    /* item picker */
    GtkWidget *item_section = gtk_label_new("Item");
    gtk_widget_add_css_class(item_section, "eo-section");
    gtk_label_set_xalign(GTK_LABEL(item_section), 0.0);
    gtk_box_append(GTK_BOX(root), item_section);

    GtkStringList *string_list = gtk_string_list_new(NULL);
    gtk_string_list_append(string_list, "Choose an item to enchant\xe2\x80\xa6");
    app->dropdown_item_index = g_new(int, g_item_count + 1);
    app->dropdown_item_index[0] = -1;
    for (int i = 0; i < g_item_count; i++) {
        gtk_string_list_append(string_list, g_items[i].display);
        app->dropdown_item_index[i + 1] = i;
    }
    app->item_dropdown = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(string_list), NULL));

    GtkListItemFactory *dropdown_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(dropdown_factory, "setup", G_CALLBACK(dropdown_factory_setup), app);
    g_signal_connect(dropdown_factory, "bind", G_CALLBACK(dropdown_factory_bind), app);
    gtk_drop_down_set_factory(app->item_dropdown, dropdown_factory);
    g_object_unref(dropdown_factory);

    gtk_drop_down_set_selected(app->item_dropdown, 0);
    g_signal_connect(app->item_dropdown, "notify::selected", G_CALLBACK(on_item_selected), app);
    gtk_box_append(GTK_BOX(root), GTK_WIDGET(app->item_dropdown));

    /* enchant grid */
    GtkWidget *enchant_section = gtk_label_new("Enchantments");
    gtk_widget_add_css_class(enchant_section, "eo-section");
    gtk_label_set_xalign(GTK_LABEL(enchant_section), 0.0);
    gtk_box_append(GTK_BOX(root), enchant_section);

    app->enchant_placeholder = gtk_label_new("Choose an item above to see available enchantments.");
    gtk_label_set_xalign(GTK_LABEL(app->enchant_placeholder), 0.0);
    gtk_widget_add_css_class(app->enchant_placeholder, "dim-label");
    gtk_box_append(GTK_BOX(root), app->enchant_placeholder);

    app->enchant_scroller = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_visible(app->enchant_scroller, FALSE);
    gtk_box_append(GTK_BOX(root), app->enchant_scroller);
    app->enchant_grid = NULL;

    /* options */
    GtkWidget *options_section = gtk_label_new("Options");
    gtk_widget_add_css_class(options_section, "eo-section");
    gtk_label_set_xalign(GTK_LABEL(options_section), 0.0);
    gtk_box_append(GTK_BOX(root), options_section);

    app->check_incompatible = gtk_check_button_new_with_label("Allow incompatible enchantments");
    gtk_box_append(GTK_BOX(root), app->check_incompatible);
    g_signal_connect(app->check_incompatible, "toggled", G_CALLBACK(on_incompatible_toggled), app);

    app->check_many = gtk_check_button_new_with_label("Allow more than 10 enchantments (slow)");
    gtk_box_append(GTK_BOX(root), app->check_many);
    g_signal_connect(app->check_many, "toggled", G_CALLBACK(on_many_toggled), app);

    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->mode_xp = gtk_check_button_new_with_label("Optimize for XP");
    app->mode_prior_work = gtk_check_button_new_with_label("Optimize for prior-work penalty");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(app->mode_prior_work), GTK_CHECK_BUTTON(app->mode_xp));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->mode_xp), TRUE);
    gtk_box_append(GTK_BOX(mode_box), app->mode_xp);
    gtk_box_append(GTK_BOX(mode_box), app->mode_prior_work);
    gtk_box_append(GTK_BOX(root), mode_box);

    /* calculate */
    GtkWidget *calc_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(calc_box, 8);
    app->calculate_button = gtk_button_new_with_label("Calculate");
    gtk_widget_add_css_class(app->calculate_button, "suggested-action");
    gtk_widget_set_sensitive(app->calculate_button, FALSE);
    g_signal_connect(app->calculate_button, "clicked", G_CALLBACK(on_calculate_clicked), app);
    gtk_box_append(GTK_BOX(calc_box), app->calculate_button);

    app->spinner = gtk_spinner_new();
    gtk_widget_set_visible(app->spinner, FALSE);
    gtk_box_append(GTK_BOX(calc_box), app->spinner);

    app->status_label = gtk_label_new("");
    gtk_box_append(GTK_BOX(calc_box), app->status_label);
    gtk_box_append(GTK_BOX(root), calc_box);

    /* results */
    app->results_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_visible(app->results_box, FALSE);
    gtk_widget_set_margin_top(app->results_box, 12);
    gtk_box_append(GTK_BOX(root), app->results_box);

    app->results_header = gtk_label_new("");
    gtk_widget_add_css_class(app->results_header, "eo-section");
    gtk_label_set_xalign(GTK_LABEL(app->results_header), 0.0);
    gtk_label_set_wrap(GTK_LABEL(app->results_header), TRUE);
    gtk_box_append(GTK_BOX(app->results_box), app->results_header);

    app->results_timing = gtk_label_new("");
    gtk_widget_add_css_class(app->results_timing, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(app->results_timing), 0.0);
    gtk_box_append(GTK_BOX(app->results_box), app->results_timing);

    app->results_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->results_list), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(app->results_box), app->results_list);

    gtk_window_present(GTK_WINDOW(app->window));
}
