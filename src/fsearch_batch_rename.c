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

#include "fsearch_batch_rename.h"

#include "fsearch.h"
#include "fsearch_array.h"
#include "fsearch_database.h"
#include "fsearch_database_entry.h"
#include "fsearch_database_work.h"
#include "fsearch_ui_utils.h"
#include "fsearch_window.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// 预览列表列
enum {
    COL_OLD_NAME,
    COL_NEW_NAME,
    COL_STATUS,
    COL_STATUS_COLOR,
    N_COLS
};

// 序号位置
enum {
    NUMBER_POS_PREFIX = 0,
    NUMBER_POS_SUFFIX,
};

// 大小写模式
enum {
    CASE_NONE = 0,
    CASE_UPPER,
    CASE_LOWER,
    CASE_CAPITALIZE,
    CASE_CAPITALIZE_WORDS,
};

// 状态
enum {
    STATUS_READY = 0,
    STATUS_NO_CHANGE,
    STATUS_CONFLICT,
};

// 查找替换的“出现次数”模式：0=全部，1=仅第 N 个，2=第 X 至第 Y 个，3=仅最后一个
enum {
    FIND_OCCUR_ALL = 0,
    FIND_OCCUR_NTH,
    FIND_OCCUR_RANGE,
    FIND_OCCUR_LAST,
};

typedef struct BatchRenameData {
    FsearchApplicationWindow *win;

    // 规则控件
    GtkWidget *enable_find;
    GtkWidget *find_entry;
    GtkWidget *replace_entry;
    GtkWidget *regex_check;
    GtkWidget *find_occur_combo; // 全部 / 仅第 N 个 / 第 X 至第 Y 个 / 仅最后一个
    GtkWidget *find_nth_spin;    // “仅第 N 个 / 第 X 至第 Y 个”的起始序号（1 基）
    GtkWidget *find_nth2_spin;   // “第 X 至第 Y 个”的结束序号（1 基）

    GtkWidget *enable_prefix;
    GtkWidget *prefix_entry;

    GtkWidget *enable_suffix;
    GtkWidget *suffix_entry;

    GtkWidget *enable_numbering;
    GtkWidget *num_start;
    GtkWidget *num_step;
    GtkWidget *num_digits;
    GtkWidget *num_pos;

    GtkWidget *enable_case;
    GtkWidget *case_combo;

    GtkWidget *enable_ext;
    GtkWidget *ext_entry;
    GtkWidget *delete_ext_check;

    GtkWidget *enable_remove;
    GtkWidget *remove_start;
    GtkWidget *remove_count;

    // 预览
    GtkWidget *tree;
    GtkListStore *store;
    GtkWidget *status_label;

    // 数据
    guint n;           // 选中文件数
    gchar **old_names; // 原名
    gchar **old_paths; // 完整路径
    gchar **new_names; // 计算后的新名
    gchar **new_paths; // 计算后的完整新路径
    gint *status;      // STATUS_*

    // 撤销
    GList *undo_old;   // 撤销：旧完整路径（原名）
    GList *undo_new;   // 撤销：新完整路径（改名后）
    GtkWidget *btn_undo;
    GtkWidget *btn_rename;
} BatchRenameData;

// ---------- 小工具 ----------

// GLib 2.58 无 g_str_replace（2.68 才有），这里手动实现全量替换
static gchar *
string_replace_all(const gchar *haystack, const gchar *needle, const gchar *replace) {
    if (!haystack || !needle || !*needle) {
        return g_strdup(haystack);
    }
    GString *out = g_string_sized_new(strlen(haystack) + 32);
    const gchar *p = haystack;
    gsize needle_len = strlen(needle);
    while (TRUE) {
        const gchar *found = strstr(p, needle);
        if (!found) {
            g_string_append(out, p);
            break;
        }
        g_string_append_len(out, p, found - p);
        g_string_append(out, replace ? replace : "");
        p = found + needle_len;
    }
    return g_string_free(out, FALSE);
}

// 普通文本：只替换第 occur_idx 个匹配（0 基）。occur_idx 超界或没有足够匹配时返回原串副本。
// 用于“仅第 N 个 / 仅最后一个”模式（全部替换走 string_replace_all）。
static gchar *
string_replace_nth(const gchar *haystack, const gchar *needle, const gchar *replace, gint occur_idx) {
    if (!haystack || !needle || !*needle || occur_idx < 0) {
        return g_strdup(haystack);
    }
    GString *out = g_string_sized_new(strlen(haystack) + 32);
    const gchar *p = haystack;
    gsize needle_len = strlen(needle);
    gint seen = 0;
    while (TRUE) {
        const gchar *found = strstr(p, needle);
        if (!found) {
            g_string_append(out, p);
            break;
        }
        if (seen == occur_idx) {
            g_string_append_len(out, p, found - p);
            g_string_append(out, replace ? replace : "");
            p = found + needle_len;
            g_string_append(out, p); // 剩余部分原样追加
            return g_string_free(out, FALSE);
        }
        // 未到目标位置：跳过这个匹配，继续找下一个
        g_string_append_len(out, p, found - p + needle_len);
        p = found + needle_len;
        seen++;
    }
    return g_string_free(out, FALSE);
}

