# FSearch 增强版

基于上游 [cboxdoerfer/fsearch](https://github.com/cboxdoerfer/fsearch) **0.3.2** 的增强版，
面向**统信 UOS 20**（Debian 10、GLib 2.58、GTK 3.24、X11）离线环境。

上游是一个干净但"只管搜索"的工具，本版本补齐了中文办公场景真正会用到的能力：
**托盘常驻、开机自启、文件管理器右键菜单、批量重命名、拖拽复制/剪切、全局快捷键**，
并提供**免联网的离线安装包**（便携 tar.gz / 可双击安装的 deb）。

---

## 一、比上游新增了什么

| 功能 | 说明 |
|---|---|
| **系统托盘常驻** | 关闭窗口后隐藏到托盘继续实时索引；托盘左键唤出窗口，右键菜单可显示/退出 |
| **开机自动启动** | 走 `systemd --user`，登录后仅驻留托盘。**不会**触发 UOS 的「是否允许开机启动」授权弹窗 |
| **文件管理器右键菜单** | 在 dde-file-manager 中新增「用 FSearch 搜索…」，覆盖三个场景：<br>· 选中文件夹<br>· 文件夹空白处<br>· 文件夹快捷方式（.desktop，自动解析出其指向的源目录） |
| **批量重命名** | 结果列表右键「批量重命名…」，支持查找替换 / 前后缀 / 序号 / 大小写 / 扩展名 / 删字符，可指定「仅第 N 个」「最后一个」「第 X 至第 Y 个」，带实时预览、冲突检测与撤销 |
| **拖拽复制 / 剪切** | 结果列表多选后可直接拖到文件管理器窗口或桌面，支持复制与剪切 |
| **全局唤起快捷键** | 自定义组合键（如 `Win+F`）全局唤起窗口，X11 有效 |
| **限定搜索范围** | 工具栏下拉框可切换：整个数据库 / 已选文件夹 / 选择文件夹… / 最近 10 次历史 |
| **窗口图标修复** | 任务栏显示 FSearch 图标而非默认齿轮（上游在部分桌面环境下显示异常） |
| **命令行选项** | `--search-in=<目录>` 限定搜索根、`--hidden` 仅启动托盘与索引 |
| **离线安装包** | 便携 tar.gz（解压即用）+ deb（双击安装），自带运行依赖，**不联网、不升级系统库、不替换系统自带文件** |

以上开关均可在 **首选项 → 界面** 中配置。

### 相对 0.3.1 增强版修复的问题

- 开机自启不再弹「是否允许 fsearch 开机启动」
- 文件夹快捷方式的右键菜单正常出现
- 任务栏图标正常显示
- 拖拽复制/剪切生效

> 详细的实现说明、踩坑记录与验证方法，见 [`docs/增强实现说明.md`](docs/增强实现说明.md)。

---

## 二、安装

到 **[Releases](https://github.com/cxymj1991/fsearch-enhanced/releases)** 页面下载最新版本，**目标机无需联网**。

### 方式 A：deb 双击安装（推荐）

1. 下载 `fsearch-0.3.2-1_amd64.deb`
2. 拷到 UOS 20 桌面，**双击**，按提示输入密码

安装后自动创建开始菜单与桌面入口，并可在首选项中开关右键菜单集成。

命令行等价操作：

```bash
sudo apt install ./fsearch-0.3.2-1_amd64.deb
```

### 方式 B：便携 tar.gz（免安装）

下载 `fsearch-0.3.2-uos20-portable.tar.gz`，以及本仓库
[`uos20-build/install_offline.sh`](uos20-build/install_offline.sh) 与
[`uos20-build/uninstall_offline.sh`](uos20-build/uninstall_offline.sh)，
把**三个文件放在同一目录**，然后：

```bash
sudo bash install_offline.sh
```

### 卸载

```bash
sudo apt remove fsearch           # deb 方式
# 或
sudo bash uninstall_offline.sh    # tar.gz 方式
```

卸载会一并清理桌面入口、软链与右键菜单配置，不留残留。

### 对系统的影响

- 程序文件只放在 `/opt/fsearch`，**不替换、不覆盖任何系统自带文件**
- 只新建 `/usr/local/bin/fsearch` 一个软链
- 右键菜单配置在 `/usr/share/deepin/dde-file-manager/oem-menuextensions/` 下新增 3 个文件，卸载时自动删除
- **不执行 `apt install`、不升级系统库、不改动 systemd 系统级单元**
- glibc 核心库刻意不打包，一律用目标机自带的，避免版本不匹配导致无法启动

---

## 三、自己编译（可选）

需要一台 **UOS 20 / Debian 10 虚拟机**（与目标机同版本，保证二进制 ABI 兼容）：

```bash
# 把整个源码文件夹拷到虚拟机桌面，保持 src/、uos20-build/ 与 一键编译.sh 平级
chmod +x 一键编译.sh
./一键编译.sh
```

双击 `一键编译.sh` 亦可。脚本会自动装编译依赖、编译、打包，
产物与日志统一收在源码文件夹内的 `输出/` 子目录，不会散落在桌面。

---

## 四、已知限制

1. **拖拽"剪切"取决于目标方是否识别**：dde-file-manager 主要读 `text/uri-list`，
   本程序通过 `GDK_ACTION_MOVE` 表达移动意图；若目标方忽略 action 而只按 uri-list 处理，
   结果会变成复制。GNOME 系（Nautilus）额外识别 `x-special/gnome-copied-files`，行为正常。
2. **全局快捷键仅 X11 有效**：UOS 20 默认 X11；若将来切换到 Wayland，该功能自动失效。
3. **deb 未做数字签名**：UOS 专业版 1060 起默认启用「仅允许签名应用」管控，
   双击安装可能被拦截。解除方式见 `uos20-build/README.md` 第三节，或直接改用方式 B。

---

## 五、许可与致谢

- 上游项目：[cboxdoerfer/fsearch](https://github.com/cboxdoerfer/fsearch)，GPL-2.0
- 本增强版同样以 **GPL-2.0** 发布，原 README 见 [`README.upstream.md`](README.upstream.md)
- 目标环境：统信 UOS 20 专业版（Debian 10、GLib 2.58.3、GTK 3.24、X11）
