#include "fsearch_preferences_dialog.h"

#include "fsearch_config.h"
#include "fsearch_database_preferences_widget.h"
#include "fsearch_filter_manager.h"
#include "fsearch_filter_preferences_widget.h"

#include <gdk/gdkevents.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdio.h>

struct _FsearchPreferencesDialog {
    GtkDialog parent_instance;

    FsearchConfig *config;
    FsearchConfig *config_old;

    // Interface page
    GtkWidget *help_stack;
    GtkWidget *help_description;
    GtkWidget *help_expander;

    FsearchFilterPreferencesWidget *filter_pref_widget;
    FsearchDatabasePreferencesWidget *database_pref_widget;

    guint help_reset_timeout_id;

    GtkNotebook *main_notebook;

    // Interface page
    GtkToggleButton *enable_dark_theme_button;
    GtkToggleButton *show_menubar_button;
    GtkToggleButton *show_tooltips_button;
    GtkToggleButton *restore_win_size_button;
    GtkToggleButton *exit_on_escape_button;
    GtkToggleButton *restore_sort_order_button;
    GtkToggleButton *restore_column_config_button;
    GtkToggleButton *double_click_path_button;
    GtkToggleButton *single_click_open_button;
    GtkToggleButton *launch_desktop_files_button;
    GtkToggleButton *show_icons_button;
    GtkToggleButton *highlight_search_terms;
    GtkToggleButton *show_base_2_units;
    GtkComboBox *action_after_file_open;
    GtkToggleButton *action_after_file_open_keyboard;
    GtkToggleButton *action_after_file_open_mouse;
    GtkToggleButton *show_indexing_status_button;
    GtkToggleButton *close_to_tray_button;
    GtkToggleButton *autostart_button;
    GtkToggleButton *context_menu_button;
    GtkEntry *global_hotkey_entry;

    // Search page
    GtkToggleButton *auto_search_in_path_button;
    GtkToggleButton *auto_match_case_button;
    GtkToggleButton *search_as_you_type_button;
    GtkToggleButton *hide_results_button;

    GtkFrame *filter_frame;

    // Dialog page
    GtkToggleButton *show_dialog_failed_opening;
};

