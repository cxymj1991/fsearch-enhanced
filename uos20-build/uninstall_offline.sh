#!/usr/bin/env bash
#
# uninstall_offline.sh
# ---------------------------------------------------------------------------
# 卸载离线安装的 fsearch（与 install_offline.sh 对应）。
# 由于安装时从没走系统包管理器，这里只需删掉释放的文件即可，对系统零残留。
#
# 用法（需要 root，因为要删 /opt 和 /usr/local/bin 下的文件）：
#   sudo bash uninstall_offline.sh            交互式：卸载后会询问"是否删除用户配置与搜索数据库"
#   sudo bash uninstall_offline.sh --delete-config   非交互：卸载并删除用户配置与搜索数据库
#   sudo bash uninstall_offline.sh --purge            非交互：同上（--purge 为 --delete-config 的别名）
#
# 说明：普通运行即可在卸载后选择是否清除用户配置（~/.config/fsearch）与搜索数据库
#       （~/.cache/fsearch），无需再手动敲 --purge。
# ---------------------------------------------------------------------------
set -euo pipefail

PREFIX=/opt/fsearch

# 取真实用户（脚本以 sudo 运行，~ 此时是 /root，必须用真实用户名）
REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

# 停止正在运行的 fsearch。
# 必须做：fsearch 是 GApplication 单实例（通过 D-Bus 注册），若旧进程还活着：
#   - 卸载时文件被删但旧进程仍驻留内存；
#   - 之后重装新版本再运行，会直接 D-Bus 激活旧进程 → 打开后依然是旧版本。
echo "== 停止正在运行的 fsearch =="
pkill -f "$PREFIX" 2>/dev/null || true        # 命令行含 /opt/fsearch 的（启动器脚本 + 真二进制）
pkill -x fsearch-bin 2>/dev/null || true      # 兜底：按进程名匹配
sleep 1

# 是否删除用户配置 / 搜索数据库
DELETE_CONFIG="no"
case "${1:-}" in
    --purge|--delete-config)
        DELETE_CONFIG="yes"
        ;;
    "")
        printf '是否同时删除用户配置与搜索数据库（含索引，占用空间较大）？[y/N] '
        read -r ans || ans=""
        case "$ans" in
            y|Y|yes|YES) DELETE_CONFIG="yes" ;;
            *) DELETE_CONFIG="no" ;;
        esac
        ;;
    *)
        echo "未知参数: $1" >&2
        echo "用法: sudo bash uninstall_offline.sh [--delete-config|--purge]" >&2
        exit 1
        ;;
esac

echo "== 删除软件本体与命令软链 =="
rm -rf "$PREFIX"
rm -f /usr/local/bin/fsearch

echo "== 删除桌面入口（开始菜单 + 桌面图标）=="
rm -f "$REAL_HOME/.local/share/applications/io.github.cboxdoerfer.FSearch.desktop"
rm -f "$REAL_HOME/桌面/io.github.cboxdoerfer.FSearch.desktop" \
      "$REAL_HOME/Desktop/io.github.cboxdoerfer.FSearch.desktop"

echo "== 删除文件管理器右键菜单项（选中文件夹 SingleDir + 空白处 EmptyArea）=="
for f in fsearch-search.desktop fsearch-search-blank.desktop; do
    rm -f "/usr/share/deepin/dde-file-manager/oem-menuextensions/$f"
    rm -f "$REAL_HOME/.local/share/deepin/dde-file-manager/oem-menuextensions/$f"
done

if [ "$DELETE_CONFIG" = "yes" ]; then
    echo "== 删除当前用户的配置、搜索数据库、自启动与右键菜单设置 =="
    rm -rf "$REAL_HOME/.config/fsearch"
    rm -rf "$REAL_HOME/.cache/fsearch"
    rm -rf "$REAL_HOME/.local/share/fsearch"
    rm -f "$REAL_HOME/.config/autostart/fsearch.desktop"
    rm -f "$REAL_HOME/.local/share/deepin/dde-file-manager/oem-menuextensions/fsearch-search.desktop"
    rm -f "$REAL_HOME/.local/share/deepin/dde-file-manager/oem-menuextensions/fsearch-search-blank.desktop"
    echo "已删除用户配置与搜索数据库。"
else
    echo "已保留用户配置与搜索数据库（~/.config/fsearch、~/.cache/fsearch）。"
    echo "如需单独清除配置与数据库，可重新运行："
    echo "  sudo bash uninstall_offline.sh --delete-config"
fi

echo
echo "卸载完成。fsearch 已从本机移除，系统未做任何改动。"
