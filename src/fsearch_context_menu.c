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

#include "fsearch_context_menu.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

// 候选的 OEM 菜单扩展目录（deepin/UOS dde-file-manager）：
//  - 系统级目录（安装时通常由 root 创建并 chown 给真实用户，使普通用户也能增删）
//  - 用户级数据目录（兜底，部分版本也会读取）
static const char *
oem_dirs[] = {
    "/usr/share/deepin/dde-file-manager/oem-menuextensions",
    NULL, // 用户级目录在运行时按 XDG_DATA_HOME 拼接
};

#define FSEARCH_OEM_FILENAME "fsearch-search.desktop"

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

static GPtrArray *
collect_oem_paths(void) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    for (int i = 0; oem_dirs[i]; i++) {
        g_ptr_array_add(paths, g_build_filename(oem_dirs[i], FSEARCH_OEM_FILENAME, NULL));
    }
    // 用户级目录
    g_ptr_array_add(paths, g_build_filename(g_get_user_data_dir(),
                                            "deepin/dde-file-manager/oem-menuextensions",
                                            FSEARCH_OEM_FILENAME, NULL));
    return paths;
}

static void
remove_all_oem(void) {
    g_autoptr(GPtrArray) paths = collect_oem_paths();
    for (guint i = 0; i < paths->len; i++) {
        g_unlink((const char *)g_ptr_array_index(paths, i));
    }
}

void
fsearch_context_menu_set_enabled(gboolean enabled) {
    if (!enabled) {
        remove_all_oem();
        return;
    }

    g_autofree char *exe = get_exe_path();
    if (!exe) {
        g_warning("[context-menu] 无法确定 fsearch 可执行文件路径，跳过右键菜单注册");
        return;
    }

    GString *content = g_string_new(NULL);
    g_string_append_printf(content,
                           "[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Search with FSearch…\n"
                           "Name[zh_CN]=用 FSearch 搜索…\n"
                           "GenericName=Search files in this folder\n"
                           "GenericName[zh_CN]=在当前文件夹中搜索文件\n"
                           "Comment=Search files in this folder with FSearch\n"
                           "Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件\n"
                           "Icon=io.github.cboxdoerfer.FSearch\n"
                           "MimeType=inode/directory;\n"
                           "Exec=%s --search-in=%%f\n"
                           "Terminal=false\n"
                           "X-DFM-MenuTypes=SingleDir\n",
                           exe);

    g_autoptr(GPtrArray) paths = collect_oem_paths();
    gboolean wrote = FALSE;
    for (guint i = 0; i < paths->len; i++) {
        const char *path = (const char *)g_ptr_array_index(paths, i);
        g_autofree char *dir = g_path_get_dirname(path);
        if (g_mkdir_with_parents(dir, 0755) != 0) {
            continue; // 无写入权限则尝试下一个位置
        }
        GError *error = NULL;
        if (g_file_set_contents(path, content->str, -1, &error)) {
            chmod(path, 0644);
            wrote = TRUE;
        }
        else {
            g_debug("[context-menu] 写入 %s 失败: %s", path, error ? error->message : "未知错误");
            g_clear_error(&error);
        }
    }

    if (!wrote) {
        g_warning("[context-menu] 无法写入 OEM 菜单扩展文件（需要 %s 可写，或在安装时由 root 创建并归属当前用户）",
                  oem_dirs[0]);
    }
    g_string_free(content, TRUE);
}
