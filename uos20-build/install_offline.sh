#!/usr/bin/env bash
#
# install_offline.sh
# ---------------------------------------------------------------------------
# 在「不能上网的单位电脑（统信 UOS 20 / Debian 10）」上离线安装 fsearch。
# 完全不触碰系统包管理器、不升级任何系统库，仅往 /opt/fsearch 释放文件。
#
# 用法（需要 root，因为要写 /opt 和 /usr/local/bin）：
#   sudo bash install_offline.sh fsearch-0.3.1-uos20-portable.tar.gz
#
# 卸载：推荐直接用配套脚本（会一并清掉文件管理器右键菜单项）
#   sudo bash uninstall_offline.sh
# 若想手动删，需额外清掉 deepin 文件管理器的右键菜单扩展：
#   sudo rm -rf /opt/fsearch /usr/local/bin/fsearch \
#     ~/.local/share/applications/io.github.cboxdoerfer.FSearch.desktop \
#     /usr/share/deepin/dde-file-manager/oem-menuextensions/fsearch-search.desktop \
#     /usr/share/deepin/dde-file-manager/oem-menuextensions/fsearch-search-blank.desktop
#   killall dde-file-manager
# ---------------------------------------------------------------------------
set -euo pipefail

# 脚本自身所在目录（兼容路径含空格、中文、@ 等）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 决定归档路径：优先用传入的参数；未传则自动在脚本目录里找 fsearch-*.tar.gz
if [ -n "${1:-}" ]; then
  ARCHIVE="$1"
  # 传入的是相对路径且当前目录下找不到时，回退到脚本所在目录查找
  if [ ! -f "$ARCHIVE" ]; then
    ARCHIVE="$SCRIPT_DIR/$1"
  fi
elif [ -f "$SCRIPT_DIR/fsearch-0.3.1-uos20-portable.tar.gz" ]; then
  ARCHIVE="$SCRIPT_DIR/fsearch-0.3.1-uos20-portable.tar.gz"
