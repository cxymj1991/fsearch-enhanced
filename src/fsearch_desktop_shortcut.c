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

#include "fsearch_desktop_shortcut.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h> // realpath()
#include <string.h>

// 去掉路径尾部的斜杠（根目录 "/" 除外），使前缀比较与显示都更稳定。
static char *
strip_trailing_slashes(char *path) {
    if (!path) {
        return NULL;
    }
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    return path;
}

// 把一个 file:// URI 转成本地路径；不是 file:// URI 时返回 NULL。
static char *
uri_to_local_path(const char *uri) {
    if (!uri || !g_str_has_prefix(uri, "file://")) {
        return NULL;
    }
    // g_filename_from_uri 要求百分号转义已解码；这里手工解码以兼容未转义的中文/空格路径。
    g_autofree char *decoded = g_uri_unescape_string(uri, NULL);
    if (decoded && g_str_has_prefix(decoded, "file://")) {
        char *p = g_filename_from_uri(decoded, NULL, NULL);
        if (p) {
            return p;
        }
    }
    // 兜底：直接砍掉 "file://" 前缀
    char *raw = g_strdup(uri + strlen("file://"));
    if (raw && g_utf8_validate(raw, -1, NULL)) {
        return raw;
    }
    g_free(raw);
    return NULL;
}

// 从 Exec= 行里提取"看起来像路径"的参数。
// 依次尝试：所有引号内片段 → 所有空白分隔片段；第一个能解析成已存在目录的即返回。
static char *
extract_dir_from_exec(const char *exec_line) {
    if (!exec_line || !*exec_line) {
        return NULL;
    }

    // 1) 引号包裹的参数（Exec 允许用 "..." 包裹含空格的路径）
    g_auto(GStrv) quoted = NULL;
    {
        GString *tmp = g_string_new(NULL);
        gboolean in_quote = FALSE;
        for (const char *p = exec_line; *p; p++) {
            if (*p == '"') {
                in_quote = !in_quote;
                continue;
            }
            if (in_quote) {
                g_string_append_c(tmp, *p);
            }
        }
        if (tmp->len > 0) {
            quoted = g_strsplit(tmp->str, " ", -1);
        }
        g_string_free(tmp, TRUE);
    }
    if (quoted) {
        for (int i = 0; quoted[i]; i++) {
            char *cand = uri_to_local_path(quoted[i]);
            if (!cand) {
                cand = g_strdup(quoted[i]);
            }
            if (cand && g_file_test(cand, G_FILE_TEST_IS_DIR)) {
                return strip_trailing_slashes(cand);
            }
            g_free(cand);
        }
    }

    // 2) 空白分隔的裸参数
    g_auto(GStrv) parts = g_strsplit(exec_line, " ", -1);
    // 从后往前找：目录路径通常是最后一个参数（前面是程序名和可能的 %U）
    for (int i = 0; parts[i]; i++) {
        if (!*parts[i] || parts[i][0] == '%' || parts[i][0] == '-') {
            continue;
        }
        char *cand = uri_to_local_path(parts[i]);
        if (!cand) {
            cand = g_strdup(parts[i]);
        }
        if (cand && g_file_test(cand, G_FILE_TEST_IS_DIR)) {
            return strip_trailing_slashes(cand);
        }
        g_free(cand);
    }

    return NULL;
}

char *
fsearch_resolve_desktop_target_dir(const char *path) {
    if (!path || !*path) {
        return NULL;
    }

    // 已经是目录就直接返回（解析成真实路径，兼容指向目录的符号链接）
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        char *real = realpath(path, NULL);
        return real ? strip_trailing_slashes(real) : strip_trailing_slashes(g_strdup(path));
    }

    // 只对 .desktop 文件做解析
    if (!g_str_has_suffix(path, ".desktop") || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        return NULL;
    }

    g_autoptr(GKeyFile) kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        return NULL;
    }

    const char *group = "Desktop Entry";
    // 只有 Shortcut/Link 类型的条目才视为"快捷方式"
    g_autofree char *type = g_key_file_get_string(kf, group, "Type", NULL);
    if (g_strcmp0(type, "Link") != 0 && g_strcmp0(type, "Shortcut") != 0 && g_strcmp0(type, "Application") != 0) {
        return NULL;
    }

    // 优先 URL=（目录快捷方式最常见的写法）
    g_autofree char *url = g_key_file_get_string(kf, group, "URL", NULL);
    if (url) {
        char *local = uri_to_local_path(url);
        if (local && g_file_test(local, G_FILE_TEST_IS_DIR)) {
            return strip_trailing_slashes(local);
        }
        g_free(local);
    }

    // 其次 Exec=
    g_autofree char *exec_line = g_key_file_get_string(kf, group, "Exec", NULL);
    if (exec_line) {
        char *dir = extract_dir_from_exec(exec_line);
        if (dir) {
            return dir;
        }
    }

    return NULL;
}
