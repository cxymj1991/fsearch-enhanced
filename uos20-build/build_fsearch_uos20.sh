#!/usr/bin/env bash
#
# build_fsearch_uos20.sh
# ---------------------------------------------------------------------------
# 在「统信 UOS 20（基于 Debian 10）」的虚拟机里编译 fsearch 0.3.2，
# 并打成一个「自带依赖、可直接拷贝到离线单位电脑」的便携 tar 包。
#
# 适用环境：
#   - 构建机：UOS 20 / Debian 10（与目标机同版本，保证二进制 ABI 兼容）
#   - 构建机需要能上网（仅用于 apt 安装【编译依赖】，运行时依赖不需要）
#   - 目标机（单位电脑）：同版本 UOS 20，不能上网 —— 本脚本产出的包无需它联网
#
# 用法：
#   sudo bash build_fsearch_uos20.sh /path/to/fsearch-0.3.2    # 传解压后的源码目录
#   sudo bash build_fsearch_uos20.sh /path/to/fsearch-0.3.2.tar.gz  # 传源码 tar 包（自动解压）
#   sudo bash build_fsearch_uos20.sh                          # 不传：自动在脚本目录找
#     （优先 fsearch-0.3.2 目录，其次 fsearch-*.tar.gz，都找不到才报错）
#
# 产物：
#   当前目录下的 fsearch-0.3.2-uos20-portable.tar.gz
#   拷到单位电脑后，用 install_offline.sh 解包即可。
# ---------------------------------------------------------------------------
set -euo pipefail

PREFIX="/opt/fsearch"
WORK="$(mktemp -d)"
DEST="$WORK/dest"                      # DESTDIR 根，里面会是 opt/fsearch/...

# 脚本自身所在目录（兼容路径含空格、中文、@ 等）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 源码根目录 = uos20-build/ 的上一级
SRC_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 所有产物统一放在源码文件夹内的「输出」子目录，与源码同处一地，
# 避免在桌面 / 当前工作目录散落一堆文件。
OUT_DIR="$SRC_ROOT/输出"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/fsearch-0.3.2-uos20-portable.tar.gz"

