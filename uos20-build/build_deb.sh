#!/usr/bin/env bash
#
# build_deb.sh
# ---------------------------------------------------------------------------
# 把 build_fsearch_uos20.sh 产出的便携包，封装成标准 .deb 安装包，
# 这样在离线单位电脑上**双击即可安装**（UOS/DDE 会调用包管理器处理），
# 不必每次都手工敲命令。
#
# 用法（在 UOS 20 / Debian 10 上，需与构建同版本）：
#   bash build_deb.sh                      # 自动找 ./fsearch-0.3.2-uos20-portable.tar.gz
#   bash build_deb.sh /path/to/xxx.tar.gz  # 显式指定便携包
#
# 产物：
#   ./fsearch-0.3.2-uos20_amd64.deb
#
# 之后把 .deb 拷到单位电脑，双击安装即可。
# 卸载：UOS「软件中心」→ 找到 FSearch → 卸载
#      或命令行：sudo apt remove fsearch
#
# 说明（为什么这样设计）：
#   * 程序本体仍放在 /opt/fsearch —— 这是本项目的既定布局，不放进 /usr，
#     避免与系统自带库/目录混在一起，卸载时零残留，不会碰任何系统文件。
#   * 便携包把除 glibc 核心库外的运行依赖都打进了 /opt/fsearch/lib/，
#     因此 deb 的 Depends 只声明"系统本来就有的"基础包，
#     离线机上不会因为依赖缺失而装不上。
# ---------------------------------------------------------------------------
set -euo pipefail

PREFIX=/opt/fsearch
PKG_NAME=fsearch
PKG_VERSION=0.3.2-1
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 源码根目录 = uos20-build/ 的上一级
SRC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 所有产物统一放在源码文件夹内的「输出」子目录（与 build_fsearch_uos20.sh 一致）
OUT_DIR="$SRC_ROOT/输出"
mkdir -p "$OUT_DIR"

