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
*/

#pragma once

#include <glib.h>

// 在文件管理器右键菜单注册/注销 "用 FSearch 搜索…" 项（deepin/UOS dde-file-manager 的
// OEM 上下文菜单扩展机制）。点击后会以 fsearch --search-in=<文件夹> 启动，限定在当前
// 文件夹内搜索。
// enabled = true  : 注册菜单项（写入 oem-menuextensions 目录下的 .desktop）。
// enabled = false : 移除菜单项。
void fsearch_context_menu_set_enabled(gboolean enabled);
