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
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "fsearch_drag_drop.h"

#include "fsearch.h"
#include "fsearch_database.h"
#include "fsearch_list_view.h"
#include "fsearch_window.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdarg.h> // drag_log() 的可变参数

// 画拖拽图标用到 cairo / pango。虽然 gtk.h 间接包含了它们（gtk.h → gdk/gdk.h → cairo.h、
// pango/pango.h），但显式包含更稳妥：将来若改用更小的包含集也不会突然编译失败。
#include <cairo.h>
#include <pango/pangocairo.h>

/*
 * 结果列表的"多选后拖拽"支持。
 *
 * 最终实现（经三轮真机排查收敛）：
 *   1. target list 通过 gtk_drag_source_set_target_list() 存到 widget 的 site 上
 *      —— 这一步【不会】连接任何按压/移动事件检测；
 *   2. 发起完全单点化：FsearchListView 的拖拽手势在"按在已选行 + 移动越过阈值"时
 *      调用 fsearch_drag_drop_begin_from_view() → gtk_drag_begin_with_coordinates()；
 *   3. drag-begin / drag-data-get / drag-end 三个 GtkWidget 信号照常挂接。
 *
 * ⚠️ 不要改回 gtk_drag_source_set()：它会在 widget 上连接内置按压检测，
 * 与手势主动发起形成【两条竞争的发起路径】——内置检测先起拖、手势再起一次，
 * 第二次 gtk_drag_begin 在拖拽进行中直接崩溃（真机实测：一拖就闪退）。
 *
 * ⚠️ GTK 3 API 的坑（首次实机编译踩过）：
 *   1. GTK 3 没有 GtkDragSource 类型（GTK 4 才有），也没有 gtk_drag_source_new() /
 *      gtk_widget_add_controller()；widget 级 API 第一参数都是 GtkWidget *。
 *   2. gtk_drag_source_set_targets() / _set_actions() 不在 GTK 3.24 公开声明里，
 *      用了报 implicit declaration；set_target_list() 公开可用。
 *
 * 手势让位规则（在 fsearch_list_view.c 的 bin_drag_gesture_begin 中实现）：
 *   从"已选中的行"按下并拖动 → 拒绝框选，发起文件拖拽；
 *   从"未选中的行/空白处"按下并拖动 → 正常框选。
 * 另在 multi_press 中让"点击已选中行"保留选区，保证多选后能整组拖走。
 *
 * 操作语义（与 Linux 桌面通用约定一致）：
 *   直接拖拽        → 移动（剪切）
 *   按住 Ctrl 拖拽  → 复制
 *   （动作集 MOVE|COPY 在发起时同时提供，最终由放下目标 + 修饰键决定）
 */

// 同时提供两种 target：
//   text/uri-list                 —— 通用标准，dde-file-manager / Nautilus / 桌面均认
//   x-special/gnome-copied-files  —— GNOME 约定的载荷（首行 "copy"，其后每行一个 URI）
static const GtkTargetEntry drag_targets[] = {
    {(gchar *)"text/uri-list", 0, 0},
    {(gchar *)"x-special/gnome-copied-files", 0, 1},
};

// 收集选中项的完整路径（每项一个 char*）
static void
collect_selected_path(FsearchDatabaseEntry *entry, gpointer user_data) {
    GList **paths = user_data;
    g_autoptr(GString) path = db_entry_get_path_full(entry);
    if (path) {
        *paths = g_list_append(*paths, g_strdup(path->str));
    }
}

