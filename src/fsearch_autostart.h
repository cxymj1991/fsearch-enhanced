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

// 注册/注销"开机自动在后台启动"（带 --hidden，仅驻留托盘并实时索引）。
//
// 实现说明（统信 UOS 20 适配）：
//   首选走 systemd --user 用户服务（~/.config/systemd/user/fsearch.service），
//   完全绕开 XDG autostart 目录，因此不会触发 UOS 的「是否允许 XXX 开机启动」授权弹窗；
//   仅当 systemd --user 不可用时，才退回写 ~/.config/autostart/*.desktop。
//
// enabled = true  : 登录后自动后台启动 fsearch。
// enabled = false : 移除自启动配置。
void fsearch_autostart_set_enabled(gboolean enabled);