# 定位源码：优先用传入参数；未传则自动在脚本目录找（目录优先，其次 tar.gz）。
# 传的是 tar.gz 时解压到临时目录。结果输出到 $WORK/src（源码根目录）。
locate_src() {
  local given="${1:-}"
  local candidate_dir="" candidate_tar=""

  if [ -n "$given" ]; then
    # 用户显式传入：目录直接用；tar.gz 解压；其它报错
    if [ -d "$given" ]; then
      candidate_dir="$given"
    elif [ -f "$given" ] && [[ "$given" == *.tar.gz ]]; then
      candidate_tar="$given"
    else
      echo "找不到源码: $given（既不是目录也不是 .tar.gz）" >&2
      return 1
    fi
  else
    # 未传参：脚本目录里按优先级找
    # 依次尝试 fsearch-0.3.2-enhanced（增强版实际目录名）、fsearch-0.3.2（上游原版目录名），
    # 命中的那个直接用。这样无论用户把增强版还是原版目录放在哪都能自动识别。
    for d in fsearch-0.3.2-enhanced fsearch-0.3.2; do
      if [ -z "$candidate_dir" ] && [ -d "$SCRIPT_DIR/$d" ]; then
        candidate_dir="$SCRIPT_DIR/$d"
      fi
    done
    if [ -z "$candidate_dir" ] && [ -d "$SCRIPT_DIR/.." ] && [ -f "$SCRIPT_DIR/../src/meson.build" ]; then
      # 兜底：源码就在脚本上一级（本项目 src/ 与 uos20-build/ 平级的布局）
      candidate_dir="$SCRIPT_DIR/.."
    fi
    if [ -z "$candidate_dir" ]; then
      # 恰好只有一个 fsearch-*.tar.gz 才自动用；多个则要求显式指定
      shopt -s nullglob
      local tars=("$SCRIPT_DIR"/fsearch-*.tar.gz)
      shopt -u nullglob
      if [ ${#tars[@]} -eq 1 ]; then
        candidate_tar="${tars[0]}"
      elif [ ${#tars[@]} -gt 1 ]; then
        echo "脚本目录发现多个 fsearch-*.tar.gz，请显式指定要编译的那一个：" >&2
        printf '  - %s\n' "${tars[@]}" >&2
        return 1
      fi
    fi
  fi

  if [ -n "$candidate_dir" ]; then
    echo "$candidate_dir"
  elif [ -n "$candidate_tar" ]; then
    mkdir -p "$WORK/src"
    echo "== 解压源码 $candidate_tar ==" >&2
    tar -xzf "$candidate_tar" -C "$WORK/src"
    # tar 包解压后通常是单个顶层目录（fsearch-0.3.2），取其第一个条目
    local extracted
    extracted="$(find "$WORK/src" -mindepth 1 -maxdepth 1 -type d | head -n1)"
    [ -n "$extracted" ] || { echo "tar 包内没有目录，解压失败" >&2; return 1; }
    echo "$extracted"
  else
    echo "未找到源码。请把源码目录 fsearch-0.3.2 或 fsearch-*.tar.gz 放到脚本同目录，"
    echo "或显式传入：sudo bash build_fsearch_uos20.sh <源码目录或tar.gz>" >&2
    return 1
  fi
}

SRC="$(locate_src "${1:-}")"

# 这些库必须来自目标机自身的 glibc/gcc，绝不能打包进我们的包，
# 否则会因 glibc 版本不匹配导致无法启动。
CORE_LIBS="libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 \
libgcc_s.so.1 libstdc++.so.6 ld-linux-x86-64.so.2 libresolv.so.2 libutil.so.1"

is_core() {
  local b="$1"
  for c in $CORE_LIBS; do
    [ "$b" = "$c" ] && return 0
  done
  return 1
}

echo "== [1/5] 安装编译依赖（仅构建机需要联网） =="
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  meson ninja-build pkg-config gettext itstool \
  gcc \
  libicu-dev libpcre2-dev libglib2.0-dev libgtk-3-dev libx11-dev \
  appstream libxml2-utils yelp-tools
#   libx11-dev       -> 全局快捷键（XGrabKey）所需
#   appstream        -> 提供 metainfo.its（i18n.merge_file type:'xml' 必需）
#   libxml2-utils    -> 提供 xmllint（消除 gresource 的 XMLLINT 警告）
#   yelp-tools       -> 提供 yelp-build（gnome.yelp 生成帮助文档必需）

echo "== [2/5] 配置并编译 =="
[ -d "$SRC" ] || { echo "找不到源码目录: $SRC"; exit 1; }
echo "  使用源码: $SRC"
cd "$SRC"

# ---------- GLib 版本兼容 ----------
# 0.3.2 上游要求 GLib >= 2.62，而 UOS 20（Debian 10）自带 GLib 2.58.3。
# 不能用「升级构建机 glib」来解决——目标机也是 2.58，升了链接出来的二进制在目标机照样跑不起来。
#
# 本增强版已把 src/meson.build 的依赖下限调回 2.58，并在 src/fsearch_compat.h 里为
# 0.3.2 真正用到的两个 2.62 新 API（g_ptr_array_copy / g_clear_signal_handler）
# 提供了语义等价的垫片实现（GLIB_VERSION_MAX_ALLOWED 条件编译）。
# 因此这里无需再改写源码，只需校验垫片文件在位，避免误用上游原版目录编译。
if [ ! -f "$SRC/src/fsearch_compat.h" ]; then
  echo "错误：源码里缺少 src/fsearch_compat.h（GLib 2.58 兼容垫片）。" >&2
  echo "     当前源码: $SRC" >&2
  echo "     请确认编译的是本增强版源码目录，而不是上游 fsearch-0.3.2 原版 tar 包。" >&2
  exit 1
fi
echo "  已找到 GLib 兼容垫片: src/fsearch_compat.h"

rm -rf build
meson setup build --prefix="$PREFIX" --buildtype=release
ninja -C build

echo "== [3/5] 安装到 DESTDIR（prefix=$PREFIX） =="
rm -rf "$DEST"
DESTDIR="$DEST" meson install -C build

BIN_REAL="$DEST$PREFIX/libexec/fsearch-bin"
LIBDIR="$DEST$PREFIX/lib"
mkdir -p "$LIBDIR" "$(dirname "$BIN_REAL")"

echo "== [4/5] 收集并打包运行依赖（离线安全；排除 glibc 核心库） =="
# 把二进制从 bin 挪到 libexec，bin/fsearch 改成启动器脚本
mv "$DEST$PREFIX/bin/fsearch" "$BIN_REAL"

copy_deps() {
  local f="$1"
  [ -e "$f" ] || return 0
  ldd "$f" 2>/dev/null | while read -r line; do
    # 形如：libfoo.so.0 => /usr/lib/.../libfoo.so.0 (0x...)
    local lib
    lib="$(printf '%s' "$line" | sed -n 's/.*=> \([^ ]*\) .*/\1/p')"
    [ -n "$lib" ] || continue
    local base; base="$(basename "$lib")"
    is_core "$base" && continue                       # 用目标机自带的 glibc
    case "$lib" in
      "$DEST$PREFIX"/*) continue ;;                  # 已经是我们自己的文件
    esac
    if [ ! -e "$LIBDIR/$base" ]; then
      cp -L "$lib" "$LIBDIR/"                         # -L 跟随符号链接，存实体
      copy_deps "$lib"                               # 递归收集它的依赖
    fi
  done
}
copy_deps "$BIN_REAL"
chmod 755 "$LIBDIR"/*.so* 2>/dev/null || true

# 启动器：设置 LD_LIBRARY_PATH 指向自带依赖，再执行真正的二进制
cat > "$DEST$PREFIX/bin/fsearch" <<'EOF'
#!/bin/sh
# FSearch 便携启动器（由 build_fsearch_uos20.sh 生成）
# 注意：/usr/local/bin/fsearch 是指向本脚本的软链，必须先用 readlink -f
# 解析出脚本真实路径，否则 HERE/ROOT 会算到 /usr/local 下而找不到二进制。
REAL="$(readlink -f "$0")"
HERE="$(cd "$(dirname "$REAL")" && pwd)"
ROOT="$(dirname "$HERE")"
# 告知被启动的 fsearch 自己的“启动器”路径，供自启动/右键菜单写出正确的 Exec= 入口
# （否则 /proc/self/exe 会指向 shell 解释器，导致这两个功能写出的命令失效）
export FSEARCH_LAUNCHER="$REAL"
export LD_LIBRARY_PATH="$ROOT/lib:$LD_LIBRARY_PATH"
exec "$ROOT/libexec/fsearch-bin" "$@"
EOF
chmod +x "$DEST$PREFIX/bin/fsearch"

# 顺手把 README 也打进包里，方便单位电脑上查看
cat > "$DEST$PREFIX/README.txt" <<'EOF'
FSearch 0.3.2 便携包（为统信 UOS 20 / Debian 10 编译）
- 直接运行：/opt/fsearch/bin/fsearch
- 或在 install_offline.sh 之后直接敲：fsearch
- 卸载：sudo rm -rf /opt/fsearch /usr/local/bin/fsearch
EOF

echo "== [5/5] 打包 =="
tar -C "$DEST" -czf "$OUT" .
echo
echo "完成！产物："
echo "  $OUT"
echo "大小： $(du -h "$OUT" | cut -f1)"
echo
echo "下一步："
echo "  1) 若需要 .deb（双击安装）：在同目录执行  bash build_deb.sh"
echo "  2) 把「输出」目录里的文件拷到离线单位电脑安装："
echo "     - .deb  → 双击安装（可能被『应用安全』拦一次）"
echo "     - tar.gz + install_offline.sh 放同一目录 → sudo bash install_offline.sh"