// 普通文本：替换第 from_idx 到 to_idx 个匹配（0 基，含两端）。范围超界时仅替换实际存在的匹配。
// 用于“第 X 至第 Y 个”模式。
static gchar *
string_replace_range(const gchar *haystack, const gchar *needle, const gchar *replace, gint from_idx, gint to_idx) {
    if (!haystack || !needle || !*needle || from_idx < 0 || to_idx < from_idx) {
        return g_strdup(haystack);
    }
    GString *out = g_string_sized_new(strlen(haystack) + 32);
    const gchar *p = haystack;
    gsize needle_len = strlen(needle);
    gint seen = 0;
    while (TRUE) {
        const gchar *found = strstr(p, needle);
        if (!found) {
            g_string_append(out, p);
            break;
        }
        if (seen >= from_idx && seen <= to_idx) {
            g_string_append_len(out, p, found - p);
            g_string_append(out, replace ? replace : "");
        }
        else {
            // 范围外的匹配原样保留
            g_string_append_len(out, p, found - p + needle_len);
        }
        p = found + needle_len;
        seen++;
    }
    return g_string_free(out, FALSE);
}

// 正则：只替换第 occur_idx 个匹配（0 基）。occur_idx 超界或没有足够匹配时返回原串副本。
// 实现：先数到第 occur_idx 个匹配，拿到它的起始字节位置，再用 g_regex_replace 的
// start_position 参数从该位置开始替换（该位置之后第一个匹配即目标，且保留前面所有文本、支持 \1 反向引用）。
static gchar *
regex_replace_nth(const gchar *haystack, GRegex *re, const gchar *replace, gint occur_idx) {
    if (!haystack || occur_idx < 0) {
        return g_strdup(haystack);
    }
    GMatchInfo *info = NULL;
    g_regex_match(re, haystack, 0, &info);
    gint seen = 0;
    gint target_start = -1;
    while (g_match_info_matches(info)) {
        if (seen == occur_idx) {
            gint start = 0;
            g_match_info_fetch_pos(info, 0, &start, NULL);
            target_start = start;
            break;
        }
        seen++;
        g_match_info_next(info, NULL);
    }
    g_match_info_free(info);

    if (target_start < 0) {
        return g_strdup(haystack);
    }
    gchar *res = g_regex_replace(re, haystack, -1, target_start, replace, 0, NULL);
    return res ? res : g_strdup(haystack);
}

// 正则：替换第 from_idx 到 to_idx 个匹配（0 基，含两端）。
// 实现：先收集所有匹配的起始位置，再从后往前逐个替换（从后往前避免前面的替换改变后续匹配的字节偏移）。
static gchar *
regex_replace_range(const gchar *haystack, GRegex *re, const gchar *replace, gint from_idx, gint to_idx) {
    if (!haystack || from_idx < 0 || to_idx < from_idx) {
        return g_strdup(haystack);
    }
    // 收集所有匹配起始位置
    g_autoptr(GArray) starts = g_array_new(FALSE, FALSE, sizeof(gint));
    GMatchInfo *info = NULL;
    g_regex_match(re, haystack, 0, &info);
    while (g_match_info_matches(info)) {
        gint start = 0;
        g_match_info_fetch_pos(info, 0, &start, NULL);
        g_array_append_val(starts, start);
        g_match_info_next(info, NULL);
    }
    g_match_info_free(info);

    if (starts->len == 0) {
        return g_strdup(haystack);
    }

    // 从后往前替换 [from_idx, to_idx] 内的匹配（逆序保证前面匹配的位置不变）
    gchar *result = g_strdup(haystack);
    const gint hi = MIN(to_idx, (gint)starts->len - 1);
    for (gint i = hi; i >= from_idx; i--) {
        const gint start = g_array_index(starts, gint, i);
        gchar *tmp = g_regex_replace(re, result, -1, start, replace, 0, NULL);
        if (tmp) {
            g_free(result);
            result = tmp;
        }
    }
    return result;
}

