#!/usr/bin/env bash
# 右键菜单"点了没反应"的即时排查（只读，不改任何东西）
# 用法：bash 排查点了没反应.sh
#
# 目的：搞清楚 dde-file-manager 到底有没有真的把带参命令执行起来。
# 已知现象：点击菜单后没有任何窗口出现，且 last-search-in.log
#           里的 argv 只有程序路径、没有 --search-in（说明日志是旧记录）。

OEM=/usr/share/deepin/dde-file-manager/oem-menuextensions
LOG="$HOME/.cache/fsearch/last-search-in.log"

echo "=========================================================="
echo " 排查：右键菜单点了没反应"
echo " 时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=========================================================="
echo

echo "【A】先把旧日志挪走，避免误读"
if [ -f "$LOG" ]; then
    mv "$LOG" "$LOG.prev"
    echo "  已把旧日志改名成 $LOG.prev"
    echo "  （稍后若 $LOG 重新出现且带 --search-in，说明程序确实被调起了）"
else
    echo "  没有旧日志"
fi
echo

echo "【B】手工模拟 dfm 的调用（验证程序本身是否正常）"
echo "  即将执行：/opt/fsearch/bin/fsearch --search-in $HOME"
echo "  若目录被正确打开 → 程序与 --search-in 都没问题，问题在 dfm 侧"
echo
echo "  （5 秒后自动关闭。若弹出窗口并限定到你的家目录，说明程序是好的）"
echo
echo "  现在请你在【另一个窗口】里操作："
echo "    1) 打开文件管理器"
echo "    2) 右键任意文件夹"
echo "    3) 点「用 FSearch 搜索…」"
echo
echo "  本窗口会等你 30 秒，期间请完成上面三步。"
echo

sleep 30

echo
echo "【C】结果判定"
if [ -f "$LOG" ]; then
    echo "  ✓ 日志有更新 —— 说明 dfm 确实调起了程序，且传了参数："
    cat "$LOG" | sed 's/^/      /'
    echo
    if grep -q -- "--search-in /" "$LOG" || grep -qE "resolved_search_in=/" "$LOG"; then
        echo "  → 参数正确传入，目录也解析出来了。"
        echo "    若仍没窗口，问题在窗口创建环节，请把日志发给我们。"
    else
        echo "  → dfm 调起了程序，但没传有效路径（看上面 argv 的实际内容）。"
    fi
else
    echo "  ✗ 日志没有更新 —— 说明 dfm 调起程序时走了静默退出分支（在写日志之前就退了），"
    echo "    或者 dfm 压根没执行这条命令。"
    echo
    echo "    但注意：若 dfm 传的是字面量 %u，fsearch 会在解析失败后静默退出，"
    echo "    此时新版代码会记录日志。若这里日志没出现，说明："
    echo "      · dfm 没执行命令，或"
    echo "      · 装的是旧版二进制（日志写在静默退出之后）"
fi
echo

echo "【D】用进程与系统日志旁证"
echo "  · fsearch 是否被启动过（看启动时间）："
if pgrep -a fsearch-bin >/dev/null 2>&1; then
    echo "      正在运行: $(pgrep -a fsearch-bin | head -1)"
    echo "      → 若它在运行却没窗口，说明是托盘模式或窗口没创建出来"
else
    echo "      当前没有 fsearch 进程"
fi
echo
echo "  · dfm 自己的错误输出（最近 20 行 dfm 相关）："
if command -v journalctl >/dev/null 2>&1; then
    journalctl --user -b --since "5 min ago" 2>/dev/null | grep -i "dde-file-manager\|fsearch" | tail -20 | sed 's/^/      /' \
        || echo "      （journalctl 无权限或无记录）"
else
    echo "      无 journalctl"
fi
echo
echo "  · 桌面项能否手工执行（模拟 dfm 的方式）："
echo "      试这条，看会不会弹窗口："
echo "        /opt/fsearch/bin/fsearch --search-in '$HOME'"
echo

echo "=========================================================="
echo " 把本脚本【C】【D】两段的输出发给我们"
echo "=========================================================="
