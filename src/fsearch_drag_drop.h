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

#ifndef FSEARCH_DRAG_DROP_H
#define FSEARCH_DRAG_DROP_H

#include <gtk/gtk.h>

// 声明里要用到 FsearchApplicationWindow。这个类型由 fsearch_window.h 提供，
// 本头文件必须显式包含它，否则调用方（含 .c 文件）只看到前向声明会报
// "unknown type name 'FsearchApplicationWindow'"。
// fsearch_window.h 不会反过来包含本头文件，因此不存在循环依赖。
#include "fsearch_window.h"

G_BEGIN_DECLS

/**
 * fsearch_drag_drop_init:
 * @win: 搜索结果窗口
 *
 * 为结果列表启用"多选后拖拽"：把选中的文件以 text/uri-list 拖到任意支持拖放的目标
 * （文件管理器、桌面、其它应用的输入框等）。
 *
 * 操作语义（与 Linux 桌面通用约定一致）：
 *   - 直接拖拽        → 移动（剪切）
 *   - 按住 Ctrl 拖拽  → 复制
 * 拖拽开始时会显示带"N 个项目 / 移动|复制"字样的浮动图标，操作一目了然。
 */
void
fsearch_drag_drop_init(FsearchApplicationWindow *win);

/**
 * fsearch_drag_drop_begin_from_view:
 * @widget: 结果列表控件（FsearchListView）
 * @x: 拖拽起始点（widget 坐标）
 * @y: 拖拽起始点（widget 坐标）
 *
 * 由 FsearchListView 的拖拽手势在"按住已选行并移动超过阈值"时调用，
 * 主动发起一次拖拽（不依赖 gtk_drag_source_set 的内置按压检测）。
 * 详见 fsearch_drag_drop.c 中该函数上方的说明。
 */
void
fsearch_drag_drop_begin_from_view(GtkWidget *widget, gdouble x, gdouble y);

/**
 * fsearch_drag_drop_log_branch:
 * @press_row: 按下的行号
 * @x: 拖拽起始点（widget 坐标）
 * @y: 拖拽起始点（widget 坐标）
 *
 * 调试用：在手势走到"按在已选行上"分支时记录一条日志，
 * 用于远程排查"拖拽没反应"时确认手势链路是否到达。
 */
void
fsearch_drag_drop_log_branch(int press_row, gdouble x, gdouble y);

G_END_DECLS

#endif // FSEARCH_DRAG_DROP_H
