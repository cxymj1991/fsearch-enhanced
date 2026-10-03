#!/usr/bin/env bash
# 右键菜单诊断脚本（在 UOS 20 上执行）
# 用法：bash 诊断右键菜单.sh
#
# 本脚本只读不写，不修改任何系统状态。

echo "=========================================================="
echo " FSearch 右键菜单诊断"
echo " 时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=========================================================="
echo

echo "【1】fsearch 是否已安装"
if [ -x /opt/fsearch/bin/fsearch ]; then
    echo "  ✓ /opt/fsearch/bin/fsearch 存在"
    echo "    wrapper 内容里的启动器路径: $(grep -o 'libexec/fsearch-bin' /opt/fsearch/bin/fsearch | head -1)"
else
    echo "  ✗ 未安装到 /opt/fsearch"
fi
echo "  /usr/local/bin/fsearch 软链: $(readlink -f /usr/local/bin/fsearch 2>/dev/null || echo '不存在')"
echo

echo "【2】是否已有 fsearch 进程在运行（会拦住单实例转发）"
if pgrep -a fsearch-bin >/dev/null 2>&1; then
    echo "  ⚠ 有进程在跑："
    pgrep -a fsearch-bin | sed 's/^/      /'
    echo "    → 右键测试前请先完全退出它（右键托盘图标 → 退出）"
else
    echo "  ✓ 没有进程在跑"
fi
echo

echo "【3】OEM 菜单文件是否已写入（dfm 5.x 只读系统级目录）"
OEM_SYS=/usr/share/deepin/dde-file-manager/oem-menuextensions
OEM_USR="$HOME/.local/share/deepin/dde-file-manager/oem-menuextensions"
for d in "$OEM_SYS" "$OEM_USR"; do
    echo "  --- $d"
    if [ ! -d "$d" ]; then
        echo "      目录不存在"
        continue
    fi
    ls -l "$d"/fsearch-search*.desktop 2>/dev/null | sed 's/^/      /' || echo "      没有 fsearch-*.desktop"
done
echo

echo "【4】若文件存在，核对 Exec 行（必须空格 + %u）"
for f in "$OEM_SYS"/fsearch-search*.desktop; do
    [ -f "$f" ] || continue
    echo "  --- $(basename "$f")"
    grep -E "^(Exec|MimeType|X-DFM-MenuTypes|X-DFM-SupportSuffix)=" "$f" | sed 's/^/      /'
done
echo

echo "【5】dfm 进程与热加载"
if pgrep -a dde-file-manager >/dev/null 2>&1; then
    echo "  ✓ dfm 在运行: $(pgrep -a dde-file-manager | head -1)"
    echo "    若菜单没出现，执行：killall dde-file-manager  # 5.x 只监听新增/删除，不监听内容变化"
else
    echo "  ⚠ dfm 没在运行（右键菜单测不了，先打开文件管理器）"
fi
echo

echo "【6】配置开关状态"
for cfg in "$HOME/.config/fsearch/fsearch.conf"; do
    if [ -f "$cfg" ]; then
        echo "  配置文件: $cfg"
        grep -E "context-menu|autostart" "$cfg" 2>/dev/null | sed 's/^/      /' || echo "      （没找到 context-menu 项，用默认值 true）"
    else
        echo "  ✗ 配置文件不存在: $cfg"
    fi
done
echo

echo "【7】上次右键触发记录"
LOG="$HOME/.cache/fsearch/last-search-in.log"
if [ -f "$LOG" ]; then
    echo "  --- $LOG"
    cat "$LOG" | sed 's/^/      /'
    echo
    if grep -q "search-in" "$LOG"; then
        if grep -qE "search-in (%[a-zA-Z]|/|\")" "$LOG"; then
            echo "  ✓ 看到 --search-in，说明右键菜单触发过"
        else
            echo "  ⚠ 日志里有 --search-in 但值可疑，请把上面内容发给我们"
        fi
    else
        echo "  ⚠ 日志里没有 --search-in —— 说明右键菜单【从未触发过】，"
        echo "     很可能菜单项压根没出现，或 dfm 没加载到 OEM 目录"
    fi
else
    echo "  ✗ 日志不存在，说明 fsearch 还没被启动过"
fi
echo

echo "=========================================================="
echo " 手动测试步骤："
echo "   1) 先从托盘完全退出 fsearch（避免单实例转发干扰）"
echo "   2) 在文件管理器里右键一个【真实文件夹】"
echo "      ⚠ 注意：不要在文件管理器里浏览 ~/桌面 再右键，"
echo "         dde-file-manager 在目录内不叠加 OEM 菜单（这是它的设计）"
echo "         测「文件夹快捷方式」要回到桌面图标上右键"
echo "   3) 看菜单里有没有「用 FSearch 搜索…」"
echo "   4) 有的话点它，然后执行：cat ~/.cache/fsearch/last-search-in.log"
echo "   5) 把脚本输出的【7】段内容发给我们"
echo "=========================================================="