// 统一的“查找替换”入口：mode 为 FIND_OCCUR_*，nth/nth2 为 1 基的序号（RANGE 模式用 nth..nth2）。
static gchar *
find_replace_apply(const gchar *name,
                   const gchar *find,
                   const gchar *replace,
                   gboolean use_regex,
                   gint mode,
                   gint nth,
                   gint nth2) {
    if (!name || !find || !*find) {
        return g_strdup(name);
    }
    if (mode == FIND_OCCUR_ALL) {
        if (use_regex) {
            GRegex *re = g_regex_new(find, G_REGEX_MULTILINE, 0, NULL);
            if (!re) {
                return g_strdup(name);
            }
            gchar *res = g_regex_replace(re, name, -1, 0, replace, 0, NULL);
            g_regex_unref(re);
            return res ? res : g_strdup(name);
        }
        return string_replace_all(name, find, replace);
    }

    if (mode == FIND_OCCUR_RANGE) {
        gint from = MAX(nth, 1) - 1;
        gint to = MAX(nth2, 1) - 1;
        if (to < from) {
            gint t = from;
            from = to;
            to = t;
        }
        if (use_regex) {
            GRegex *re = g_regex_new(find, G_REGEX_MULTILINE, 0, NULL);
            if (!re) {
                return g_strdup(name);
            }
            gchar *res = regex_replace_range(name, re, replace, from, to);
            g_regex_unref(re);
            return res;
        }
        return string_replace_range(name, find, replace, from, to);
    }

    // 仅第 N 个 / 仅最后一个
    gint target = -1; // 0 基
    if (mode == FIND_OCCUR_NTH) {
        target = MAX(nth, 1) - 1;
    }
    else { // FIND_OCCUR_LAST
        if (use_regex) {
            GRegex *re = g_regex_new(find, G_REGEX_MULTILINE, 0, NULL);
            if (!re) {
                return g_strdup(name);
            }
            // 数一下匹配总数
            GMatchInfo *info = NULL;
            g_regex_match(re, name, 0, &info);
            gint count = 0;
            while (g_match_info_matches(info)) {
                count++;
                g_match_info_next(info, NULL);
            }
            g_match_info_free(info);
            if (count == 0) {
                g_regex_unref(re);
                return g_strdup(name);
            }
            gchar *res = regex_replace_nth(name, re, replace, count - 1);
            g_regex_unref(re);
            return res;
        }
        // 普通文本：数匹配次数
        gint count = 0;
        const gchar *p = name;
        gsize nlen = strlen(find);
        while ((p = strstr(p, find)) != NULL) {
            count++;
            p += nlen;
        }
        if (count == 0) {
            return g_strdup(name);
        }
        return string_replace_nth(name, find, replace, count - 1);
    }

    if (use_regex) {
        GRegex *re = g_regex_new(find, G_REGEX_MULTILINE, 0, NULL);
        if (!re) {
            return g_strdup(name);
        }
        gchar *res = regex_replace_nth(name, re, replace, target);
        g_regex_unref(re);
        return res;
    }
    return string_replace_nth(name, find, replace, target);
}

// 从第 start 个字符（1 基、UTF-8 字符）开始删除 count 个字符
static gchar *
string_remove_chars(const gchar *str, gint start, gint count) {
    if (!str) {
        return NULL;
    }
    if (count <= 0 || start <= 0) {
        return g_strdup(str);
    }
    const glong len = g_utf8_strlen(str, -1);
    if (start > len) {
        return g_strdup(str);
    }
    const glong end = MIN((glong)start + count - 1, len);
    const gchar *head_end = g_utf8_offset_to_pointer(str, start - 1);
    const gchar *tail_start = g_utf8_offset_to_pointer(str, end);
    GString *out = g_string_sized_new(strlen(str) + 1);
    g_string_append_len(out, str, head_end - str);
    g_string_append(out, tail_start);
    return g_string_free(out, FALSE);
}

// 首字母大写（仅第一个 UTF-8 字符）
static gchar *
string_capitalize(const gchar *str) {
    if (!str || !*str) {
        return g_strdup(str ? str : "");
    }
    const gchar *first = g_utf8_next_char(str);
    g_autofree gchar *head_dup = g_strndup(str, first - str);
    gchar *head = g_utf8_strup(head_dup, -1);
    gchar *tail = g_strdup(first);
    gchar *res = g_strconcat(head, tail, NULL);
    g_free(head);
    g_free(tail);
    return res;
}

// 每个单词首字母大写（按空白分隔）
static gchar *
string_capitalize_words(const gchar *str) {
    if (!str) {
        return NULL;
    }
    GString *out = g_string_sized_new(strlen(str) + 1);
    const gchar *p = str;
    gboolean prev_space = TRUE;
    while (*p) {
        const gchar *next = g_utf8_next_char(p);
        const gunichar ch = g_utf8_get_char(p);
        if (g_unichar_isspace(ch)) {
            g_string_append_len(out, p, next - p);
            prev_space = TRUE;
        }
        else {
            if (prev_space) {
                g_autofree gchar *word = g_strndup(p, next - p);
                gchar *tmp = g_utf8_strup(word, -1);
                g_string_append(out, tmp);
                g_free(tmp);
            }
            else {
                g_string_append_len(out, p, next - p);
            }
            prev_space = FALSE;
        }
        p = next;
    }
    return g_string_free(out, FALSE);
}

// 拆分文件名主体与扩展名（隐藏文件 .foo 无扩展名）
static void
split_name(const gchar *name, gchar **stem, gchar **ext) {
    const gchar *dot = g_utf8_strrchr(name, -1, '.');
    if (dot && dot != name && g_utf8_next_char(dot)[0] != '\0') {
        *stem = g_strndup(name, dot - name);
        *ext = g_strdup(dot);
    }
    else {
        *stem = g_strdup(name);
        *ext = g_strdup("");
    }
}

// 序号字符串（补零）
static gchar *
make_sequence_number(gint value, gint digits) {
    if (digits <= 0) {
        digits = 1;
    }
    return g_strdup_printf("%0*d", digits, value);
}