enum { PROP_0, PROP_CONFIG, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

G_DEFINE_TYPE(FsearchPreferencesDialog, fsearch_preferences_dialog, GTK_TYPE_DIALOG)

static gboolean
help_reset(gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    if (self->help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(self->help_stack), GTK_WIDGET(self->help_description));
    }
    g_source_remove(self->help_reset_timeout_id);
    self->help_reset_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean
on_help_reset(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(user_data);
    if (self->help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(self->help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }
    self->help_reset_timeout_id = g_timeout_add(200, help_reset, self);
    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_help_show(GtkWidget *widget, int x, int y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(gtk_widget_get_toplevel(widget));
    g_return_val_if_fail(self, GDK_EVENT_PROPAGATE);

    if (self->help_expander && !gtk_expander_get_expanded(GTK_EXPANDER(self->help_expander))) {
        return GDK_EVENT_PROPAGATE;
    }

    if (self->help_reset_timeout_id != 0) {
        g_source_remove(self->help_reset_timeout_id);
        self->help_reset_timeout_id = 0;
    }
    if (self->help_stack != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(self->help_stack), GTK_WIDGET(user_data));
    }
    return GDK_EVENT_PROPAGATE;
}

void
fsearch_preferences_dialog_bind_help(FsearchPreferencesDialog *self, GtkWidget *control, const char *help_page_name) {
    g_return_if_fail(FSEARCH_IS_PREFERENCES_DIALOG(self));
    g_return_if_fail(GTK_IS_WIDGET(control));

    GtkWidget *page = gtk_stack_get_child_by_name(GTK_STACK(self->help_stack), help_page_name);
    g_return_if_fail(page);

    // drop any static tooltip so only the dynamic help panel responds
    gtk_widget_set_tooltip_text(control, NULL);
    gtk_widget_set_tooltip_markup(control, NULL);
    gtk_widget_set_has_tooltip(control, TRUE);

    g_signal_connect(control, "query-tooltip", G_CALLBACK(on_help_show), page);
    g_signal_connect(control, "leave-notify-event", G_CALLBACK(on_help_reset), self);
    g_signal_connect(control, "focus-out-event", G_CALLBACK(on_help_reset), self);
}

static void
update_config(FsearchPreferencesDialog *self) {
    self->config->enable_dark_theme = gtk_toggle_button_get_active(self->enable_dark_theme_button);
    self->config->show_menubar = !gtk_toggle_button_get_active(self->show_menubar_button);
    self->config->enable_list_tooltips = gtk_toggle_button_get_active(self->show_tooltips_button);
    self->config->restore_window_size = gtk_toggle_button_get_active(self->restore_win_size_button);
    self->config->restore_column_config = gtk_toggle_button_get_active(self->restore_column_config_button);
    self->config->restore_sort_order = gtk_toggle_button_get_active(self->restore_sort_order_button);
    self->config->exit_on_escape = gtk_toggle_button_get_active(self->exit_on_escape_button);
    self->config->double_click_path = gtk_toggle_button_get_active(self->double_click_path_button);
    self->config->single_click_open = gtk_toggle_button_get_active(self->single_click_open_button);
    self->config->launch_desktop_files = gtk_toggle_button_get_active(self->launch_desktop_files_button);
    self->config->show_listview_icons = gtk_toggle_button_get_active(self->show_icons_button);
    self->config->highlight_search_terms = gtk_toggle_button_get_active(self->highlight_search_terms);
    self->config->show_base_2_units = gtk_toggle_button_get_active(self->show_base_2_units);
    self->config->action_after_file_open_keyboard = gtk_toggle_button_get_active(self->action_after_file_open_keyboard);
    self->config->action_after_file_open_mouse = gtk_toggle_button_get_active(self->action_after_file_open_mouse);
    self->config->show_indexing_status = gtk_toggle_button_get_active(self->show_indexing_status_button);
    self->config->close_to_tray = gtk_toggle_button_get_active(self->close_to_tray_button);
    self->config->autostart = gtk_toggle_button_get_active(self->autostart_button);
    self->config->context_menu = gtk_toggle_button_get_active(self->context_menu_button);

    const char *hk = gtk_entry_get_text(self->global_hotkey_entry);
    g_clear_pointer(&self->config->global_hotkey, g_free);
    self->config->global_hotkey = g_strdup(hk ? hk : "");

    self->config->auto_search_in_path = gtk_toggle_button_get_active(self->auto_search_in_path_button);
    self->config->auto_match_case = gtk_toggle_button_get_active(self->auto_match_case_button);
    self->config->search_as_you_type = gtk_toggle_button_get_active(self->search_as_you_type_button);
    self->config->hide_results_on_empty_search = gtk_toggle_button_get_active(self->hide_results_button);
    self->config->show_dialog_failed_opening = gtk_toggle_button_get_active(self->show_dialog_failed_opening);

    self->config->action_after_file_open = gtk_combo_box_get_active(self->action_after_file_open);

    g_clear_pointer(&self->config->filters, fsearch_filter_manager_unref);
    self->config->filters = fsearch_filter_preferences_widget_get_filter_manager(self->filter_pref_widget);

    g_clear_object(&self->config->includes);
    self->config->includes = fsearch_database_preferences_widget_get_include_manager(self->database_pref_widget);

    g_clear_object(&self->config->excludes);
    self->config->excludes = fsearch_database_preferences_widget_get_exclude_manager(self->database_pref_widget);
}

static void
on_action_after_file_open_changed(GtkComboBox *widget, gpointer user_data) {
    int active = gtk_combo_box_get_active(widget);
    if (active != ACTION_AFTER_OPEN_NOTHING) {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), TRUE);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(user_data), FALSE);
    }
}

