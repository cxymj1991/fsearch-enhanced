# FSearch 增强版（fsearch-enhanced）

基于 [FSearch 0.3.1](https://github.com/cboxdoerfer/fsearch)（原作者 Christian Boxdörfer）二次开发的增强版本，
专为**统信 UOS 20（Debian 10）**环境提供离线便携安装包，并在原版基础上新增了一批实用功能。

- 上游项目：<https://github.com/cboxdoerfer/fsearch>
- 本仓库版本规则：`0.3.1.x`，`x` 按增强批次递增（当前为 **0.3.1.1**）
- 开箱即用的离线安装包请在右侧 **Releases** 页面下载：
  `fsearch-0.3.1.1-uos20-portable.tar.gz`

---

## 增强功能一览（相对上游 0.3.1）

1. **系统托盘 + 关闭到托盘**
   - 启动后右下角显示托盘图标（GTK3 内建 `GtkStatusIcon`，无需额外依赖）；
   - 左键单击托盘图标唤出/聚焦主窗口；右键弹出菜单（显示 / 退出）；
   - 点窗口「×」默认隐藏到托盘，后台持续实时索引；`Ctrl+Q` / 菜单退出为彻底退出；
   - 偏好设置 → 界面 → 「关闭时最小化到系统托盘」开关。

2. **开机自启动（后台运行）**
   - 勾选后登录时以 `fsearch --hidden` 后台启动（仅托盘不弹窗），
     标准 freedesktop 自启动规范，写入 `~/.config/autostart/`。

3. **文件管理器右键集成（类 Everything「在此文件夹中搜索」）**
   - 在 UOS 20 / deepin 的 `dde-file-manager` 中右键文件夹或空白处，
     即可「用 FSearch 搜索此文件夹」，打开独立窗口并限定搜索范围；
   - 主窗口新增「搜索范围」下拉框：整个数据库 / 选择文件夹… / 最近 10 条历史 / 清除历史，
     多窗口各自独立，宽度自适应；
   - 通过 dde OEM 上下文菜单扩展机制实现，`SingleDir`+`%f` / `EmptyArea`+`%p`，
     并对占位符未替换的情况做了优雅降级（详见 `uos20-build/README.md`）。

4. **全局唤起快捷键**
   - 偏好设置中可配置系统级热键（如 `<Super>space`），随时一键呼出 FSearch；
   - X11 `XGrabKey` 实现，兼容 NumLock/CapsLock 等锁键状态，保存即时生效。

5. **搜索结果批量重命名**
   - 右键选中文件 → 「批量重命名…」；
   - 支持查找替换（含正则、指定第 N 个出现位置）、前缀/后缀、序号、大小写、
     扩展名修改、删除指定位置字符等规则，可叠加；
   - 实时预览「原名 → 新名 → 状态」，冲突（已存在/非法/重名）红色标记并跳过；
   - 执行后支持**一键撤销**全部改名，数据库同步更新。

6. **修复任务栏窗口图标**：窗口/任务栏正确显示 FSearch 图标（不再是齿轮）。

7. **UOS 20（Debian 10 / GLib 2.58）兼容**
   - 构建脚本自动打 GLib 2.58 兼容补丁（`g_ptr_array_copy` 等价替换）；
   - 离线便携包自带运行依赖（除 glibc 核心库外），不联网、不动系统包管理器即可安装。

---

## 快速开始（离线安装）

在 UOS 20（或 Debian 10）目标机器上：

```bash
# 解压 Releases 下载的便携包并安装（需要 root）
sudo bash install_offline.sh ~/桌面/fsearch-0.3.1.1-uos20-portable.tar.gz

# 运行
fsearch

# 验证依赖齐全（应无 not found）
ldd /opt/fsearch/bin/fsearch | grep -i 'not found' || echo '依赖齐全，OK'

# 卸载（交互式；--purge 一并删除配置与索引数据库）
sudo bash uninstall_offline.sh
```

> 安装包内已包含 `install_offline.sh` / `uninstall_offline.sh`，与本仓库 `uos20-build/` 目录一致。

## 从源码构建

详见 [`uos20-build/README.md`](uos20-build/README.md)（含完整流程、GLib 2.58 兼容说明、
新功能验证方法与常见问题排查）：

```bash
# 在能联网的 UOS 20 / Debian 10 虚拟机中
sudo bash uos20-build/build_fsearch_uos20.sh   # 自动定位源码并产出便携 tar 包
```

新二进制验证：`fsearch --help` 输出中应包含 `--hidden` 与 `--search-in=FOLDER` 两行。

## 目录结构

```
├── src/                 # 修改后的 FSearch 源码（含全部增强功能）
├── uos20-build/         # UOS 20 构建 / 离线安装 / 卸载脚本 + 详细说明
├── data/ po/ help/ ...  # 上游资源文件（保持原结构）
└── LICENSE              # GPLv2（继承上游）
```

## 许可证

继承上游 [FSearch](https://github.com/cboxdoerfer/fsearch) 的 **GNU GPL v2** 许可证。
所有增强改动同样以 GPL v2 发布。

## 致谢

- 原作者：Christian Boxdörfer（[cboxdoerfer/fsearch](https://github.com/cboxdoerfer/fsearch)）
- FSearch 是一款出色的 Everything 风格文件搜索工具，本仓库仅在其基础上做适配与增强。