// ---------- 规则应用 ----------

static gchar *
apply_rules(BatchRenameData *d, const gchar *old_name, guint seq) {
    gchar *name = g_strdup(old_name ? old_name : "");

    // 1. 查找替换（整个文件名，支持正则、支持只替换第 N 个/第 X 至第 Y 个/最后一个匹配）
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_find))) {
        const gchar *find = gtk_entry_get_text(GTK_ENTRY(d->find_entry));
        if (find && *find) {
            const gchar *replace = gtk_entry_get_text(GTK_ENTRY(d->replace_entry));
            gboolean use_regex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->regex_check));
            gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(d->find_occur_combo));
            gint nth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->find_nth_spin));
            gint nth2 = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->find_nth2_spin));
            gchar *new_name = find_replace_apply(name, find, replace, use_regex, mode, nth, nth2);
            if (new_name) {
                g_free(name);
                name = new_name;
            }
        }
    }

    // 2. 删除字符
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_remove))) {
        gint start = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->remove_start));
        gint count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->remove_count));
        gchar *tmp = string_remove_chars(name, start, count);
        g_free(name);
        name = tmp;
    }

    // 3. 拆分主体/扩展名
    gchar *stem = NULL;
    gchar *ext = NULL;
    split_name(name, &stem, &ext);
    g_free(name);

    // 4. 大小写（仅主体）
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_case))) {
        const gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(d->case_combo));
        gchar *tmp = NULL;
        switch (mode) {
        case CASE_UPPER:
            tmp = g_utf8_strup(stem, -1);
            break;
        case CASE_LOWER:
            tmp = g_utf8_strdown(stem, -1);
            break;
        case CASE_CAPITALIZE:
            tmp = string_capitalize(stem);
            break;
        case CASE_CAPITALIZE_WORDS:
            tmp = string_capitalize_words(stem);
            break;
        default:
            tmp = g_strdup(stem);
            break;
        }
        g_free(stem);
        stem = tmp;
    }

    // 5. 序号
    gchar *seq_str = NULL;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_numbering))) {
        gint start = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->num_start));
        gint step = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->num_step));
        gint digits = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->num_digits));
        gint value = start + (gint)seq * step;
        seq_str = make_sequence_number(value, digits);
    }

    // 6. 前缀 + 序号(前缀位置)
    gchar *prefix = NULL;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_prefix))) {
        prefix = g_strdup(gtk_entry_get_text(GTK_ENTRY(d->prefix_entry)));
    }
    if (seq_str && gtk_combo_box_get_active(GTK_COMBO_BOX(d->num_pos)) == NUMBER_POS_PREFIX) {
        gchar *tmp = g_strconcat(seq_str, prefix ? prefix : "", stem, NULL);
        g_free(stem);
        stem = tmp;
        g_free(seq_str);
        seq_str = NULL;
    }
    else if (prefix && *prefix) {
        gchar *tmp = g_strconcat(prefix, stem, NULL);
        g_free(stem);
        stem = tmp;
    }
    g_clear_pointer(&prefix, g_free);

    // 7. 后缀 + 序号(后缀位置)
    gchar *suffix = NULL;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_suffix))) {
        suffix = g_strdup(gtk_entry_get_text(GTK_ENTRY(d->suffix_entry)));
    }
    if (seq_str) {
        gchar *tmp = g_strconcat(stem, seq_str, suffix ? suffix : "", NULL);
        g_free(stem);
        stem = tmp;
        g_free(seq_str);
        seq_str = NULL;
    }
    else if (suffix && *suffix) {
        gchar *tmp = g_strconcat(stem, suffix, NULL);
        g_free(stem);
        stem = tmp;
    }
    g_clear_pointer(&suffix, g_free);

    // 8. 扩展名：删除 / 替换 / 保留
    gchar *final_ext = g_strdup(ext);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->enable_ext))) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->delete_ext_check))) {
            g_clear_pointer(&final_ext, g_free);
            final_ext = g_strdup("");
        }
        else {
            const gchar *new_ext = gtk_entry_get_text(GTK_ENTRY(d->ext_entry));
            if (new_ext && *new_ext) {
                g_clear_pointer(&final_ext, g_free);
                if (new_ext[0] == '.') {
                    final_ext = g_strdup(new_ext);
                }
                else {
                    final_ext = g_strconcat(".", new_ext, NULL);
                }
            }
        }
    }

    gchar *result = g_strconcat(stem, final_ext, NULL);
    g_free(stem);
    g_free(ext);
    g_free(final_ext);
    return result;
}

// ---------- 预览 ----------