static void
fsearch_preferences_dialog_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_pointer(value, self->config);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_preferences_dialog_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    switch (prop_id) {
    case PROP_CONFIG:
        self->config = config_copy(g_value_get_pointer(value));
        self->config_old = config_copy(g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_preferences_dialog_dispose(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    g_clear_pointer(&self->config, config_free);
    g_clear_pointer(&self->config_old, config_free);

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->dispose(object);
}

static void
fsearch_preferences_dialog_finalize(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    if (self->help_reset_timeout_id) {
        g_source_remove(self->help_reset_timeout_id);
        self->help_reset_timeout_id = 0;
    }

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->finalize(object);
}

// 捕获组合键并转成 GTK 加速键串（如 <Super>f / <Alt>f / <Control>f）写入输入框。
// 关键：必须自己捕获 key-press-event 并阻止默认传播；否则 GtkEntry 只接收字符（如只显示“f”），
// 修饰键（Win/Alt/Ctrl）会被丢弃，无法自定义全局快捷键。
static gboolean
on_global_hotkey_entry_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)user_data;
    guint keyval = event->keyval;
    GdkModifierType state = event->state;

    // 只按下纯修饰键/锁键时先忽略，等用户按下真正的功能键再记录
    switch (keyval) {
        case GDK_KEY_Shift_L: case GDK_KEY_Shift_R:
        case GDK_KEY_Control_L: case GDK_KEY_Control_R:
        case GDK_KEY_Alt_L: case GDK_KEY_Alt_R:
        case GDK_KEY_Super_L: case GDK_KEY_Super_R:
        case GDK_KEY_Hyper_L: case GDK_KEY_Hyper_R:
        case GDK_KEY_Meta_L: case GDK_KEY_Meta_R:
        case GDK_KEY_Caps_Lock: case GDK_KEY_Num_Lock: case GDK_KEY_Scroll_Lock:
            return TRUE;
    }

    // 剥离锁键（NumLock/CapsLock/ScrollLock），保留真实修饰键（含 Super/Alt/Ctrl）
    state &= ~(GDK_LOCK_MASK | GDK_MOD2_MASK | GDK_MOD5_MASK);

    // Windows/Super 键在部分 X11 配置下会同时置位 Mod4 与 Super，
    // 折叠成单一的 Super，否则 gtk_accelerator_name 会输出 “<Mod4><Super>” 这种无法被解析的串。
    if (state & GDK_SUPER_MASK) {
        state &= ~GDK_MOD4_MASK;
    }
    else if (state & GDK_MOD4_MASK) {
        state |= GDK_SUPER_MASK;
        state &= ~GDK_MOD4_MASK;
    }

    // 安全护栏：不允许把“单个字母/数字（无修饰键）”设为全局热键，
    // 否则会劫持该键的全局键盘输入（例如单按 f 就弹出 FSearch，且 f 无法输入到其它程序）。
    // 带修饰键的组合（如 Win+F）或功能键（F1~F12）不受此限制。
    if (state == 0) {
        if ((keyval >= GDK_KEY_a && keyval <= GDK_KEY_z) ||
            (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z) ||
            (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)) {
            gtk_entry_set_text(GTK_ENTRY(widget), "");
            return TRUE;
        }
    }

    char *accel = gtk_accelerator_name(keyval, state);
    gtk_entry_set_text(GTK_ENTRY(widget), accel ? accel : "");
    g_free(accel);

    // 阻止按键继续传播：避免字符被写入、或 Alt+F 触发菜单等
    return TRUE;
}

static void
fsearch_preferences_dialog_constructed(GObject *object) {
    FsearchPreferencesDialog *self = FSEARCH_PREFERENCES_DIALOG(object);

    G_OBJECT_CLASS(fsearch_preferences_dialog_parent_class)->constructed(object);

    self->filter_pref_widget = fsearch_filter_preferences_widget_new(self->config_old->filters);
    gtk_container_add(GTK_CONTAINER(self->filter_frame), GTK_WIDGET(self->filter_pref_widget));
    gtk_widget_show(GTK_WIDGET(self->filter_pref_widget));

    self->database_pref_widget = fsearch_database_preferences_widget_new(self->config_old->includes,
                                                                          self->config_old->excludes);
    gtk_notebook_append_page(self->main_notebook, GTK_WIDGET(self->database_pref_widget), gtk_label_new(_("Database")));
    gtk_widget_show(GTK_WIDGET(self->database_pref_widget));
    fsearch_database_preferences_widget_setup_help(self->database_pref_widget, self);

    gtk_toggle_button_set_active(self->enable_dark_theme_button, self->config_old->enable_dark_theme);
    gtk_toggle_button_set_active(self->show_menubar_button, !self->config_old->show_menubar);
    gtk_toggle_button_set_active(self->show_tooltips_button, self->config_old->enable_list_tooltips);
    gtk_toggle_button_set_active(self->restore_win_size_button, self->config_old->restore_window_size);
    gtk_toggle_button_set_active(self->restore_column_config_button, self->config_old->restore_column_config);
    gtk_toggle_button_set_active(self->restore_sort_order_button, self->config_old->restore_sort_order);
    gtk_toggle_button_set_active(self->exit_on_escape_button, self->config_old->exit_on_escape);
    gtk_toggle_button_set_active(self->double_click_path_button, self->config_old->double_click_path);
    gtk_toggle_button_set_active(self->single_click_open_button, self->config_old->single_click_open);
    gtk_toggle_button_set_active(self->launch_desktop_files_button, self->config_old->launch_desktop_files);
    gtk_toggle_button_set_active(self->show_icons_button, self->config_old->show_listview_icons);
    gtk_toggle_button_set_active(self->highlight_search_terms, self->config_old->highlight_search_terms);
    gtk_toggle_button_set_active(self->show_base_2_units, self->config_old->show_base_2_units);
    gtk_toggle_button_set_active(self->action_after_file_open_keyboard,
                                 self->config_old->action_after_file_open_keyboard);
    gtk_toggle_button_set_active(self->action_after_file_open_mouse, self->config_old->action_after_file_open_mouse);
    gtk_toggle_button_set_active(self->show_indexing_status_button, self->config_old->show_indexing_status);
    gtk_toggle_button_set_active(self->close_to_tray_button, self->config_old->close_to_tray);
    gtk_toggle_button_set_active(self->autostart_button, self->config_old->autostart);
    gtk_toggle_button_set_active(self->context_menu_button, self->config_old->context_menu);
    gtk_entry_set_text(self->global_hotkey_entry,
                       self->config_old->global_hotkey ? self->config_old->global_hotkey : "");
    // 让输入框能捕获组合键（如 Win+F、Alt+F），而非只接收字符“f”
    g_signal_connect(self->global_hotkey_entry, "key-press-event",
                     G_CALLBACK(on_global_hotkey_entry_key_pressed), self);
    gtk_toggle_button_set_active(self->auto_search_in_path_button, self->config_old->auto_search_in_path);
    gtk_toggle_button_set_active(self->auto_match_case_button, self->config_old->auto_match_case);
    gtk_toggle_button_set_active(self->search_as_you_type_button, self->config_old->search_as_you_type);
    gtk_toggle_button_set_active(self->hide_results_button, self->config_old->hide_results_on_empty_search);
    gtk_toggle_button_set_active(self->show_dialog_failed_opening, self->config_old->show_dialog_failed_opening);

    gtk_combo_box_set_active(self->action_after_file_open, self->config_old->action_after_file_open);
}

