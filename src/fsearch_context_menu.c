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

/*
 * 【关键：Exec 里的字段码必须用空格连接，且选 %u 而非 %f】
 *
 * 统信 UOS 20 的 dde-file-manager 5.x 通过 libqtxdg 解析 OEM 菜单的 Exec 行，
 * 其 expandExecString() 的行为是：
 *   1) 先用 parseCombinedArgString() 分词，**只在「引号外 + 空白字符」处切分**，
 *      `=` 不是分隔符；
 *   2) 字段码用**精确相等比较**（token == "%u"），不是子串替换；
 *   3) %f 与 %u 都能展开 urls.at(0)，真正的区别是**是否做 URL→本地路径转换**：
 *        - %f 原样输出，5.x 传入的是 file:// URL，所以拿到的还是 URI；
 *        - %u 会先 QUrl::toLocalFile()，直接给出本地路径。
 *      选 %u 是为了少一层转换、少一层风险。
 *      （注：UOS 20 具体打包的 libqtxdg 版本未确证，老版本上 %f 可能是"只丢弃不展开"，
 *        新版本才是"展开为原串"。无论哪种，用 %u 都是安全的那一个。）
 *   4) 根本没有 %p 分支 —— 用 %p 会被原样传出。
 *
 * 因此：
 *   - 写 `--search-in=%f` → 分词后是【一个】token "--search-in=%f"，
 *     不等于 "%f"，落入兜底分支被原样输出 → 程序收到字面量 `--search-in=%f`
 *     → 解析失败。**这是最常见的错误写法**（0.3.1 即如此，靠程序侧的位置参数
 *       兜底"碰巧能用"——点了总能开窗口，只是范围没限定）。
 *   - 写 `--search-in %u` → 语义正确，但**有硬伤**：若某 dfm 版本没展开 %u，
 *     argv 变成 ["fsearch","--search-in"]，GOption 对缺参选项直接报错 exit(1)，
 *     表现为"点菜单完全没反应"（0.3.2 首轮真机实测踩过）。
 *   - 写 `%u`（位置参数，不带选项）✅ → 本项目最终采用：
 *        · %u 正常展开 → argv=["fsearch","/路径"]，程序侧从位置参数取出并限定目录；
 *        · %u 未展开   → argv=["fsearch"]，退化为正常打开窗口。
 *      任何情况下点击菜单都至少能打开程序（恢复 0.3.1 的体验），绝无"没反应"。
 *
 * 另注：libqtxdg 根本没有 %p 分支，故"空白处"场景也不能用 %p。
 *
 * 故本文件统一使用 `Exec=… %u`（%u 作为唯一参数）。
 */


// 三个 OEM 项：真实文件夹 / 空白处 / 文件夹快捷方式
typedef enum {
    OEM_KIND_SINGLE_DIR,
    OEM_KIND_EMPTY_AREA,
    OEM_KIND_DESKTOP_SHORTCUT,
    NUM_OEM_KINDS,
} FsearchOemKind;

static const char *oem_filenames[NUM_OEM_KINDS] = {
    "fsearch-search.desktop",        // 右键真实文件夹
    "fsearch-search-blank.desktop",  // 右键文件夹空白处
    "fsearch-search-link.desktop",   // 右键文件夹快捷方式（.desktop）
};

// 取得"启动 fsearch 的入口"路径：
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
collect_oem_paths(FsearchOemKind kind) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    const char *fname = oem_filenames[kind];
    for (int i = 0; oem_dirs[i]; i++) {
        g_ptr_array_add(paths, g_build_filename(oem_dirs[i], fname, NULL));
    }
    // 用户级目录
    g_ptr_array_add(paths, g_build_filename(g_get_user_data_dir(), "deepin/dde-file-manager/oem-menuextensions", fname, NULL));
    return paths;
}

static void
remove_all_oem(void) {
    for (int k = 0; k < NUM_OEM_KINDS; k++) {
        g_autoptr(GPtrArray) paths = collect_oem_paths(k);
        for (guint i = 0; i < paths->len; i++) {
            g_unlink((const char *)g_ptr_array_index(paths, i));
        }
    }
}