static void
update_preview(BatchRenameData *d) {
    gtk_list_store_clear(d->store);

    // 先算所有新名与新路径
    for (guint i = 0; i < d->n; i++) {
        g_clear_pointer(&d->new_names[i], g_free);
        g_clear_pointer(&d->new_paths[i], g_free);
        d->new_names[i] = apply_rules(d, d->old_names[i], i);
        gchar *dir = g_path_get_dirname(d->old_paths[i]);
        d->new_paths[i] = g_build_filename(dir, d->new_names[i], NULL);
        g_free(dir);
    }

    // 判定状态
    guint num_conflicts = 0;
    for (guint i = 0; i < d->n; i++) {
        d->status[i] = STATUS_READY;
        if (g_strcmp0(d->old_names[i], d->new_names[i]) == 0) {
            d->status[i] = STATUS_NO_CHANGE;
        }
        else if (!d->new_names[i] || !*d->new_names[i] || strchr(d->new_names[i], '/')
                 || g_strcmp0(d->new_names[i], ".") == 0 || g_strcmp0(d->new_names[i], "..") == 0) {
            d->status[i] = STATUS_CONFLICT; // 非法名
        }
        else if (g_file_test(d->new_paths[i], G_FILE_TEST_EXISTS)) {
            d->status[i] = STATUS_CONFLICT; // 目标已存在
        }
        else {
            // 列表内重名检测（同一目录下的新路径是否重复）
            for (guint j = 0; j < d->n; j++) {
                if (j == i) {
                    continue;
                }
                if (g_strcmp0(d->new_paths[i], d->new_paths[j]) == 0) {
                    d->status[i] = STATUS_CONFLICT;
                    break;
                }
            }
        }
        if (d->status[i] == STATUS_CONFLICT) {
            num_conflicts++;
        }
    }

    // 填表
    for (guint i = 0; i < d->n; i++) {
        const gchar *status_text = NULL;
        const gchar *color = NULL;
        switch (d->status[i]) {
        case STATUS_NO_CHANGE:
            status_text = _("未变化");
            color = "#808080";
            break;
        case STATUS_CONFLICT:
            status_text = _("冲突");
            color = "#e01b24";
            break;
        default:
            status_text = _("就绪");
            color = "#26a269";
            break;
        }

        GtkTreeIter iter;
        gtk_list_store_append(d->store, &iter);
        gtk_list_store_set(d->store,
                           &iter,
                           COL_OLD_NAME,
                           d->old_names[i],
                           COL_NEW_NAME,
                           d->new_names[i],
                           COL_STATUS,
                           status_text,
                           COL_STATUS_COLOR,
                           color,
                           -1);
    }

    gchar *label = NULL;
    if (num_conflicts) {
        label = g_strdup_printf(_("共 %u 项，%u 项冲突"), d->n, num_conflicts);
    }
    else {
        label = g_strdup_printf(_("共 %u 项，全部可重命名"), d->n);
    }
    gtk_label_set_text(GTK_LABEL(d->status_label), label);
    g_free(label);
}

// ---------- 执行重命名 ----------

static void
do_rename(BatchRenameData *d) {
    guint ok = 0;
    guint failed = 0;
    guint skipped = 0;

    // 撤销历史只保留“最近一次执行”，新的执行先清掉旧的
    g_list_free_full(g_steal_pointer(&d->undo_old), g_free);
    g_list_free_full(g_steal_pointer(&d->undo_new), g_free);

    g_autoptr(GString) errors = g_string_sized_new(256);
    g_autoptr(DynamicArray) removed_paths = darray_new_full(d->n, g_free);

    for (guint i = 0; i < d->n; i++) {
        if (d->status[i] != STATUS_READY) {
            skipped++;
            continue;
        }
        if (g_rename(d->old_paths[i], d->new_paths[i]) == 0) {
            ok++;
            darray_add_item(removed_paths, g_strdup(d->old_paths[i]));
            // 记录撤销历史：旧路径 -> 新路径
            d->undo_old = g_list_prepend(d->undo_old, g_strdup(d->old_paths[i]));
            d->undo_new = g_list_prepend(d->undo_new, g_strdup(d->new_paths[i]));
            // 更新“旧路径/旧名”为当前实际值，便于再次执行时基于最新状态（否则第二次执行会拿已不存在的旧名去 rename）
            g_free(d->old_paths[i]);
            d->old_paths[i] = g_strdup(d->new_paths[i]);
            g_free(d->old_names[i]);
            d->old_names[i] = g_strdup(d->new_names[i]);
        }
        else {
            failed++;
            g_string_append_printf(errors, "%s: %s\n", d->old_names[i], g_strerror(errno));
        }
    }

    // 通知数据库移除旧路径条目（新路径由文件监控自动收录）
    if (ok > 0) {
        g_autoptr(FsearchDatabase) db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
        g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_notify_items_removed(removed_paths);
        fsearch_database_queue_work(db, work);
    }

    g_autoptr(GString) summary = g_string_new(NULL);
    g_string_printf(summary, _("重命名完成：%u 个成功"), ok);
    if (failed) {
        g_string_append_printf(summary, _("，%u 个失败"), failed);
    }
    if (skipped) {
        g_string_append_printf(summary, _("，%u 个跳过"), skipped);
    }

    if (failed > 0) {
        g_string_append_printf(summary, "\n\n%s", errors->str);
    }

    ui_utils_run_gtk_dialog(GTK_WIDGET(d->win),
                            failed ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                            GTK_BUTTONS_OK,
                            _("批量重命名"),
                            summary->str);

    // 有成功项才允许撤销；且撤销历史是“最近一次执行”的，新的执行会清掉旧历史
    if (ok > 0) {
        gtk_widget_set_sensitive(d->btn_undo, TRUE);
        gtk_widget_set_sensitive(d->btn_rename, TRUE);
        // 刷新预览：old_* 已更新为当前实际文件名，按当前规则重新计算新名
        update_preview(d);
    }
    // 不关闭对话框：让用户看预览结果，可继续调整规则、再次执行或撤销
}

