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

/*
 * fsearch_compat.h —— 针对旧版 GLib 的编译期兼容垫片。
 *
 * 背景：FSearch 0.3.2 的 src/meson.build 要求 GLib >= 2.62，而统信 UOS 20 只有 2.58.3。
 * 本文件为所有 0.3.2 用到、但 2.58 上不存在的 API 提供语义等价的实现。
 *
 * 已确认需要垫片的 API 及其**真实**引入版本（均查过官方文档，不是推测）：
 *   - g_ptr_array_copy()                     GLib 2.62
 *   - g_clear_signal_handler()               GLib 2.62
 *   - g_app_info_launch_uris_async()          GLib 2.60  ← 上游 fsearch_file_utils.c
 *   - g_app_info_launch_uris_finish()         GLib 2.60
 *   - g_file_query_default_handler_async()    GLib 2.60  ← 上游 fsearch_file_utils.c
 *   - g_file_query_default_handler_finish()   GLib 2.60
 *   - g_spawn_check_wait_status()             GLib 2.70  ← 本项目 fsearch_autostart.c
 *
 * 之前误以为 g_file_query_default_handler{,_async,_finish} 是 2.28、2.58 上直接可用，
 * 首轮真机链接时才发现 undefined reference —— 教训见下方 g_spawn_check_wait_status 注释。
 *
 * 做法：按各自真实版本分别用 GLIB_CHECK_VERSION 条件编译。
 * 好处：
 *   - UOS 20（GLib 2.58.3）上直接可编译，不需要"构建前用脚本改写源码"的脆弱做法；
 *   - GLib 足够新时自动使用系统原生实现，行为与上游完全一致；
 *   - 垫片只补 API 缺失，不改动任何业务逻辑。
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
// g_desktop_app_info_new()：用于把 GFileInfo 转成 GAppInfo（见下方 2.60 垫片）。
// 该头文件属于 GIO 平台相关部分，但上游 fsearch_file_utils.c 本身也包含了它。
#include <gio/gdesktopappinfo.h>

#if !GLIB_CHECK_VERSION(2, 62, 0)

/**
 * g_ptr_array_copy:
 * @array: 源数组（不能为 NULL）
 * @func: 元素拷贝函数；为 NULL 时直接复制指针
 * @user_data: 传给 @func 的用户数据
 *
 * GLib 2.62 引入。此处提供等价实现：先按源数组长度分配数组，再逐元素拷贝，
 * 与上游 g_ptr_array_copy() 的返回值语义一致（新数组的 free_func 为 NULL，
 * 元素所有权转移到调用方）。
 */
static inline GPtrArray *
g_ptr_array_copy(GPtrArray *array, GCopyFunc func, gpointer user_data) {
    g_return_val_if_fail(array != NULL, NULL);
    GPtrArray *new_array = g_ptr_array_sized_new(array->len);
    for (guint i = 0; i < array->len; ++i) {
        gpointer item = g_ptr_array_index(array, i);
        g_ptr_array_add(new_array, func ? func(item, user_data) : item);
    }
    return new_array;
}

/**
 * g_clear_signal_handler: (skip)
 * @handler_id: (inout): 信号处理器 id，处理后被置为 0
 * @instance: 处理器所连接的对象
 *
 * GLib 2.62 引入的宏。GLib 2.58 上的等价写法：非零则 disconnect，然后置 0。
 */
#define g_clear_signal_handler(handler_id, instance)                                                                 \
    G_STMT_START                                                                                                 \
    {                                                                                                             \
        gulong *_fsearch_chs_id = (gulong *)(handler_id);                                                         \
        if (*_fsearch_chs_id) {                                                                                   \
            g_signal_handler_disconnect((instance), *_fsearch_chs_id);                                           \
            *_fsearch_chs_id = 0;                                                                                 \
        }                                                                                                         \
    }                                                                                                             \
    G_STMT_END

#endif // !GLIB_CHECK_VERSION(2, 62, 0)