else
  # 退一步：只在脚本目录里找 fsearch-*.tar.gz（不碰其它无关 tar 包，避免误装）。
  # 仅当恰好只有一个时才自动选用；若有多个则要求用户显式指定，避免装错。
  shopt -s nullglob
  candidates=("$SCRIPT_DIR"/fsearch-*.tar.gz)
  shopt -u nullglob
  if [ ${#candidates[@]} -eq 1 ]; then
    ARCHIVE="${candidates[0]}"
  else
    echo "用法: sudo bash install_offline.sh <fsearch-*.tar.gz>"
    if [ ${#candidates[@]} -gt 1 ]; then
      echo "   在脚本目录发现多个 fsearch-*.tar.gz，请明确指定要安装的那一个："
      printf '     - %s\n' "${candidates[@]}"
    else
      echo "   请把 fsearch-0.3.1-uos20-portable.tar.gz 与本脚本放在同一目录，"
      echo "   或显式传入归档路径。"
    fi
    exit 1
  fi
fi

PREFIX=/opt/fsearch

[ -f "$ARCHIVE" ] || { echo "找不到归档: $ARCHIVE"; exit 1; }

# 停止正在运行的旧版 fsearch。
# 必须做：fsearch 是 GApplication 单实例（D-Bus），若旧进程还活着，
# 解包覆盖后运行新版本会直接 D-Bus 激活旧进程 → 打开后依然是旧版本。
echo "== 停止正在运行的旧版 fsearch =="
pkill -f "$PREFIX" 2>/dev/null || true        # 命令行含 /opt/fsearch 的（启动器脚本 + 真二进制）
pkill -x fsearch-bin 2>/dev/null || true      # 兜底：按进程名匹配
sleep 1

echo "== 解包到 $PREFIX =="
sudo tar xzf "$ARCHIVE" -C /

echo "== 创建命令软链 /usr/local/bin/fsearch =="
sudo ln -sf "$PREFIX/bin/fsearch" /usr/local/bin/fsearch

# 取真实用户：本脚本以 sudo 运行，~ 此时是 /root，必须用真实用户名
REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

echo "== 添加桌面入口（开始菜单 + 桌面图标）=="
DESK_SRC="$PREFIX/share/applications/io.github.cboxdoerfer.FSearch.desktop"
if [ -f "$DESK_SRC" ]; then
  # 1) 开始菜单：放到真实用户的 ~/.local/share/applications
  APP_DIR="$REAL_HOME/.local/share/applications"
  mkdir -p "$APP_DIR"
  cp "$DESK_SRC" "$APP_DIR/"
  DEST_DESK="$APP_DIR/io.github.cboxdoerfer.FSearch.desktop"
  chown "$REAL_USER:$REAL_USER" "$DEST_DESK"

  # 图标改成绝对路径，保证菜单/桌面能显示出来
  ICON="$PREFIX/share/icons/hicolor/scalable/apps/io.github.cboxdoerfer.FSearch.svg"
  if [ -f "$ICON" ]; then
    sed -i "s|^Icon=.*|Icon=$ICON|" "$DEST_DESK"
  fi
  # Exec/TryExec 强制成绝对路径，避免菜单环境 PATH 不全时找不到命令
  sed -i "s|^Exec=.*|Exec=$PREFIX/bin/fsearch|" "$DEST_DESK"
  sed -i "s|^TryExec=.*|TryExec=$PREFIX/bin/fsearch|" "$DEST_DESK"

  # 2) 桌面图标：UOS 不会自动把开始菜单程序显示到桌面，需要手动放文件
  DESKTOP_DIR="$REAL_HOME/桌面"
  [ -d "$DESKTOP_DIR" ] || DESKTOP_DIR="$REAL_HOME/Desktop"
  if [ -d "$DESKTOP_DIR" ]; then
    cp "$DEST_DESK" "$DESKTOP_DIR/"
    chown "$REAL_USER:$REAL_USER" "$DESKTOP_DIR/io.github.cboxdoerfer.FSearch.desktop"
    chmod +x "$DESKTOP_DIR/io.github.cboxdoerfer.FSearch.desktop"
    # 标记为「可信」，深度桌面才会以图标+可双击方式显示（否则当成文本文件打开）
    sudo -u "$REAL_USER" gio set "$DESKTOP_DIR/io.github.cboxdoerfer.FSearch.desktop" "metadata::trusted" true 2>/dev/null || true
  fi

  # 3) 刷新桌面数据库，让开始菜单尽快识别
  sudo -u "$REAL_USER" update-desktop-database "$APP_DIR" 2>/dev/null || true
  echo "已为 $REAL_USER 添加开始菜单入口与桌面图标。"
else
  echo "警告：包内未找到 .desktop 文件，跳过桌面集成（程序仍可用 fsearch 命令运行）。"
fi

echo "== 注册文件管理器右键菜单（用 FSearch 搜索此文件夹）=="
OEM_DIR="/usr/share/deepin/dde-file-manager/oem-menuextensions"
OEM_FILE="$OEM_DIR/fsearch-search.desktop"
sudo mkdir -p "$OEM_DIR"
sudo tee "$OEM_FILE" >/dev/null <<EOF
[Desktop Entry]
Type=Application
Name=Search with FSearch…
Name[zh_CN]=用 FSearch 搜索…
GenericName=Search files in this folder
GenericName[zh_CN]=在当前文件夹中搜索文件
Comment=Search files in this folder with FSearch
Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件
Icon=io.github.cboxdoerfer.FSearch
MimeType=inode/directory;
Exec=$PREFIX/bin/fsearch --search-in=%f
Terminal=false
X-DFM-MenuTypes=SingleDir
EOF
# 归属真实用户，这样用户在设置里开关“集成到右键菜单”时无需 root 即可增删该文件
sudo chown "$REAL_USER:$REAL_USER" "$OEM_FILE"

# 第二个菜单项：在文件夹“空白处”右键也出现“用 FSearch 搜索”，限定到当前所在文件夹。
# EmptyArea 表示右键点文件管理器空白处时显示；当前文件夹路径由 %p 传入。
# 若某些 dde-file-manager 版本不替换 %p，fsearch 会忽略该占位符（见 normalize_search_root），
# 退化为“整个数据库”搜索，不会崩溃。
OEM_BLANK_FILE="$OEM_DIR/fsearch-search-blank.desktop"
sudo tee "$OEM_BLANK_FILE" >/dev/null <<EOF
[Desktop Entry]
Type=Application
Name=Search with FSearch…
Name[zh_CN]=用 FSearch 搜索…
GenericName=Search files in this folder
GenericName[zh_CN]=在当前文件夹中搜索文件
Comment=Search files in this folder with FSearch
Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件
Icon=io.github.cboxdoerfer.FSearch
MimeType=inode/directory;
Exec=$PREFIX/bin/fsearch --search-in=%p
Terminal=false
X-DFM-MenuTypes=EmptyArea
EOF
sudo chown "$REAL_USER:$REAL_USER" "$OEM_BLANK_FILE"
echo "已注册 deepin 文件管理器右键菜单项（选中文件夹=SingleDir 用 %f；空白处=EmptyArea 用 %p）。需重启文件管理器：killall dde-file-manager。"

echo
echo "安装完成，可直接运行：fsearch"
echo "若桌面菜单没立刻出现，注销重登录一次即可。"
echo
echo "验证（应看不到 \"not found\"）："
echo "  ldd $PREFIX/bin/fsearch | grep -i 'not found' || echo '依赖齐全，OK'"
echo
echo "卸载命令："
echo "  sudo bash uninstall_offline.sh"
