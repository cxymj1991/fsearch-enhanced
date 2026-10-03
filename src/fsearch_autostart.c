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

#include "fsearch_autostart.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <limits.h>
// GLib 2.58 兼容垫片：本文件的 run_cmd() 用 g_spawn_check_wait_status()（GLib 2.70 引入），
// compat.h 把它映射到 2.58 上等价的 g_spawn_check_exit_status()。
#include "fsearch_compat.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// 自启动相关的两个文件名
#define FSEARCH_AUTOSTART_DESKTOP_NAME "io.github.cboxdoerfer.FSearch.desktop"
#define FSEARCH_SYSTEMD_UNIT_NAME "fsearch.service"

/*
 * 【问题 1 的根因与对策】
 *
 * 现象：勾选"开机自动启动"后，UOS 每次开机仍弹「是否允许 fsearch 开机启动」确认框，
 *       必须手动点"允许"才真正自启。
 *
 * 原因：原实现把 .desktop 直接写进 XDG 用户自启动目录 ~/.config/autostart/。
 *       统信 UOS 桌面对**用户级 XDG autostart 目录中新出现的、未被系统应用数据库
 *       登记的条目**会做一次性授权确认（点"允许"后条目才被允许执行）。
 *       原实现还有两个加剧因素：
 *         - 写的是 fsearch.desktop，与已安装桌面项的 desktop file ID
 *           （io.github.cboxdoerfer.FSearch.desktop）不一致，系统的应用管理器
 *           无法把它与已登记应用对应起来 → 更容易被判定为"未知第三方程序"；
 *         - 未显式写 Hidden=false。DDE 系列组件以 Hidden 字段作为自启动开关判据，
 *           缺省值行为不确定。
 *
 * 对策（本实现按可靠性排序，实际只让其中一条真正生效）：
 *   ① 首选：改用 systemd --user 用户服务（~/.config/systemd/user/fsearch.service）。
 *      systemd 的 user manager 直接按 unit 文件拉起，**不经过 XDG autostart 目录**，
 *      因此不触发 UOS 的自启动授权弹窗。这是最彻底的绕开方式。
 *   ② 兜底：仅当 systemd --user 不可用（enable 失败）时，才退回写 XDG autostart；
 *      此时使用与桌面项一致的 desktop file ID 并显式写 Hidden=false。
 *
 * 注：两条机制即便同时生效也不会起两份进程——FSearch 是 GApplication 单实例应用，
 *     重复启动只会通过 D-Bus 激活已有实例后立即退出。
 */

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

// 同步执行一条命令并返回退出码（0 成功，-1 无法执行，非 0 为命令失败）。
static int
run_cmd(const char *cmd) {
    g_autofree char *stderr_out = NULL;
    int status = 0;
    GError *error = NULL;
    // 显式补上 XDG_RUNTIME_DIR，否则从 GUI 调 systemctl --user 可能找不到 user bus
    g_autofree char *full = g_strdup_printf("XDG_RUNTIME_DIR=/run/user/%u %s", (unsigned)getuid(), cmd);
    if (!g_spawn_command_line_sync(full, NULL, &stderr_out, &status, &error)) {
        g_debug("[autostart] 命令执行失败: %s (%s)", cmd, error ? error->message : "未知错误");
        g_clear_error(&error);
        return -1;
    }
    if (stderr_out && *stderr_out) {
        g_debug("[autostart] %s 输出: %s", cmd, stderr_out);
    }
    return g_spawn_check_wait_status(status, NULL) ? 0 : 1;
}

// 写 XDG 用户自启动项（兜底方案）。文件名与已安装桌面项的 desktop file ID 保持一致，
// 并显式写 Hidden=false —— DDE 的自启动管理以该字段作为开关判据。
static void
write_xdg_autostart(const char *exe) {
    g_autofree char *autostart_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_autofree char *path = g_build_filename(autostart_dir, FSEARCH_AUTOSTART_DESKTOP_NAME, NULL);

    if (g_mkdir_with_parents(autostart_dir, 0700) != 0) {
        g_warning("[autostart] 无法创建目录 %s", autostart_dir);
        return;
    }

    g_autofree char *content = g_strdup_printf("[Desktop Entry]\n"
                                               "Type=Application\n"
                                               "Version=1.0\n"
                                               "Name=FSearch\n"
                                               "GenericName=File Search\n"
                                               "Comment=Fast file search utility\n"
                                               "Exec=%s --hidden\n"
                                               "TryExec=%s\n"
                                               "Icon=io.github.cboxdoerfer.FSearch\n"
                                               "Terminal=false\n"
                                               "NoDisplay=true\n"
                                               "Hidden=false\n"
                                               "X-GNOME-Autostart-enabled=true\n"
                                               "X-GNOME-Autostart-Delay=3\n",
                                               exe,
                                               exe);

    GError *error = NULL;
    if (!g_file_set_contents(path, content, -1, &error)) {
        g_warning("[autostart] 写入 %s 失败: %s", path, error ? error->message : "未知错误");
        g_clear_error(&error);
        return;
    }
    chmod(path, 0644);
    g_debug("[autostart] 已写入 XDG 自启动项: %s", path);
}

static void
remove_xdg_autostart(void) {
    g_autofree char *autostart_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_autofree char *path = g_build_filename(autostart_dir, FSEARCH_AUTOSTART_DESKTOP_NAME, NULL);
    g_unlink(path);
    // 兼容旧版本写下的文件名，一并清掉，避免历史残留
    g_autofree char *legacy = g_build_filename(autostart_dir, "fsearch.desktop", NULL);
    g_unlink(legacy);
}