/*
 * GLib 2.60 引入的异步 API（上游 fsearch_file_utils.c 使用）。
 *
 * 它们的**同步**版本 g_app_info_launch_uris() 与 g_file_query_default_handler()
 * 都是 GLib 2.28 引入，2.58 上现成可用，因此这里用"同步执行 + 立即回调"的方式
 * 复现异步接口的语义：
 *   - 成功：构造一个"无错误"的 GAsyncResult 并立刻调用 callback；
 *   - 失败：构造一个带 GError 的 GAsyncResult，finish() 会取出该错误。
 *
 * 注意与上游的差异：同步执行会阻塞调用方。对本项目无影响 —— 该路径只在用户
 * 双击打开文件时触发，耗时很短；换来的好处是免去为 UOS 20 维护一份分支代码。
 */
#if !GLIB_CHECK_VERSION(2, 60, 0)

/**
 * g_file_query_default_handler_async: (skip)
 *
 * GLib 2.60 引入。此处用同步的 g_file_query_default_handler() 实现，
 * 完成后立即回调 @callback。
 *
 * 【关键】g_file_query_default_handler()（2.28 起）返回的就是 **GAppInfo\***，
 * 与 2.60 的 g_file_query_default_handler_finish() 返回类型一致。
 * 之前垫片错把它当 GFileInfo* 去 g_file_info_get_name()，读到的是 GAppInfo
 * 对象内的无关字段，得到垃圾/NULL → 双击打开文件报"未找到默认处理器: (未知)"。
 * 真机实测踩过，这里必须直接透传 GAppInfo。
 */
static inline void
g_file_query_default_handler_async(GFile *file, gint io_priority, GCancellable *cancellable,
                                   GAsyncReadyCallback callback, gpointer user_data) {
    (void)io_priority;
    g_autoptr(GError) error = NULL;
    // 同步版返回 GAppInfo*（已带引用），失败时返回 NULL 并设置 error
    GAppInfo *app_info = g_file_query_default_handler(file, cancellable, &error);

    GTask *task = g_task_new(G_OBJECT(file), cancellable, callback, user_data);
    if (app_info) {
        g_task_return_pointer(task, app_info, g_object_unref);
    }
    else {
        g_task_return_error(task, g_steal_pointer(&error));
    }
    g_object_unref(task);
}

/**
 * g_file_query_default_handler_finish: (skip)
 *
 * GLib 2.60 引入。取出 g_file_query_default_handler_async() 的结果。
 * 返回 GAppInfo*（与上游一致，不是 GFileInfo*）。
 */
static inline GAppInfo *
g_file_query_default_handler_finish(GFile *file, GAsyncResult *result, GError **error) {
    (void)file;
    return g_task_propagate_pointer(G_TASK(result), error);
}

/**
 * g_app_info_launch_uris_async: (skip)
 *
 * GLib 2.60 引入。此处用同步的 g_app_info_launch_uris() 实现。
 */
static inline void
g_app_info_launch_uris_async(GAppInfo *appinfo, GList *uris, GAppLaunchContext *context, GCancellable *cancellable,
                             GAsyncReadyCallback callback, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    GTask *task = g_task_new(G_OBJECT(appinfo), cancellable, callback, user_data);
    g_app_info_launch_uris(appinfo, uris, context, &error);
    if (error) {
        g_task_return_error(task, g_steal_pointer(&error));
    }
    else {
        g_task_return_boolean(task, TRUE);
    }
    g_object_unref(task);
}

/**
 * g_app_info_launch_uris_finish: (skip)
 *
 * GLib 2.60 引入。取出 g_app_info_launch_uris_async() 的结果。
 */
static inline gboolean
g_app_info_launch_uris_finish(GAppInfo *appinfo, GAsyncResult *result, GError **error) {
    (void)appinfo;
    return g_task_propagate_boolean(G_TASK(result), error);
}

#endif // !GLIB_CHECK_VERSION(2, 60, 0)

/*
 * g_spawn_check_wait_status: GLib 2.70 引入。
 *
 * 本项目 fsearch_autostart.c 的 run_cmd() 用它判断 systemctl 的退出状态。
 * 2.70 之前等价功能的函数叫 g_spawn_check_exit_status()（2.34 引入），
 * 语义一致：成功返回 TRUE，失败返回 FALSE 并设置 error。
 *
 * ⚠️ 教训：我最初误以为这个函数是 2.34 引入、首轮真机链接才发现 undefined reference。
 * 说明"查过印象里的版本号"不可靠，必须逐个查官方文档确认。
 */
#if !GLIB_CHECK_VERSION(2, 70, 0)
#define g_spawn_check_wait_status(wait_status, error) g_spawn_check_exit_status((wait_status), (error))
#endif