static GdkPixbuf *
make_drag_icon(guint num_items, gboolean is_copy) {
    const int width = 150;
    const int height = 46;
    const double arc = 9.0;

    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    if (!pixbuf) {
        return NULL;
    }
    gdk_pixbuf_fill(pixbuf, 0x00000000);

    // GTK 3.24 的真实签名是【3 个参数】：
    //   cairo_surface_t *gdk_cairo_surface_create_from_pixbuf(const GdkPixbuf *pixbuf,
    //                                                         int scale,
    //                                                         GdkWindow *for_window);
    // 第 2 参是 int scale（缩放倍数），第 3 参才是 GdkWindow*。
    // scale 传 1 表示按 1:1 使用；不需要窗口合成时 for_window 传 NULL。
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, 1, NULL);
    if (!surface) {
        g_object_unref(pixbuf);
        return NULL;
    }
    cairo_t *cr = cairo_create(surface);

    // 底色：复制用蓝、移动（剪切）用琥珀色，与 GTK 文件管理器的拖放提示一致
    if (is_copy) {
        cairo_set_source_rgba(cr, 0.13, 0.42, 0.78, 0.92);
    }
    else {
        cairo_set_source_rgba(cr, 0.58, 0.35, 0.04, 0.92);
    }

    // 圆角矩形。cairo_arc 的签名是 (cr, 圆心x, 圆心y, 半径, 起始角, 终止角) —— 6 个参数，
    // 弧度制。之前漏写了终止角导致编译不过，这里四个角都给全。
    cairo_new_path(cr);
    cairo_arc(cr, arc, arc, arc, -G_PI / 2, 0);                        // 左上
    cairo_arc(cr, width - arc, arc, arc, -G_PI / 2, 0);               // 右上
    cairo_arc(cr, width - arc, height - arc, arc, 0, G_PI / 2);       // 右下
    cairo_arc(cr, arc, height - arc, arc, G_PI / 2, G_PI);            // 左下
    cairo_close_path(cr);
    cairo_fill(cr);

    g_autofree char *label = num_items == 1 ? g_strdup(_("1 个项目")) : g_strdup_printf(_("%u 个项目"), num_items);

    PangoLayout *layout = gtk_widget_create_pango_layout(NULL, label);
    if (layout) {
        // 不用 g_autoptr(PangoFontDescription)：那需要 GLib/pango 提供
        // PangoFontDescription_autoptr 这个 cleanup 类型，UOS 20 的 GLib 2.58 还没有定义。
        // 这里手动管理。
        PangoFontDescription *font = pango_font_description_from_string("Sans Bold 11");
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);

        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        int tw = 0, th = 0;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, MAX(6.0, (width - tw) / 2.0), (height - th) / 2.0 - 1);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return pixbuf;
}

// 本次拖拽的路径快照（模块内私有，拖拽结束时清空）
static GList *drag_paths = NULL;

// 拖拽链路调试日志：写入 ~/.cache/fsearch/drag-debug.log（追加）。
// 拖拽涉及 手势 → 发起 → drag-begin → drag-data-get → 放下 多个环节，
// 任一环断掉都表现为"拖拽没反应"，远程排查时靠它精确定位断点。
static void
drag_log(const char *fmt, ...) {
    g_autofree char *cachedir = g_build_filename(g_get_user_cache_dir(), "fsearch", NULL);
    g_mkdir_with_parents(cachedir, 0700);
    g_autofree char *logpath = g_build_filename(cachedir, "drag-debug.log", NULL);

    va_list args;
    va_start(args, fmt);
    g_autofree char *msg = g_strdup_vprintf(fmt, args);
    va_end(args);

    GDateTime *now = g_date_time_new_now_local();
    // 注意：UOS 20 的 GLib 2.58 上 g_date_time_format() 遇到 %f 占位符会返回 NULL
    //（真机日志实证，时间戳打成了 "(null)"），所以微秒手动拼。
    g_autofree char *stamp = g_strdup_printf("%s.%06ld",
                                             g_date_time_format(now, "%H:%M:%S") ?: "",
                                             (long)g_date_time_get_microsecond(now));
    g_date_time_unref(now);

    g_autofree char *line = g_strdup_printf("[%s] %s\n", stamp, msg);
    GError *error = NULL;
    if (!g_file_set_contents(logpath, line, -1, &error)) {
        g_debug("[drag] 日志写入失败: %s", error ? error->message : "?");
        g_clear_error(&error);
    }
}

static void
clear_drag_paths(void) {
    if (drag_paths) {
        g_list_free_full(drag_paths, g_free);
        drag_paths = NULL;
    }
}

// GTK 3 没有 GtkDragSource（那是 GTK 4 的类型）。GTK 3 的拖拽源配置函数全部以
// GtkWidget * 为第一参数，直接作用在 widget 上；drag-begin / drag-data-get / drag-end
// 三个信号也是 GtkWidget 的信号。因此下面所有回调的第一个参数都应是 GtkWidget *。
/*
 * 拖拽源的 target 列表存储。
 *
 * 【为什么不用 gtk_drag_source_set】它除了存 target list，还会在 widget 上连接
 * 内置的 press/motion 检测（motion 时自行发起拖拽）。而本项目由 FsearchListView 的
 * 拖拽手势主动发起（fsearch_drag_drop_begin_from_view），两条发起路径会竞争：
 * 内置检测先起拖、手势又再起一次，第二次 gtk_drag_begin 在已有拖拽进行中时崩溃
 *（真机实测：一拖拽就闪退，且日志停在 init —— 崩在内置检测的发起路径上）。
 *
 * 因此改用 gtk_drag_source_set_target_list()：它只在 widget 上建 site 存目标表
 *（gtk_drag_source_set_icon_* 系列 API 依赖这个 site），**不连接任何按压/移动检测**。
 * 发起完全由手势单点控制；动作集在 gtk_drag_begin_with_coordinates 调用时显式传入。
 */
