#!/usr/bin/env bash
#
# install_offline.sh
# ---------------------------------------------------------------------------
# 在「不能上网的单位电脑（统信 UOS 20 / Debian 10）」上离线安装 fsearch。
# 完全不触碰系统包管理器、不升级任何系统库，仅往 /opt/fsearch 释放文件。
#
# 用法（需要 root，因为要写 /opt 和 /usr/local/bin）：
#   sudo bash install_offline.sh fsearch-0.3.2-uos20-portable.tar.gz
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
elif [ -f "$SCRIPT_DIR/fsearch-0.3.2-uos20-portable.tar.gz" ]; then
  ARCHIVE="$SCRIPT_DIR/fsearch-0.3.2-uos20-portable.tar.gz"
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
      echo "   请把 fsearch-0.3.2-uos20-portable.tar.gz 与本脚本放在同一目录，"
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
# dde-file-manager 5.x（= 统信 UOS 20 的底座）只扫描系统级目录
# /usr/share/deepin/dde-file-manager/oem-menuextensions/，不读用户级目录。
# 目录本身属主是 root:root、权限 755，因此普通用户在首选项里开关右键菜单时无法写入，
# 必须在这里由 root 预先把该目录的属主改成真实用户。
OEM_DIR="/usr/share/deepin/dde-file-manager/oem-menuextensions"
OEM_FILE="$OEM_DIR/fsearch-search.desktop"
sudo mkdir -p "$OEM_DIR"
sudo chown "$REAL_USER:$REAL_USER" "$OEM_DIR"
echo "  已将 $OEM_DIR 属主设为 $REAL_USER（便于在首选项中开关右键菜单，无需 root）"
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
Exec=$PREFIX/bin/fsearch %f
Terminal=false
X-DFM-MenuTypes=SingleDir
EOF
# 归属真实用户，这样用户在设置里开关“集成到右键菜单”时无需 root 即可增删该文件
sudo chown "$REAL_USER:$REAL_USER" "$OEM_FILE"

# 第二个菜单项：在文件夹"空白处"右键也出现"用 FSearch 搜索"。
#
# 关于字段码：dde-file-manager 5.x（= 统信 UOS 20 的底座）已经移除了 %p，
# 若这里沿用 %p，dfm 会把字面量 "%p" 原样传给 fsearch，菜单项必然点不开。
# 因此改用 %F（选中项的 URI 列表，%U 的多选对应形式）：
#   - 空白处右键且有选中项 → 取第一项的父目录作为搜索根；
#   - 空白处右键且无选中   → %F 为空，fsearch 静默退出（不打扰用户）。
# 这样菜单项只在确实能确定目标目录时才生效。
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
Exec=$PREFIX/bin/fsearch %p
Terminal=false
X-DFM-MenuTypes=EmptyArea
EOF
sudo chown "$REAL_USER:$REAL_USER" "$OEM_BLANK_FILE"

# 第三个菜单项：右键「文件夹快捷方式」（.desktop 文件）时也出现「用 FSearch 搜索」。
# 快捷方式在 dde-file-manager 里被识别为 application/x-desktop，菜单类型为 SingleFile（单个普通文件）。
# 这里把快捷方式文件自身的路径通过 %f 传给 fsearch，由 fsearch 解析其 URL= / Exec= 字段
# 还原出它所指向的源目录，再限定在该目录内搜索 —— 语义与"在源文件夹上右键后搜索"完全一致。
OEM_LINK_FILE="$OEM_DIR/fsearch-search-link.desktop"
sudo tee "$OEM_LINK_FILE" >/dev/null <<EOF
[Desktop Entry]
Type=Application
Name=Search with FSearch…
Name[zh_CN]=用 FSearch 搜索…
GenericName=Search files in this folder
GenericName[zh_CN]=在当前文件夹中搜索文件
Comment=Search files in this folder with FSearch
Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件
Icon=io.github.cboxdoerfer.FSearch
MimeType=application/x-desktop;
X-DFM-SupportSuffix=desktop;
X-DFM-SupportSchemes=file;
X-DFM-NotShowIn=Desktop;
Exec=$PREFIX/bin/fsearch %f
Terminal=false
X-DFM-MenuTypes=SingleFile
EOF
sudo chown "$REAL_USER:$REAL_USER" "$OEM_LINK_FILE"
echo "已注册 deepin 文件管理器右键菜单项："
echo "  1) 选中文件夹      = SingleDir （用 %f 传文件夹路径）"
echo "  2) 文件夹空白处    = EmptyArea （用 %F；5.x 下展开为 [当前目录]）"
echo "  3) 文件夹快捷方式  = SingleFile （用 %f 传 .desktop 路径，由 fsearch 解析出源目录）"
# 生效方式（依据 dde-file-manager 5.x 源码 dfmadditionalmenu.cpp 的 QFileWatcher）：
#   - 监听 subfileCreated / subfileDeleted，500ms 防抖后自动重载 → 首次新增文件无需重启；
#   - 但**不监听文件内容变化** → 改已有 OEM 文件的内容不会生效，必须先删再写。
# 因此这里只把 killall 作为"菜单没出现时"的兜底手段，不说成必需步骤。
echo "首次安装约 0.5 秒后自动生效，无需重启文件管理器。"
echo "提示：若日后修改已有菜单项内容，dfm 5.x 不监听文件变化，需先删除该 .desktop 再重新写入。"
echo "      若菜单仍未出现，可执行：killall dde-file-manager"

# 清理历史版本可能留下的旧式自启动项（fsearch.desktop）。
# 0.3.2 起自启动优先走 systemd --user；这里只删旧的 XDG 残留，避免重复与授权弹窗。
echo "== 清理旧版 XDG 自启动残留（0.3.2 起自启动走 systemd --user）=="
rm -f "$REAL_HOME/.config/autostart/fsearch.desktop" 2>/dev/null || true
echo "  已清理 ~/.config/autostart/fsearch.desktop（若存在）"
echo "  如需开机自启，请在 fsearch「首选项 → 界面」勾选『开机自动启动』"

echo
echo "安装完成，可直接运行：fsearch"
echo "若桌面菜单没立刻出现，注销重登录一次即可。"
echo
echo "验证（应看不到 \"not found\"）："
echo "  ldd $PREFIX/bin/fsearch | grep -i 'not found' || echo '依赖齐全，OK'"
echo
echo "卸载命令："
echo "  sudo bash uninstall_offline.sh"
