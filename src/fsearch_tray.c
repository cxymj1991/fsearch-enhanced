/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "fsearch_tray.h"

#include "fsearch.h"
#include "fsearch_window.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <limits.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// 为 TRUE 时，窗口关闭按钮会真正退出程序，而不是最小化到托盘。
static gboolean force_quit = FALSE;

// 用 GTK 自带的状态图标（GtkStatusIcon）：
//   - 左键单击  -> "activate" 信号 -> 直接唤出/聚焦窗口
//   - 右键单击  -> "popup-menu" 信号 -> 弹出含"显示/退出"的菜单
// 对统信 UOS 20 / deepin 的托盘兼容良好，且无需额外的 libappindicator 依赖。
// （GtkStatusIcon 在 GTK3 中虽被标记 deprecated，但在 3.24 仍可用，UOS20 自带即此版本。）
static GtkStatusIcon *status_icon = NULL;

// 根据当前可执行文件位置推导安装前缀下的图标目录
// （二进制位于 <prefix>/libexec/fsearch-bin，前缀即向上两级）。
static char *
get_icon_dir(void) {
    char buf[PATH_MAX] = "";
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return g_strdup("/opt/fsearch/share/icons");
    }
    buf[n] = '\0';

    char *libexec_dir = g_path_get_dirname(buf); // .../libexec
    char *prefix = g_path_get_dirname(libexec_dir); // .../<prefix>
    char *icon_dir = g_build_filename(prefix, "share", "icons", NULL);

    g_free(libexec_dir);
    g_free(prefix);
    return icon_dir;
}

static void
on_tray_show(GtkMenuItem *item, gpointer user_data) {
    fsearch_tray_show_window();
}

static void
on_tray_quit(GtkMenuItem *item, gpointer user_data) {
    force_quit = TRUE;
    GtkApplication *app = GTK_APPLICATION(FSEARCH_APPLICATION_DEFAULT);
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
        if (FSEARCH_IS_APPLICATION_WINDOW(l->data)) {
            gtk_window_close(GTK_WINDOW(l->data));
            return;
        }
    }
    g_application_quit(G_APPLICATION(app));
}

// 左键单击托盘图标：直接唤出（或聚焦）窗口
static void
on_tray_activate(GtkStatusIcon *icon, gpointer user_data) {
    fsearch_tray_show_window();
}

// 右键托盘图标：弹出菜单（含"显示"与"退出"）
static void
on_tray_popup_menu(GtkStatusIcon *icon, guint button, guint activate_time, gpointer user_data) {
    GtkWidget *menu = gtk_menu_new();

    GtkWidget *show_item = gtk_menu_item_new_with_label(_("显示 FSearch"));
    GtkWidget *quit_item = gtk_menu_item_new_with_label(_("退出 FSearch"));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    g_signal_connect(show_item, "activate", G_CALLBACK(on_tray_show), NULL);
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_tray_quit), NULL);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

void
fsearch_tray_set_force_quit(gboolean v) {
    force_quit = v;
}

gboolean
fsearch_tray_get_force_quit(void) {
    return force_quit;
}

void
fsearch_tray_show_window(void) {
    GtkApplication *app = GTK_APPLICATION(FSEARCH_APPLICATION_DEFAULT);
    if (!app) {
        return;
    }
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
        if (FSEARCH_IS_APPLICATION_WINDOW(l->data)) {
            GtkWindow *win = GTK_WINDOW(l->data);
            gtk_window_deiconify(win);
            gtk_window_present_with_time(win, gtk_get_current_event_time());
            return;
        }
    }
}

void
fsearch_tray_init(void) {
    // 把自带图标目录加入图标主题搜索路径，托盘才能找到 fsearch 的 svg 图标
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    char *icon_dir = get_icon_dir();
    gtk_icon_theme_append_search_path(theme, icon_dir);
    g_free(icon_dir);

    status_icon = gtk_status_icon_new_from_icon_name("io.github.cboxdoerfer.FSearch");
    gtk_status_icon_set_title(status_icon, "FSearch");
    gtk_status_icon_set_tooltip_text(status_icon, _("FSearch 文件搜索"));
    gtk_status_icon_set_visible(status_icon, TRUE);

    g_signal_connect(status_icon, "activate", G_CALLBACK(on_tray_activate), NULL);
    g_signal_connect(status_icon, "popup-menu", G_CALLBACK(on_tray_popup_menu), NULL);
}

void
fsearch_tray_shutdown(void) {
    if (status_icon) {
        gtk_status_icon_set_visible(status_icon, FALSE);
        g_object_unref(status_icon);
        status_icon = NULL;
    }
}