// 撤销上一次批量重命名：把 new 路径 rename 回 old 路径，并通知数据库移除新路径条目。
static void
do_undo(BatchRenameData *d) {
    guint ok = 0;
    guint failed = 0;
    g_autoptr(GString) errors = g_string_sized_new(256);
    g_autoptr(DynamicArray) removed_paths = darray_new_full(g_list_length(d->undo_old), g_free);

    GList *lo = d->undo_old;
    GList *ln = d->undo_new;
    for (; lo && ln; lo = lo->next, ln = ln->next) {
        const char *old_path = lo->data;
        const char *new_path = ln->data;
        if (g_rename(new_path, old_path) == 0) {
            ok++;
            darray_add_item(removed_paths, g_strdup(new_path));
        }
        else {
            failed++;
            g_string_append_printf(errors, "%s: %s\n", old_path, g_strerror(errno));
        }
    }

    if (ok > 0) {
        g_autoptr(FsearchDatabase) db = fsearch_application_get_db(FSEARCH_APPLICATION_DEFAULT);
        g_autoptr(FsearchDatabaseWork) work = fsearch_database_work_new_notify_items_removed(removed_paths);
        fsearch_database_queue_work(db, work);
    }

    g_autoptr(GString) summary = g_string_new(NULL);
    g_string_printf(summary, _("撤销完成：%u 个已恢复"), ok);
    if (failed) {
        g_string_append_printf(summary, _("，%u 个失败"), failed);
    }
    if (failed > 0) {
        g_string_append_printf(summary, "\n\n%s", errors->str);
    }

    ui_utils_run_gtk_dialog(GTK_WIDGET(d->win),
                            failed ? GTK_MESSAGE_WARNING : GTK_MESSAGE_INFO,
                            GTK_BUTTONS_OK,
                            _("撤销批量重命名"),
                            summary->str);

    // 清空撤销历史，恢复按钮状态，刷新预览（新名列表重新计算）
    g_list_free_full(g_steal_pointer(&d->undo_old), g_free);
    g_list_free_full(g_steal_pointer(&d->undo_new), g_free);
    gtk_widget_set_sensitive(d->btn_undo, FALSE);
    gtk_widget_set_sensitive(d->btn_rename, TRUE);
    update_preview(d);
}

// ---------- 信号 ----------

static void
on_rule_changed(GtkWidget *widget, gpointer user_data) {
    BatchRenameData *d = user_data;
    (void)widget;
    update_preview(d);
}

static void
on_enable_toggled(GtkToggleButton *btn, gpointer user_data) {
    BatchRenameData *d = user_data;
    (void)btn;
    update_preview(d);
}

static void
on_rename_clicked(GtkButton *btn, gpointer user_data) {
    BatchRenameData *d = user_data;
    (void)btn;
    do_rename(d);
}

static void
on_undo_clicked(GtkButton *btn, gpointer user_data) {
    BatchRenameData *d = user_data;
    (void)btn;
    do_undo(d);
}

// 释放数据
static void
batch_rename_data_free(BatchRenameData *d) {
    if (!d) {
        return;
    }
    for (guint i = 0; i < d->n; i++) {
        g_free(d->old_names[i]);
        g_free(d->old_paths[i]);
        g_free(d->new_names[i]);
        g_free(d->new_paths[i]);
    }
    g_free(d->old_names);
    g_free(d->old_paths);
    g_free(d->new_names);
    g_free(d->new_paths);
    g_free(d->status);
    g_list_free_full(d->undo_old, g_free);
    g_list_free_full(d->undo_new, g_free);
    g_free(d);
}

// ---------- UI 构建 ----------

static GtkWidget *
make_checkbox(const gchar *label, gboolean active) {
    GtkWidget *cb = gtk_check_button_new_with_label(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), active);
    return cb;
}

static GtkWidget *
make_entry(const gchar *placeholder, gint width) {
    GtkWidget *entry = gtk_entry_new();
    if (placeholder) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    }
    gtk_entry_set_width_chars(GTK_ENTRY(entry), width);
    return entry;
}

static GtkWidget *
make_spin(gint min, gint max, gint value) {
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_size_request(spin, 64, -1);
    return spin;
}

