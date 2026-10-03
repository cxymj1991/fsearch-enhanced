#!/usr/bin/env bash
#
# 一键编译（双击运行）
# ---------------------------------------------------------------------------
# 本脚本就放在源码文件夹里（与 uos20-build/ 平级）。
# 把整个 fsearch-0.3.2-enhanced 文件夹放到虚拟机桌面后，
# **双击本文件**即可完成「编译 + 打 deb」全过程。
#
# 所有产物与日志都收在源码文件夹内的「输出」子目录里，
# 不会在桌面或其他目录散落文件。
#
# 最终目录结构：
#   fsearch-0.3.2-enhanced/
#   ├── 一键编译.sh          ← 双击这个
#   ├── src/  meson.build     ← 源码
#   ├── uos20-build/          ← 底层脚本
#   └── 输出/                 ← 产物与日志都放这里（脚本自动创建）
#       ├── fsearch-0.3.2-uos20-portable.tar.gz
#       ├── fsearch-0.3.2-1_amd64.deb
#       └── 编译日志.log
#
# 首次使用请先给本脚本加执行权限（否则双击会用文本编辑器打开）：
#   · 在文件管理器里：右键 → 属性 → 勾选「允许作为程序执行」
#   · 或在终端里：    cd <桌面>/fsearch-0.3.2-enhanced && chmod +x 一键编译.sh
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_ROOT="$SCRIPT_DIR"                       # 本脚本就在源码根目录
BUILD_DIR="$SRC_ROOT/uos20-build"
OUT_DIR="$SRC_ROOT/输出"
LOG="$OUT_DIR/编译日志.log"

mkdir -p "$OUT_DIR"

# 双击运行时若为 root（例如从文件管理器以管理员身份打开），切回真实用户，
# 否则产物可能落到 /root 之类的地方。
if [ "$(id -u)" = "0" ] && [ -n "${SUDO_USER:-}" ]; then
    exec sudo -u "$SUDO_USER" -H bash "$0" "$@"
fi

{
    echo "==========================================================="
    echo " FSearch 0.3.2 增强版 · 一键编译"
    echo " 开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo " 源码目录: $SRC_ROOT"
    echo " 产物目录: $OUT_DIR"
    echo "==========================================================="
    echo
} >"$LOG"

log() { echo "$@" | tee -a "$LOG"; }

if [ ! -d "$BUILD_DIR" ]; then
    log "❌ 错误：找不到 uos20-build 目录"
    log ""
    log "  期望结构："
    log "    $SRC_ROOT/uos20-build/"
    log ""
    log "  说明：本脚本必须与 uos20-build 文件夹平级（即放在源码文件夹里）。"
    log ""
    log "按回车键关闭..."
    read -r
    exit 1
fi

log "⏳ 即将开始编译。"
log "   首次运行需要联网下载编译依赖（几分钟）；之后重新编译也会清理旧 build 目录。"
log "   期间请勿关闭本窗口。"
log ""

cd "$BUILD_DIR" || {
    log "❌ 无法进入构建目录：$BUILD_DIR"
    log "按回车键关闭..."
    read -r
    exit 1
}

# ---- 第 1 步：编译 + 打便携包 ----
log "==========================================================="
log " 第 1/2 步：编译"
log "==========================================================="
if sudo bash build_fsearch_uos20.sh >>"$LOG" 2>&1; then
    log "✅ 编译成功"
else
    log ""
    log "❌ 编译失败。错误信息见上方输出与 $LOG"
    log ""
    log "常见原因："
    log "  · 网络不通（首次编译需联网 apt 装依赖）"
    log "  · 源码不完整（应同时存在 src/ 与 uos20-build/）"
    log ""
    log "按回车键关闭..."
    read -r
    exit 1
fi

TARBALL="$(ls -t "$OUT_DIR"/fsearch-*-uos20-portable.tar.gz 2>/dev/null | head -1)"
log ""
log "📦 便携包：$TARBALL"

# ---- 第 2 步：打包成 deb ----
log ""
log "==========================================================="
log " 第 2/2 步：封装成 .deb（可双击安装）"
log "==========================================================="
if bash build_deb.sh >>"$LOG" 2>&1; then
    DEB="$(ls -t "$OUT_DIR"/fsearch-*_*.deb 2>/dev/null | head -1)"
    log "✅ deb 打包成功"
    log ""
    log "==========================================================="
    log " 🎉 全部完成！"
    log "==========================================================="
    log ""
    log "所有文件都在这个文件夹里（桌面不会有其它散落文件）："
    log "  $OUT_DIR"
    log ""
    log "  1) ${TARBALL##*/}"
    log "     → 配合 install_offline.sh 使用（完全绕过 deb 机制）"
    log "  2) ${DEB##*/}"
    log "     → 双击即可安装（可能被 UOS『应用安全』拦一次）"
    log ""
    log "把「输出」整个文件夹拷到离线单位电脑即可。"
    log ""
    log "⚠️ 重要：安装后请优先验证右键菜单 ——"
    log "   右键任意文件夹点『用 FSearch 搜索』，然后执行："
    log "     cat ~/.cache/fsearch/last-search-in.log"
    log "   期望：argv=... --search-in /真实路径/..."
    log "   若看到字面量 %f 或 %u，请把这个文件发给技术支持。"
    log ""
    log "按回车键关闭..."
    read -r
    exit 0
else
    log ""
    log "⚠️  deb 打包失败（不影响便携包，便携包仍可正常使用）"
    log "    错误信息见 $LOG"
    log "    修好问题后可单独重跑："
    log "      cd \"$BUILD_DIR\" && bash build_deb.sh"
    log ""
    log "按回车键关闭..."
    read -r
    exit 1
fi