// 生成某个 OEM 项的 .desktop 内容。三个场景只在 MimeType / Exec 字段码 / X-DFM-MenuTypes 上不同。
static char *
build_oem_content(FsearchOemKind kind, const char *exe) {
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
                           "Terminal=false\n");

    switch (kind) {
    case OEM_KIND_SINGLE_DIR:
        // 右键一个真实文件夹：%f 被替换成该文件夹的绝对路径
        g_string_append_printf(content,
                               "MimeType=inode/directory;\n"
                               "Exec=%s %%f\n"
                               "X-DFM-MenuTypes=SingleDir\n",
                               exe);
        break;
    case OEM_KIND_EMPTY_AREA:
        // 右键文件夹内的空白处。
        //
        // 5.x 移除了 %p 字段码（libqtxdg 的 expandExecString 里根本没有 %p 分支），
        // %d / %D / %n 传的都是空串，同样不可用。
        // 改用 %u：dfm 5.x 的 emptyAreaActoins()（dfmadditionalmenu.cpp）在 EmptyArea
        // 场景下用 action->setData(currentDir) 存 QString，而 QVariant::toStringList()
        // 支持 QString 存储类型 → urls 列表为 [当前目录]，
        // %u 取 urls.at(0) 正好就是当前目录，即我们需要的搜索根。
        g_string_append_printf(content,
                               "MimeType=inode/directory;\n"
                               "Exec=%s %%p\n"
                               "X-DFM-MenuTypes=EmptyArea\n",
                               exe);
        break;
    case OEM_KIND_DESKTOP_SHORTCUT:
        // 右键"文件夹快捷方式"（.desktop 文件）：传 %u（快捷方式文件自身的 URI），
        // 由 fsearch 解析其 URL= / Exec= 字段还原出所指向的源目录再搜索 ——
        // 字段码用 %f（0.3.1 实证可用的写法），作为位置参数传入。
        // 语义与"在源文件夹上右键后用 FSearch 搜索"一致。
        //
        // 匹配条件说明（deepin/dde-file-manager 源码 dfmadditionalmenu.cpp）：
        //   - MimeType=application/x-desktop; 由 MIME 数据库判定；
        //   - X-DFM-SupportSuffix=desktop; 是纯字符串后缀比较，不依赖 MIME 库，
        //     在 .desktop 未被正确注册 MIME 的系统上仍能命中（两者是 AND 关系，
        //     正常系统上 .desktop 必然被识别为 application/x-desktop，故同时写两者）。
        //   - X-DFM-NotShowIn=Desktop; 避免在桌面图标上重复出现（桌面由 dde-desktop 单独加载）。
        //   - X-DFM-SupportSchemes=file; 只对本地文件生效，远程/网络位置不显示。
        g_string_append_printf(content,
                               "MimeType=application/x-desktop;\n"
                               "X-DFM-SupportSuffix=desktop;\n"
                               "X-DFM-SupportSchemes=file;\n"
                               "X-DFM-NotShowIn=Desktop;\n"
                               "Exec=%s %%f\n"
                               "X-DFM-MenuTypes=SingleFile\n",
                               exe);
        break;
    default:
        break;
    }

    return g_string_free(content, FALSE);
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

    guint num_written = 0;
    for (int k = 0; k < NUM_OEM_KINDS; k++) {
        g_autofree char *content = build_oem_content(k, exe);
        g_autoptr(GPtrArray) paths = collect_oem_paths(k);
        for (guint i = 0; i < paths->len; i++) {
            const char *path = (const char *)g_ptr_array_index(paths, i);
            g_autofree char *dir = g_path_get_dirname(path);
            if (g_mkdir_with_parents(dir, 0755) != 0) {
                continue; // 无写入权限则尝试下一个位置
            }
            GError *error = NULL;
            if (g_file_set_contents(path, content, -1, &error)) {
                chmod(path, 0644);
                num_written++;
            }
            else {
                g_debug("[context-menu] 写入 %s 失败: %s", path, error ? error->message : "未知错误");
                g_clear_error(&error);
            }
        }
    }

    if (num_written == 0) {
        g_warning("[context-menu] 无法写入 OEM 菜单扩展文件（需要 %s 可写，或在安装时由 root 创建并归属当前用户）",
                  oem_dirs[0]);
    }
}