static void
connect_all_changed(BatchRenameData *d, GtkWidget *box) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *w = l->data;
        if (GTK_IS_SPIN_BUTTON(w)) {
            g_signal_connect(w, "value-changed", G_CALLBACK(on_rule_changed), d);
        }
        else if (GTK_IS_ENTRY(w) || GTK_IS_COMBO_BOX(w)) {
            g_signal_connect(w, "changed", G_CALLBACK(on_rule_changed), d);
        }
        else if (GTK_IS_TOGGLE_BUTTON(w)) {
            g_signal_connect(w, "toggled", G_CALLBACK(on_enable_toggled), d);
        }
        if (GTK_IS_CONTAINER(w)) {
            connect_all_changed(d, w);
        }
    }
    g_list_free(children);
}

// 收集选中文件完整路径
static void
collect_selected_path(FsearchDatabaseEntry *entry, gpointer user_data) {
    GPtrArray *arr = user_data;
    GString *path = db_entry_get_path_full(entry);
    if (path) {
        g_ptr_array_add(arr, g_string_free(path, FALSE));
    }
}

void
fsearch_batch_rename_dialog_new(FsearchApplicationWindow *win) {
    g_return_if_fail(win);

    // 收集选中文件路径
    g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func(g_free);
    fsearch_application_window_selection_for_each(win, collect_selected_path, paths);

    const guint n = paths->len;
    if (n == 0) {
        return;
    }

    BatchRenameData *d = g_new0(BatchRenameData, 1);
    d->win = win;
    d->n = n;
    d->old_names = g_new0(gchar *, n);
    d->old_paths = g_new0(gchar *, n);
    d->new_names = g_new0(gchar *, n);
    d->new_paths = g_new0(gchar *, n);
    d->status = g_new0(gint, n);
    for (guint i = 0; i < n; i++) {
        d->old_paths[i] = g_strdup(g_ptr_array_index(paths, i));
        d->old_names[i] = g_path_get_basename(d->old_paths[i]);
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(_("批量重命名"),
                                                    GTK_WINDOW(win),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    _("取消"),
                                                    GTK_RESPONSE_CANCEL,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 780, 620);
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "io.github.cboxdoerfer.FSearch");

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    // ---- 规则区 ----
    GtkWidget *rules_frame = gtk_frame_new(_("重命名规则"));
    GtkWidget *rules_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(rules_grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(rules_grid), 6);
    gtk_container_add(GTK_CONTAINER(rules_frame), rules_grid);
    gtk_box_pack_start(GTK_BOX(vbox), rules_frame, FALSE, FALSE, 0);

    gint row = 0;

    // 查找替换
    d->enable_find = make_checkbox(_("查找替换"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_find, 0, row, 1, 1);
    d->find_entry = make_entry(_("查找"), 18);
    gtk_grid_attach(GTK_GRID(rules_grid), d->find_entry, 1, row, 1, 1);
    d->replace_entry = make_entry(_("替换为"), 18);
    gtk_grid_attach(GTK_GRID(rules_grid), d->replace_entry, 2, row, 1, 1);
    d->regex_check = gtk_check_button_new_with_label(_("正则"));
    gtk_grid_attach(GTK_GRID(rules_grid), d->regex_check, 3, row, 1, 1);
    // 出现次数：全部 / 仅第 N 个 / 第 X 至第 Y 个 / 仅最后一个
    d->find_occur_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->find_occur_combo), NULL, _("全部"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->find_occur_combo), NULL, _("仅第"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->find_occur_combo), NULL, _("第…至第…"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->find_occur_combo), NULL, _("仅最后一个"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->find_occur_combo), FIND_OCCUR_ALL);
    gtk_grid_attach(GTK_GRID(rules_grid), d->find_occur_combo, 4, row, 1, 1);
    d->find_nth_spin = make_spin(1, 1000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->find_nth_spin, 5, row, 1, 1);
    d->find_nth2_spin = make_spin(1, 1000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->find_nth2_spin, 6, row, 1, 1);
    GtkWidget *lbl_nth_suffix = gtk_label_new(_("个匹配"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_nth_suffix, 7, row, 1, 1);
    row++;

    // 前缀
    d->enable_prefix = make_checkbox(_("添加前缀"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_prefix, 0, row, 1, 1);
    d->prefix_entry = make_entry(_("前缀文本"), 18);
    gtk_grid_attach(GTK_GRID(rules_grid), d->prefix_entry, 1, row, 3, 1);
    row++;

    // 后缀
    d->enable_suffix = make_checkbox(_("添加后缀"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_suffix, 0, row, 1, 1);
    d->suffix_entry = make_entry(_("后缀文本"), 18);
    gtk_grid_attach(GTK_GRID(rules_grid), d->suffix_entry, 1, row, 3, 1);
    row++;

    // 序号
    d->enable_numbering = make_checkbox(_("添加序号"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_numbering, 0, row, 1, 1);
    GtkWidget *lbl_start = gtk_label_new(_("起始"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_start, 1, row, 1, 1);
    d->num_start = make_spin(0, 100000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->num_start, 2, row, 1, 1);
    GtkWidget *lbl_step = gtk_label_new(_("步长"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_step, 3, row, 1, 1);
    d->num_step = make_spin(1, 100000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->num_step, 4, row, 1, 1);
    GtkWidget *lbl_digits = gtk_label_new(_("位数"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_digits, 5, row, 1, 1);
    d->num_digits = make_spin(0, 20, 2);
    gtk_grid_attach(GTK_GRID(rules_grid), d->num_digits, 6, row, 1, 1);
    GtkWidget *lbl_pos = gtk_label_new(_("位置"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_pos, 7, row, 1, 1);
    d->num_pos = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->num_pos), NULL, _("前缀"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->num_pos), NULL, _("后缀"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->num_pos), NUMBER_POS_PREFIX);
    gtk_grid_attach(GTK_GRID(rules_grid), d->num_pos, 8, row, 1, 1);
    row++;

    // 大小写
    d->enable_case = make_checkbox(_("大小写"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_case, 0, row, 1, 1);
    d->case_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->case_combo), NULL, _("全大写"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->case_combo), NULL, _("全小写"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->case_combo), NULL, _("首字母大写"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->case_combo), NULL, _("每个单词首字母大写"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->case_combo), CASE_UPPER);
    gtk_grid_attach(GTK_GRID(rules_grid), d->case_combo, 1, row, 3, 1);
    row++;

    // 扩展名
    d->enable_ext = make_checkbox(_("修改扩展名"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_ext, 0, row, 1, 1);
    d->ext_entry = make_entry(_("新扩展名"), 18);
    gtk_grid_attach(GTK_GRID(rules_grid), d->ext_entry, 1, row, 1, 1);
    d->delete_ext_check = gtk_check_button_new_with_label(_("删除扩展名"));
    gtk_grid_attach(GTK_GRID(rules_grid), d->delete_ext_check, 2, row, 2, 1);
    row++;

    // 删除字符
    d->enable_remove = make_checkbox(_("删除字符"), FALSE);
    gtk_grid_attach(GTK_GRID(rules_grid), d->enable_remove, 0, row, 1, 1);
    GtkWidget *lbl_rm_start = gtk_label_new(_("从第"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_rm_start, 1, row, 1, 1);
    d->remove_start = make_spin(1, 1000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->remove_start, 2, row, 1, 1);
    GtkWidget *lbl_rm_count = gtk_label_new(_("个字符起删除"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_rm_count, 3, row, 1, 1);
    d->remove_count = make_spin(0, 1000, 1);
    gtk_grid_attach(GTK_GRID(rules_grid), d->remove_count, 4, row, 1, 1);
    GtkWidget *lbl_rm_end = gtk_label_new(_("个字符"));
    gtk_grid_attach(GTK_GRID(rules_grid), lbl_rm_end, 5, row, 1, 1);
    row++;

    // ---- 预览区 ----
    GtkWidget *preview_frame = gtk_frame_new(_("预览"));
    gtk_box_pack_start(GTK_BOX(vbox), preview_frame, TRUE, TRUE, 0);

    GtkWidget *pvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(preview_frame), pvbox);

    d->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(d->status_label), 0);
    gtk_box_pack_start(GTK_BOX(pvbox), d->status_label, FALSE, FALSE, 0);

    d->store = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    d->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->tree), TRUE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(d->tree), FALSE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    col = gtk_tree_view_column_new_with_attributes(_("原名称"), renderer, "text", COL_OLD_NAME, NULL);
    gtk_tree_view_column_set_expand(GTK_TREE_VIEW_COLUMN(col), TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), col);

    col = gtk_tree_view_column_new_with_attributes(_("新名称"), renderer, "text", COL_NEW_NAME, NULL);
    gtk_tree_view_column_set_expand(GTK_TREE_VIEW_COLUMN(col), TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), col);

    col = gtk_tree_view_column_new_with_attributes(_("状态"),
                                                    renderer,
                                                    "text",
                                                    COL_STATUS,
                                                    "foreground",
                                                    COL_STATUS_COLOR,
                                                    NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), col);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), d->tree);
    gtk_box_pack_start(GTK_BOX(pvbox), scroll, TRUE, TRUE, 0);

    // ---- 底部按钮（撤销 + 执行重命名） ----
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(vbox), btn_box, FALSE, FALSE, 0);

    d->btn_rename = gtk_button_new_with_label(_("执行重命名"));
    gtk_box_pack_end(GTK_BOX(btn_box), d->btn_rename, FALSE, FALSE, 0);

    d->btn_undo = gtk_button_new_with_label(_("撤销"));
    gtk_widget_set_sensitive(d->btn_undo, FALSE); // 初始无历史，禁用
    gtk_box_pack_end(GTK_BOX(btn_box), d->btn_undo, FALSE, FALSE, 0);

    // ---- 连接信号 ----
    connect_all_changed(d, rules_grid);
    g_signal_connect(d->btn_rename, "clicked", G_CALLBACK(on_rename_clicked), d);
    g_signal_connect(d->btn_undo, "clicked", G_CALLBACK(on_undo_clicked), d);
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    g_object_set_data_full(G_OBJECT(dialog), "batch-rename-data", d, (GDestroyNotify)batch_rename_data_free);

    update_preview(d);
    gtk_widget_show_all(dialog);
}