static GtkTargetList *own_target_list = NULL;

static void
drag_source_configure(GtkWidget *widget) {
    if (!own_target_list) {
        own_target_list = gtk_target_list_new(drag_targets, G_N_ELEMENTS(drag_targets));
    }
    // 只建 site 存 target list，不连接内置按压检测
    gtk_drag_source_set_target_list(widget, own_target_list);
}

// GtkWidget::drag-begin 的签名是 (widget, context, user_data) —— 没有坐标参数。
static void
on_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data) {
    FsearchApplicationWindow *win = FSEARCH_APPLICATION_WINDOW(user_data);
    (void)context;
    drag_log("on_drag_begin: drag-begin 信号触发");
    if (!win) {
        drag_log("on_drag_begin: ✗ win 为空");
        return;
    }

    // 每次拖拽前重置快照
    clear_drag_paths();
    fsearch_application_window_selection_for_each(win, collect_selected_path, &drag_paths);
    drag_log("on_drag_begin: 收集到 %u 个选中项", g_list_length(drag_paths));

    // 动作集已在 gtk_drag_begin_with_coordinates() 调用时显式传入 MOVE|COPY，
    // 最终"移动还是复制"由放下时的目标 + 修饰键决定（按住 Ctrl 拖 = 复制，
    // 直接拖 = 移动，与文件管理器一致），无需在这里按 Ctrl 重配。
    // 这里只负责：收集选中项快照 + 设置拖拽图标。

    GdkModifierType state = 0;
    gtk_get_current_event_state(&state);
    const gboolean is_copy = (state & GDK_CONTROL_MASK) != 0;
    g_autoptr(GdkPixbuf) icon = make_drag_icon(g_list_length(drag_paths), is_copy);
    if (icon) {
        gtk_drag_source_set_icon_pixbuf(widget, icon);
    }
    else {
        gtk_drag_source_set_icon_name(widget, "io.github.cboxdoerfer.FSearch");
    }
}

static void
on_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info,
                 guint time, gpointer user_data) {
    (void)widget;
    (void)context;
    (void)time;
    (void)user_data;
    // ⚠️ gtk_selection_data_get_target() 返回的是 GdkAtom（X11 原子，本质是个整数），
    // 【不是字符串】！直接当 %s 打印会把原子值当指针解引用 → 段错误
    //（真机 gdb 栈实证：崩在 drag_log 的 vasprintf 里）。转字符串用 gdk_atom_name()。
    GdkAtom target_atom = gtk_selection_data_get_target(selection_data);
    g_autofree char *target_name = target_atom ? gdk_atom_name(target_atom) : NULL;
    drag_log("on_drag_data_get: 目标要数据 (info=%u target=%s)", info,
             target_name ? target_name : "(unknown)");
    if (!drag_paths) {
        drag_log("on_drag_data_get: ✗ drag_paths 为空，返回空载荷");
        // GTK 3 没有 gtk_selection_data_clear()（那是 GTK 4 的 API）。
        // 传空载荷表示"不提供数据"，效果等同于清空。
        gtk_selection_data_set(selection_data,
                                gtk_selection_data_get_target(selection_data),
                                8,
                                (const guchar *)"",
                                0);
        return;
    }

    GString *payload = g_string_new(info == 1 ? "copy\n" : NULL);
    for (GList *l = drag_paths; l; l = l->next) {
        char *uri = g_filename_to_uri((const char *)l->data, NULL, NULL);
        if (uri) {
            g_string_append(payload, uri);
            g_string_append_c(payload, '\n');
            g_free(uri);
        }
    }
    gtk_selection_data_set(selection_data,
                            gtk_selection_data_get_target(selection_data),
                            8,
                            (const guchar *)payload->str,
                            (int)payload->len);
    g_string_free(payload, TRUE);
}

static void
on_drag_end(GtkWidget *widget, GdkDragContext *context, gboolean delete_data, gpointer user_data) {
    (void)widget;
    (void)context;
    (void)delete_data;
    (void)user_data;
    clear_drag_paths();
    // 移动/复制完成后无需手工刷新：文件监控（inotify）会自动把新路径纳入索引。
}