// 写 systemd --user 用户服务（首选方案，绕开 UOS 的自启动授权弹窗）。
// 返回 TRUE 表示 systemd 接管成功。
static gboolean
setup_systemd_user_service(const char *exe) {
    g_autofree char *unit_dir = g_build_filename(g_get_user_config_dir(), "systemd", "user", NULL);
    g_autofree char *unit_path = g_build_filename(unit_dir, FSEARCH_SYSTEMD_UNIT_NAME, NULL);

    if (g_mkdir_with_parents(unit_dir, 0700) != 0) {
        g_debug("[autostart] 无法创建 systemd user 单元目录 %s", unit_dir);
        return FALSE;
    }

    // DISPLAY / XAUTHORITY / DBUS 会话地址：systemd --user 拉起的进程默认继承不到
    // 图形会话环境，这里按当前会话显式补齐（UOS 20 为 X11 会话）。
    g_autofree char *display = g_strdup(g_getenv("DISPLAY"));
    if (!display || !*display) {
        g_clear_pointer(&display, g_free);
        display = g_strdup(":0");
    }
    g_autofree char *xauthority = g_strdup(g_getenv("XAUTHORITY"));
    if (!xauthority || !*xauthority) {
        g_clear_pointer(&xauthority, g_free);
        xauthority = g_build_filename(g_get_home_dir(), ".Xauthority", NULL);
    }

    // WantedBy=default.target：UOS 20 的 systemd 241 尚未提供可靠的
    // graphical-session.target（该 target 由 xdg-desktop-portal 的图形会话提供，
    // UOS 20 未预装），用 default.target 可确保用户登录后一定被拉起。
    //
    // 启动时机与健壮性（这几项直接决定"开机到底有没有自启成功"，务必保留）：
    //   - ExecStartPre 延迟 3 秒：登录瞬间 X/D-Bus/托盘往往还没就绪，立刻启动可能因
    //     连不上显示而退出。若不加 Restart，一次失败就会变成"开机没自启"，
    //     很容易被误判为"弹窗问题没解决"。X-GNOME-Autostart-Delay 同样只对 XDG 路径生效。
    //   - Restart=on-failure + RestartSec=5：只对**非零退出**重启，
    //     用户从托盘"退出 FSearch"是正常退出(0)，不会被立刻拉起来。
    //   - StartLimitIntervalSec/Burst：限制 10 秒内最多重启 3 次，
    //     避免配置错误时无限重启刷屏。
    g_autofree char *content = g_strdup_printf("[Unit]\n"
                                               "Description=FSearch file search (background indexer)\n"
                                               "After=default.target\n"
                                               "StartLimitIntervalSec=10\n"
                                               "StartLimitBurst=3\n"
                                               "\n"
                                               "[Service]\n"
                                               "Type=simple\n"
                                               "Environment=DISPLAY=%s\n"
                                               "Environment=XAUTHORITY=%s\n"
                                               "Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%u/bus\n"
                                               "Environment=GDK_BACKEND=x11\n"
                                               "ExecStartPre=/bin/sleep 3\n"
                                               "ExecStart=%s --hidden\n"
                                               "Restart=on-failure\n"
                                               "RestartSec=5\n"
                                               "\n"
                                               "[Install]\n"
                                               "WantedBy=default.target\n",
                                               display,
                                               xauthority,
                                               (unsigned)getuid(),
                                               exe);

    GError *error = NULL;
    if (!g_file_set_contents(unit_path, content, -1, &error)) {
        g_debug("[autostart] 写入 systemd 单元 %s 失败: %s", unit_path, error ? error->message : "未知错误");
        g_clear_error(&error);
        return FALSE;
    }
    chmod(unit_path, 0644);

    if (run_cmd("systemctl --user daemon-reload") != 0) {
        g_debug("[autostart] systemctl --user daemon-reload 失败");
        g_unlink(unit_path);
        return FALSE;
    }
    if (run_cmd("systemctl --user enable fsearch.service") != 0) {
        g_debug("[autostart] systemctl --user enable 失败");
        // 失败则清掉单元文件，避免留下一个永远不被执行的残file
        g_unlink(unit_path);
        run_cmd("systemctl --user daemon-reload");
        return FALSE;
    }
    g_debug("[autostart] systemd --user 服务已启用: %s", unit_path);
    return TRUE;
}

static void
teardown_systemd_user_service(void) {
    run_cmd("systemctl --user disable --now fsearch.service");
    g_autofree char *unit_dir = g_build_filename(g_get_user_config_dir(), "systemd", "user", NULL);
    g_autofree char *unit_path = g_build_filename(unit_dir, FSEARCH_SYSTEMD_UNIT_NAME, NULL);
    g_unlink(unit_path);
    run_cmd("systemctl --user daemon-reload");
    run_cmd("systemctl --user reset-failed fsearch.service");
}

void
fsearch_autostart_set_enabled(gboolean enabled) {
    if (!enabled) {
        teardown_systemd_user_service();
        remove_xdg_autostart();
        return;
    }

    g_autofree char *exe = get_exe_path();
    if (!exe) {
        g_warning("[autostart] 无法确定 fsearch 可执行文件路径，跳过自启动注册");
        return;
    }

    // 首选：systemd --user（不写 XDG autostart，从根上避免 UOS 的自启动授权弹窗）
    if (setup_systemd_user_service(exe)) {
        // 清掉可能存在的旧版 XDG 自启动项，保持干净（重复本身也因单实例而无害）
        remove_xdg_autostart();
        return;
    }

    // 兜底：systemd 不可用时退回 XDG autostart
    g_warning("[autostart] systemd --user 不可用，已退回 XDG autostart 方式；"
              "UOS 可能对新增自启动项弹出一次授权确认，点“允许”即可。");
    write_xdg_autostart(exe);
}
