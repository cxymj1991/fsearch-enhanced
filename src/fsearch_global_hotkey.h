/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   自定义全局唤起快捷键：通过 X11 XGrabKey 注册系统级热键，
   即使 FSearch 不在前台（例如仅驻留托盘）也能一键唤起/聚焦窗口。
*/

#pragma once

#include <glib.h>

// 初始化模块（获取 X 显示、安装事件过滤器）。非 X11 环境下为 no-op。
void
fsearch_global_hotkey_init(void);

// 注销并移除事件过滤器（程序退出时调用）。
void
fsearch_global_hotkey_shutdown(void);

// 设置/更换全局快捷键。
// accel：GTK 加速键字符串，例如 "<Super>space"、"<Control><Alt>f"、"F10"；
//        传 NULL 或空串表示关闭全局快捷键（解除旧绑定）。
void
fsearch_global_hotkey_set(const char *accel);

// 设置触发时的回调（由主程序提供，用于唤起/聚焦窗口）。
void
fsearch_global_hotkey_set_activate_callback(void (*cb)(void));
