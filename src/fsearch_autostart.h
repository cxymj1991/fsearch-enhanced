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

// 在用户级 autostart 目录写入/删除自启动项。
// enabled = true  : 系统登录后自动在后台启动 fsearch（带 --hidden，仅驻留托盘并实时索引）。
// enabled = false : 移除自启动项。
void fsearch_autostart_set_enabled(gboolean enabled);
