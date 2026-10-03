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

#ifndef FSEARCH_DESKTOP_SHORTCUT_H
#define FSEARCH_DESKTOP_SHORTCUT_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * fsearch_resolve_desktop_target_dir:
 * @path: 候选路径（可能是普通目录、目录本身，或一个 .desktop 快捷方式文件）
 *
 * 解析"文件夹快捷方式"所指向的源目录，用于右键菜单里对快捷方式执行"用 FSearch 搜索"。
 *
 * 支持两类常见写法：
 *   1) [Desktop Entry] + Type=Link + URL=file:///path/to/dir[/]
 *      —— deepin / UOS「发送到桌面 → 创建快捷方式」以及手工制作的目录快捷方式
 *   2) [Desktop Entry] + Type=Application + Exec=…/open /path/to/dir
 *      —— 部分工具用 Exec 形式指向目录
 *
 * 返回值: 新分配的目标目录绝对路径（不含尾部斜杠），调用者用 g_free() 释放；
 *         无法解析或目标不是目录时返回 NULL。
 */
char *
fsearch_resolve_desktop_target_dir(const char *path);

G_END_DECLS

#endif // FSEARCH_DESKTOP_SHORTCUT_H
