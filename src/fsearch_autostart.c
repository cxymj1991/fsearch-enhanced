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

#include "fsearch_autostart.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

// 取得“启动 fsearch 的入口”路径：
//  - 便携包用 shell 脚本做启动器（设 LD_LIBRARY_PATH 后才 exec 真正的二进制），
//    此时 /proc/self/exe 指向 shell 解释器而非 fsearch，故启动器会 export
//    FSEARCH_LAUNCHER 指向自身（即 /opt/fsearch/bin/fsearch）。优先用它。
//  - 普通系统安装（/usr/bin/fsearch 就是真实二进制）无该变量，回退 readlink。
static char *
get_exe_path(void) {
    const char *launcher = g_getenv("FSEARCH_LAUNCHER");
    if (launcher && *launcher) {
        return g_strdup(launcher);
    }
    char buf[PATH_MAX] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return NULL;
    }
    buf[n] = '\0';
    return g_strdup(buf);
}

void
fsearch_autostart_set_enabled(gboolean enabled) {
    g_autofree char *autostart_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_autofree char *path = g_build_filename(autostart_dir, "fsearch.desktop", NULL);

    if (!enabled) {
        g_unlink(path);
        return;
    }

    g_autofree char *exe = get_exe_path();
    if (!exe) {
        g_warning("[autostart] 无法确定 fsearch 可执行文件路径，跳过自启动注册");
        return;
    }

    if (g_mkdir_with_parents(autostart_dir, 0700) != 0) {
        g_warning("[autostart] 无法创建目录 %s", autostart_dir);
        return;
    }

    GString *content = g_string_new(NULL);
    g_string_append_printf(content,
                           "[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=FSearch\n"
                           "Comment=Fast file search utility\n"
                           "Exec=%s --hidden\n"
                           "Icon=io.github.cboxdoerfer.FSearch\n"
                           "Terminal=false\n"
                           "NoDisplay=true\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "X-GNOME-Autostart-Delay=2\n",
                           exe);

    GError *error = NULL;
    if (!g_file_set_contents(path, content->str, -1, &error)) {
        g_warning("[autostart] 写入 %s 失败: %s", path, error ? error->message : "未知错误");
        g_clear_error(&error);
    }
    else {
        chmod(path, 0644);
    }
    g_string_free(content, TRUE);
}