static void
fsearch_preferences_dialog_class_init(FsearchPreferencesDialogClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = fsearch_preferences_dialog_finalize;
    object_class->dispose = fsearch_preferences_dialog_dispose;
    object_class->constructed = fsearch_preferences_dialog_constructed;
    object_class->set_property = fsearch_preferences_dialog_set_property;
    object_class->get_property = fsearch_preferences_dialog_get_property;

    properties[PROP_CONFIG] = g_param_spec_pointer("config",
                                                   "Configuration",
                                                   "The configuration which will be used to fill the dialog with and "
                                                   "which will be modified by the dialog"
                                                   "default",
                                                   (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                    G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(widget_class,
                                                "/io/github/cboxdoerfer/fsearch/ui/fsearch_preferences.ui");

    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_stack);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_description);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, help_expander);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, main_notebook);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, enable_dark_theme_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_menubar_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_tooltips_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_win_size_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, exit_on_escape_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_sort_order_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, restore_column_config_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, double_click_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, single_click_open_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, launch_desktop_files_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_icons_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, highlight_search_terms);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_base_2_units);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open_keyboard);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, action_after_file_open_mouse);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_indexing_status_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, close_to_tray_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, autostart_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, context_menu_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, global_hotkey_entry);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_search_in_path_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, auto_match_case_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, search_as_you_type_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, hide_results_button);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, filter_frame);
    gtk_widget_class_bind_template_child(widget_class, FsearchPreferencesDialog, show_dialog_failed_opening);

    gtk_widget_class_bind_template_callback(widget_class, on_help_show);
    gtk_widget_class_bind_template_callback(widget_class, on_help_reset);
    gtk_widget_class_bind_template_callback(widget_class, on_action_after_file_open_changed);
}

static void
fsearch_preferences_dialog_init(FsearchPreferencesDialog *self) {
    g_assert(FSEARCH_IS_PREFERENCES_DIALOG(self));

    gtk_widget_init_template(GTK_WIDGET(self));

    gtk_dialog_add_button(GTK_DIALOG(self), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(self), _("_OK"), GTK_RESPONSE_OK);
}

FsearchPreferencesDialog *
fsearch_preferences_dialog_new(GtkWindow *parent, FsearchConfig *config) {
    FsearchPreferencesDialog *self = g_object_new(FSEARCH_PREFERENCES_DIALOG_TYPE, "config", config, NULL);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    }
    return self;
}

FsearchConfig *
fsearch_preferences_dialog_get_config(FsearchPreferencesDialog *self) {
    g_return_val_if_fail(self, NULL);
    update_config(self);
    return config_copy(self->config);
}

void
fsearch_preferences_dialog_set_page(FsearchPreferencesDialog *self, FsearchPreferencesDialogPage page) {
    g_return_if_fail(self);

    switch (page) {
    case FSEARCH_PREFERENCES_DIALOG_PAGE_GENERAL:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 0);
        break;
    case FSEARCH_PREFERENCES_DIALOG_PAGE_SEARCH:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 1);
        break;
    case FSEARCH_PREFERENCES_DIALOG_PAGE_DATABASE:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 2);
        break;
    default:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(self->main_notebook), 0);
    }
}