echo "== [1/4] 定位便携包（tar.gz）=="
# 优先去「输出」目录找，那里是 build_fsearch_uos20.sh 的固定产物位置
shopt -s nullglob
candidates=("$OUT_DIR"/fsearch-*-uos20-portable.tar.gz)
[ ${#candidates[@]} -eq 0 ] && candidates=("$SCRIPT_DIR"/fsearch-*-uos20-portable.tar.gz)
[ ${#candidates[@]} -eq 0 ] && candidates=("$SCRIPT_DIR"/../fsearch-*-uos20-portable.tar.gz)
shopt -u nullglob

if [ -n "${1:-}" ]; then
    TARBALL="$1"
elif [ ${#candidates[@]} -eq 1 ]; then
    TARBALL="${candidates[0]}"
elif [ ${#candidates[@]} -eq 0 ]; then
    echo "错误：脚本目录里没有找到 fsearch-*-uos20-portable.tar.gz" >&2
    echo "     请先运行：sudo bash build_fsearch_uos20.sh" >&2
    echo "     或显式传入：bash build_deb.sh <便携包路径>" >&2
    exit 1
else
    echo "发现多个便携包，请显式指定要封装哪一个：" >&2
    printf '  - %s\n' "${candidates[@]}" >&2
    exit 1
fi

[ -f "$TARBALL" ] || { echo "找不到便携包: $TARBALL" >&2; exit 1; }
echo "  使用: $TARBALL ($(du -h "$TARBALL" | cut -f1))"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
PKGROOT="$WORK/deb"
mkdir -p "$PKGROOT/DEBIAN"

echo "== [2/4] 解包并校验 =="
tar xzf "$TARBALL" -C "$PKGROOT"
[ -d "$PKGROOT$PREFIX" ] || { echo "错误：包内没有 $PREFIX 目录，便携包格式不对" >&2; exit 1; }
[ -x "$PKGROOT$PREFIX/bin/fsearch" ] || { echo "错误：包内缺少 $PREFIX/bin/fsearch" >&2; exit 1; }
# GLib 2.58 兼容垫片应已在编译期生效，二进制不应链接到 glibc 之外的高版本符号
echo "  ✓ 便携包结构正确"

# 校验二进制没有依赖 glibc 之外的核心系统库（防止误把别处编的包打进来）
if ldd "$PKGROOT$PREFIX/libexec/fsearch-bin" 2>/dev/null | grep -q "not found"; then
    echo "错误：二进制存在未满足的依赖，请先确认便携包是在 UOS 20 上编译的：" >&2
    ldd "$PKGROOT$PREFIX/libexec/fsearch-bin" | grep "not found" >&2
    exit 1
fi
echo "  ✓ 依赖自检通过（无 not found）"

echo "== [3/4] 生成 DEBIAN/ 控制文件与维护脚本 =="

# 依赖说明：只声明系统本来就具备的基础包。
# 便携包自带 gtk/glib/pcre2/icu 等非核心库，故这里不会在离线机上产生依赖缺失。
cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: FSearch Enhanced Contributors <noreply@example.com>
Installed-Size: $(du -ks "$PKGROOT$PREFIX" | cut -f1)
Depends: libc6 (>= 2.17), libglib2.0-0 (>= 2.58), libgtk-3-0 (>= 3.22)
Description: Fast file search utility (增强版 / UOS 20 离线版)
 FSearch 是一个快速文件搜索工具，支持名称与路径匹配、过滤、排序与实时索引。
 .
 本包为面向统信 UOS 20（Debian 10）离线环境的增强版：
  - 系统托盘常驻，关闭窗口后仍在后台索引
  - 可选开机自动启动（systemd --user，不触发系统授权弹窗）
  - 文件管理器右键菜单「用 FSearch 搜索…」
  - 搜索结果批量重命名
  - 结果列表多选后可直接拖拽复制/剪切
 运行依赖已随包附带，除 glibc 核心库外无需联网安装任何系统库。
EOF

# 打包权限固定为 0755，避免 dpkg 校验告警
cat > "$PKGROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
# 安装后：建命令软链 + 桌面入口 + 文件管理器右键菜单
set -e
PREFIX=/opt/fsearch

[ -x "$PREFIX/bin/fsearch" ] || exit 0

# ---- 命令软链（系统级，幂等）----
ln -sf "$PREFIX/bin/fsearch" /usr/local/bin/fsearch

# ---- 桌面图标 ----
# 【关键】优先装到**系统级** /usr/share/applications/，而不是用户家目录下。
# 原因：deb 双击安装走图形化包管理器（PackageKit/polkit），它不设置 SUDO_USER，
# 也不设置 PKEXEC_UID，因此无法可靠判断"是谁点的安装"。若装到用户家目录，
# 一旦判断成 root，入口就落到 /root/.local/...，实际用户当然在开始菜单里看不到。
# 装到系统级则对所有用户都生效，行为可预期。
DESKTOP_SRC="$PREFIX/share/applications/io.github.cboxdoerfer.FSearch.desktop"
ICON_ABS="$PREFIX/share/icons/hicolor/scalable/apps/io.github.cboxdoerfer.FSearch.svg"
if [ -f "$DESKTOP_SRC" ]; then
    SYS_APP=/usr/share/applications
    mkdir -p "$SYS_APP"
    cp "$DESKTOP_SRC" "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop"
    # 模板里 Exec=fsearch / TryExec=fsearch 依赖 PATH 上的软链，这里写绝对路径更稳
    # （离线机上 /usr/local/bin 可能不在某些桌面会话的 PATH 里）
    sed -i "s|^Exec=.*|Exec=$PREFIX/bin/fsearch|" "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop"
    sed -i "s|^TryExec=.*|TryExec=$PREFIX/bin/fsearch|" "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop"
    if [ -f "$ICON_ABS" ]; then
        # 绝对路径图标，免去依赖图标主题搜索路径
        sed -i "s|^Icon=.*|Icon=$ICON_ABS|" "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop"
    fi
    chmod 644 "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop"
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$SYS_APP" >/dev/null 2>&1 || true
fi

# ---- 桌面图标文件（UOS 不像 GNOME 那样自动把开始菜单程序显示到桌面）----
REAL_USER="${SUDO_USER:-}"
if [ -z "$REAL_USER" ] && [ -n "${PKEXEC_UID:-}" ]; then
    REAL_USER="$(getent passwd "$PKEXEC_UID" 2>/dev/null | cut -d: -f1)"
fi
if [ -z "$REAL_USER" ] || [ "$REAL_USER" = "root" ]; then
    # 图形化安装拿不到调用者信息，退而求其次：用 XDG 的默认用户目录反查
    for cand in /home/*; do
        [ -d "$cand" ] || continue
        [ -f /etc/passwd ] || continue
        u="$(basename "$cand")"
        # 必须是普通用户（uid >= 1000）且家目录就是该路径
        uid_line="$(getent passwd "$u" 2>/dev/null | cut -d: -f3)"
        home_line="$(getent passwd "$u" 2>/dev/null | cut -d: -f6)"
        case "$uid_line" in ''|*[!0-9]*) continue ;; esac
        [ "$uid_line" -ge 1000 ] 2>/dev/null || continue
        [ "$home_line" = "$cand" ] || continue
        REAL_USER="$u"
        break
    done
fi
[ -n "$REAL_USER" ] || REAL_USER="root"
REAL_HOME="$(getent passwd "$REAL_USER" 2>/dev/null | cut -d: -f6)"
[ -n "$REAL_HOME" ] || REAL_HOME=/root

if [ -f "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop" ] && [ -d "$REAL_HOME" ] && [ "$REAL_USER" != "root" ]; then
    DESKTOP_DIR="$REAL_HOME/桌面"
    [ -d "$DESKTOP_DIR" ] || DESKTOP_DIR="$REAL_HOME/Desktop"
    if [ -d "$DESKTOP_DIR" ]; then
        cp "$SYS_APP/io.github.cboxdoerfer.FSearch.desktop" "$DESKTOP_DIR/" 2>/dev/null || true
        # 必须可执行，否则双击会用文本编辑器打开
        chmod +x "$DESKTOP_DIR/io.github.cboxdoerfer.FSearch.desktop" 2>/dev/null || true
        chown "$REAL_USER:$REAL_USER" "$DESKTOP_DIR/io.github.cboxdoerfer.FSearch.desktop" 2>/dev/null || true
    fi
fi

# ---- 文件管理器右键菜单（三个场景，Exec 必须用空格 + %u）----
OEM_DIR=/usr/share/deepin/dde-file-manager/oem-menuextensions
if [ -d "$OEM_DIR" ] || mkdir -p "$OEM_DIR" 2>/dev/null; then
    write_oem() {
        # $1=文件名 $2=MimeType $3=MenuTypes $4=字段码（%f=选中项路径 %p=当前目录）
        cat > "$OEM_DIR/$1" <<EOT
[Desktop Entry]
Type=Application
Name=Search with FSearch…
Name[zh_CN]=用 FSearch 搜索…
GenericName=Search files in this folder
GenericName[zh_CN]=在当前文件夹中搜索文件
Comment=Search files in this folder with FSearch
Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件
Icon=io.github.cboxdoerfer.FSearch
MimeType=$2
X-DFM-MenuTypes=$3
Exec=$PREFIX/bin/fsearch $4
Terminal=false
EOT
    }
    # %u 必须用空格与 --search-in 分开（libqtxdg 按空白分词，且 %u 才是真正展开的字段码）
    write_oem fsearch-search.desktop        "inode/directory;"      "SingleDir"   "%f"
    write_oem fsearch-search-blank.desktop  "inode/directory;"      "EmptyArea"   "%p"
    {
        echo "[Desktop Entry]"
        echo "Type=Application"
        echo "Name=Search with FSearch…"
        echo "Name[zh_CN]=用 FSearch 搜索…"
        echo "GenericName=Search files in this folder"
        echo "GenericName[zh_CN]=在当前文件夹中搜索文件"
        echo "Comment=Search files in this folder with FSearch"
        echo "Comment[zh_CN]=使用 FSearch 在当前文件夹中搜索文件"
        echo "Icon=io.github.cboxdoerfer.FSearch"
        echo "MimeType=application/x-desktop;"
        echo "X-DFM-SupportSuffix=desktop;"
        echo "X-DFM-SupportSchemes=file;"
        echo "X-DFM-NotShowIn=Desktop;"
        echo "Exec=$PREFIX/bin/fsearch %f"
        echo "Terminal=false"
        echo "X-DFM-MenuTypes=SingleFile"
    } > "$OEM_DIR/fsearch-search-link.desktop"
    chown -R "$REAL_USER:$REAL_USER" "$OEM_DIR" 2>/dev/null || true
fi

exit 0
EOF

cat > "$PKGROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
pkill -f /opt/fsearch 2>/dev/null || true
pkill -x fsearch-bin 2>/dev/null || true
exit 0
EOF

cat > "$PKGROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
PREFIX=/opt/fsearch
REAL_USER="${SUDO_USER:-}"
[ -n "$REAL_USER" ] && REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)" || REAL_HOME=/root

case "$1" in
  remove|purge)
    rm -f /usr/local/bin/fsearch
    # 清掉 postinst 装到系统级的桌面入口（否则卸载后开始菜单里还留着一个打不开的图标）
    rm -f /usr/share/applications/io.github.cboxdoerfer.FSearch.desktop
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
    for f in fsearch-search.desktop fsearch-search-blank.desktop fsearch-search-link.desktop; do
        rm -f "/usr/share/deepin/dde-file-manager/oem-menuextensions/$f"
        [ -n "$REAL_HOME" ] && rm -f "$REAL_HOME/.local/share/deepin/dde-file-manager/oem-menuextensions/$f"
    done
    # 清掉所有普通用户家目录里的桌面/开始菜单副本（postinst 的用户识别可能不准，
    # 这里全扫一遍确保不残留图标）
    for home in /home/*; do
        [ -d "$home" ] || continue
        rm -f "$home/.local/share/applications/io.github.cboxdoerfer.FSearch.desktop" 2>/dev/null || true
        rm -f "$home/桌面/io.github.cboxdoerfer.FSearch.desktop" 2>/dev/null || true
        rm -f "$home/Desktop/io.github.cboxdoerfer.FSearch.desktop" 2>/dev/null || true
    done
    rm -f /root/.local/share/applications/io.github.cboxdoerfer.FSearch.desktop 2>/dev/null || true
    rm -f /root/桌面/io.github.cboxdoerfer.FSearch.desktop 2>/dev/null || true
    ;;
esac

# 注销开机自启（fsearch 本体可能已被删，无法自行注销 systemd unit）
if command -v systemctl >/dev/null 2>&1 && [ -n "$REAL_USER" ]; then
    UID_NUM="$(id -u "$REAL_USER" 2>/dev/null || true)"
    if [ -n "$UID_NUM" ]; then
        su - "$REAL_USER" -c "XDG_RUNTIME_DIR=/run/user/$UID_NUM systemctl --user disable --now fsearch.service" >/dev/null 2>&1 || true
        su - "$REAL_USER" -c "XDG_RUNTIME_DIR=/run/user/$UID_NUM systemctl --user reset-failed fsearch.service" >/dev/null 2>&1 || true
    fi
fi
[ -n "$REAL_HOME" ] && rm -f "$REAL_HOME/.config/systemd/user/fsearch.service"
[ -n "$REAL_HOME" ] && rm -f "$REAL_HOME/.config/autostart/fsearch.desktop"
[ -n "$REAL_HOME" ] && rm -f "$REAL_HOME/.config/autostart/io.github.cboxdoerfer.FSearch.desktop"

if [ "$1" = "purge" ]; then
    [ -n "$REAL_HOME" ] && rm -rf "$REAL_HOME/.config/fsearch" "$REAL_HOME/.cache/fsearch"
fi
exit 0
EOF

chmod 755 "$PKGROOT/DEBIAN/postinst" "$PKGROOT/DEBIAN/prerm" "$PKGROOT/DEBIAN/postrm"
chmod 644 "$PKGROOT/DEBIAN/control"
# 固定权限，避免 dpkg 产生 "Packaging QA" 警告。
#
# 注意：这里【不要】手动 chown -R root:root。build_deb.sh 设计成普通用户即可运行，
# 手动 chown 会因权限不足对每个文件报 "不允许的操作"，而脚本开头是 set -euo pipefail，
# 第一个报错就会直接中止脚本，导致打包失败（首轮实机就踩了这个）。
# 属主问题由下面的 `dpkg-deb --build --root-owner-group` 统一处理 ——
# 该选项会把包内所有文件的 owner/group 强制设为 root:root，不需要 root 权限。
find "$PKGROOT" -type d -exec chmod 755 {} +
find "$PKGROOT" -type f -exec chmod go-w {} +

echo "== [4/4] 生成 .deb =="
OUT="$OUT_DIR/${PKG_NAME}-${PKG_VERSION}_${ARCH}.deb"
# --root-owner-group：把包内文件属主统一设为 root:root，无需 root 权限，
# 这样本脚本普通用户就能跑，不必 sudo。
# 不再吞掉 stderr（2>/dev/null），否则真出错时看不到原因。
if ! dpkg-deb --build --root-owner-group "$PKGROOT" "$OUT"; then
    echo "错误：dpkg-deb 打包失败" >&2
    echo "  若上面提示 --root-owner-group 不支持（老版本 dpkg），" >&2
    echo "  可改用 sudo bash build_deb.sh，或手动执行不带该选项的版本。" >&2
    exit 1
fi

echo
echo "完成！产物："
echo "  $OUT"
echo "大小： $(du -h "$OUT" | cut -f1)"
echo
echo "安装（在离线单位电脑上）："
echo "  方式 A（推荐）：双击该 .deb 文件，按提示输入密码安装"
echo "  方式 B：命令行  sudo apt install ./$(basename "$OUT")"
echo
echo "卸载："
echo "  UOS『软件中心』→ 找到 FSearch → 卸载"
echo "  或命令行：sudo apt remove fsearch"