void
fsearch_drag_drop_init(FsearchApplicationWindow *win) {
    g_return_if_fail(FSEARCH_IS_APPLICATION_WINDOW(win));

    // 不能写 win->result_view->list_view：FsearchApplicationWindow 是 G_DECLARE_FINAL_TYPE，
    // 其结构体定义只存在于 fsearch_window.c 里（不透明类型），外部文件直接取成员会报
    // "dereferencing pointer to incomplete type"。必须用头文件里现成的 getter。
    FsearchListView *list_view_model = fsearch_application_window_get_listview(win);
    if (!list_view_model) {
        // 【不要静默返回】之前 list_view 未初始化时走到这里直接 return，
        // 拖拽源从未配置且无任何痕迹，排查花了一整轮（真机教训）。
        drag_log("init: ✗ list_view 为 NULL —— init 调用时机早于 init_listview，拖拽源未配置");
        g_warning("[drag] list_view 为 NULL，拖拽源未配置（init 调用时机过早）");
        return;
    }
    drag_log("init: 拖拽源配置到 list_view (%p)", (void *)list_view_model);

    // GTK 3：拖拽目标表存到 widget 的 site 上（供图标 API 用），但不连接内置按压检测
    //（发起由手势单点控制，见 fsearch_drag_drop_begin_from_view 上方说明）。
    GtkWidget *list_view = GTK_WIDGET(list_view_model);
    drag_source_configure(list_view);
    gtk_drag_source_set_icon_name(list_view, "io.github.cboxdoerfer.FSearch");

    // drag-begin / drag-data-get / drag-end 都是 GtkWidget 的信号
    g_signal_connect(list_view, "drag-begin", G_CALLBACK(on_drag_begin), win);
    g_signal_connect(list_view, "drag-data-get", G_CALLBACK(on_drag_data_get), win);
    g_signal_connect(list_view, "drag-end", G_CALLBACK(on_drag_end), win);
}

/*
 * 由 FsearchListView 的拖拽手势主动发起拖拽（fsearch_list_view.c 调用）。
 *
 * 【为什么不用 gtk_drag_source_set 的内置按压检测】
 * FsearchListView 是自绘控件，自身用 GtkGestureDrag 处理框选。实测发现：即使手势在
 * "按在已选行上"时 DENY 让位，gtk_drag_source_set 依赖的 press/motion 事件流仍可能
 * 被手势体系拦截，导致拖拽从未发起（真机实测：拖拽完全无反应）。
 * 因此改为在手势的 drag-begin 回调（已越过移动阈值）里【主动】发起：
 *   - 按"已选行"拖动超过阈值 → 到这里 → 直接 gtk_drag_begin_with_coordinates；
 *   - 普通点击不越过阈值 → 不会走到这里，不影响点击行为。
 * target list 用模块自建的 own_target_list（init 时通过 gtk_drag_source_set_target_list
 * 存到 widget 上，但 widget 上【没有】内置按压检测——发起只有手势这一条路）。
 */
void
fsearch_drag_drop_begin_from_view(GtkWidget *widget, gdouble x, gdouble y) {
    g_return_if_fail(GTK_IS_WIDGET(widget));

    drag_log("begin_from_view: 进入 (x=%.0f y=%.0f) own_target_list=%s", x, y,
             own_target_list ? "有效" : "✗ NULL");

    if (!own_target_list) {
        drag_log("begin_from_view: ✗ target list 未初始化 —— init 未成功执行");
        g_return_if_reached();
    }

    // 动作集：MOVE|COPY 同时提供，最终用移动还是复制由放下时的目标 + 修饰键决定
    // （按住 Ctrl 拖 = 复制，直接拖 = 移动，与文件管理器一致）。
    // event 传 NULL：文档明确允许，由 GTK 自行取当前设备；
    // 传 gtk_get_current_event() 反有拿到过期/异常事件的风险。
    GdkDragContext *ctx = gtk_drag_begin_with_coordinates(widget,
                                                          own_target_list,
                                                          GDK_ACTION_MOVE | GDK_ACTION_COPY,
                                                          1, // 左键
                                                          NULL,
                                                          (gint)x,
                                                          (gint)y);
    drag_log("begin_from_view: gtk_drag_begin_with_coordinates → %s",
             ctx ? "拖拽已发起" : "✗ 返回 NULL（发起失败）");
}

void
fsearch_drag_drop_log_branch(int press_row, gdouble x, gdouble y) {
    drag_log("手势分支命中: 按在已选行 row=%d (x=%.0f y=%.0f)，即将发起拖拽", press_row, x, y);
}
