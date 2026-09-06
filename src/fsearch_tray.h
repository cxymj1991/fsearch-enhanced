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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

// 初始化系统托盘图标（GtkStatusIcon，无需额外依赖）。
void fsearch_tray_init(void);

// 销毁托盘图标（应用退出时调用）。
void fsearch_tray_shutdown(void);

// 把已最小化的窗口重新显示出来（托盘菜单"显示"使用）。
void fsearch_tray_show_window(void);

// 标记"强制退出"：为 TRUE 时点击窗口关闭按钮会真正退出而非最小化到托盘。
void fsearch_tray_set_force_quit(gboolean v);

// 读取"强制退出"标记。
gboolean fsearch_tray_get_force_quit(void);

G_END_DECLS
