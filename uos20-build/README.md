# FSearch 0.3.2 增强版 · 打包与安装速查

面向统信 UOS 20 专业版（Debian 10）的离线构建与安装。

---

## 〇、目录结构（所有东西都在这一个文件夹里）

把 **`fsearch-0.3.2-enhanced/` 整个文件夹**放到虚拟机桌面，然后：

```
桌面/
└── fsearch-0.3.2-enhanced/          ← 只需拷这一个文件夹
    ├── 一键编译.sh                  ← ★ 双击这个
    ├── src/                         ← 源码
    ├── meson.build
    ├── uos20-build/                 ← 底层脚本（一般不用管）
    │   ├── build_fsearch_uos20.sh
    │   ├── build_deb.sh
    │   ├── install_offline.sh
    │   ├── uninstall_offline.sh
    │   └── README.md
    └── 输出/                         ← 脚本自动创建，产物都在这里
        ├── fsearch-0.3.2-uos20-portable.tar.gz
        ├── fsearch-0.3.2-1_amd64.deb
        └── 编译日志.log
```

**桌面上不会出现任何散落的编译产物**——便携包、deb、日志全部收在
`fsearch-0.3.2-enhanced/输出/` 里。编译完只需把这一个「输出」文件夹拷走。

---

## 一、编译：桌面双击即可

### 第 1 步：加执行权限（只做一次）

从 Windows 拷过去的 `.sh` 没有执行权限，双击会进文本编辑器。任选一种：

- **图形界面**：右键 `一键编译.sh` → 属性 → 勾选「允许作为程序执行」→ 确定
- **终端**：
  ```bash
  cd ~/桌面/fsearch-0.3.2-enhanced && chmod +x 一键编译.sh
  ```

### 第 2 步：双击 `一键编译.sh`

会依次完成：

1. 装编译依赖（首次需联网）→ 编译 → 打便携 tar.gz
2. 把 tar.gz 封装成 .deb

跑完后终端显示 🎉 全部完成，按回车关闭。
产物与日志都在 `输出/` 里，出问题看 `输出/编译日志.log`。

> 若提示「找不到 uos20-build 目录」，说明 `一键编译.sh` 没有和 `uos20-build/`
> 放在同一层——它必须位于源码文件夹里。

### 也可以用命令行编译

```bash
cd ~/桌面/fsearch-0.3.2-enhanced/uos20-build
sudo bash build_fsearch_uos20.sh   # 只出便携包
bash build_deb.sh                  # 再出 deb（不需要 sudo）
```

两个脚本都会把产物放进 `../输出/`，不会散落在当前目录。

---

## 二、安装到离线单位电脑

把虚拟机上 `fsearch-0.3.2-enhanced/输出/` 里的文件拷到单位电脑（U 盘即可），
然后三选一：

| 方式 | 需要拷哪些文件 | 操作 |
|---|---|---|
| **A（推荐）** | `fsearch-0.3.2-1_amd64.deb` | **双击**，按提示输密码 |
| B | 那个 deb | `sudo apt install ./fsearch-0.3.2-1_amd64.deb` |
| C | `.tar.gz` + `install_offline.sh` | 两个文件放同一目录后 `sudo bash install_offline.sh` |

> 方式 C 完全不碰 deb 机制，若单位不便开安全策略开关就选它。

**装完验证**：从开始菜单或桌面图标打开 FSearch，或命令行敲 `fsearch`。

**⚠️ 关于 UOS「应用安全」**：UOS 从专业版 1060 起默认启用「仅允许签名应用」管控，
未签名的 deb 双击时可能被拦，提示"没有通过系统安全认证"。
这**无法由本包规避**（是系统级安全策略）。若遇到，二选一：

- 启动器 → 搜索「安全中心」→ 安全工具 → 应用安全 → 点「允许任意应用」，同意声明后输密码；
- 控制中心 → 通用 → 打开「开发者模式」
  （⚠️ **专业版开启后不可关闭，请先与单位信息部门确认**）。

每次重装 deb 都需要重做一次这一步。

---

## 三、卸载

```bash
# deb 安装的：
#   图形界面：启动器 → 搜索「软件中心」→ 找到 FSearch → 卸载
sudo apt remove fsearch      # 命令行卸载，保留用户配置
sudo apt purge fsearch       # 连同用户配置与搜索数据库一并删除

# tar.gz 安装的：
sudo bash uninstall_offline.sh            # 交互式，询问是否删用户配置
sudo bash uninstall_offline.sh --purge    # 一并删除
```

卸载会清理：程序本体、命令软链、开始菜单/桌面图标、文件管理器右键菜单项、
systemd 自启动单元与 XDG autostart 残留。**不会动任何系统自带文件。**

---

## 四、.deb 与 tar.gz 的区别

| | tar.gz（方式 C） | .deb（方式 A/B） |
|---|---|---|
| 安装操作 | 需敲一条命令 | **双击即可** |
| 安装位置 | `/opt/fsearch` | `/opt/fsearch`（相同） |
| 是否替换系统文件 | 否 | 否 |
| 触发「应用安全」拦截 | 不会 | **可能**，需一次性放行 |
| 卸载 | 跑 `uninstall_offline.sh` | 软件中心一键卸载 |
| 断网可用 | 是 | 是 |

两者安装后的**程序行为完全一致**，因为文件布局与运行方式相同。

---

## 五、对系统的影响（可放心安装）

- 只往 `/opt/fsearch` 释放文件，**不替换、不覆盖任何系统自带文件或库**
- 只新建 `/usr/local/bin/fsearch` 一个软链
- 只在 `~/.config/`、`~/.local/share/`、`~/桌面/` 写用户级文件
- 只在 `/usr/share/deepin/dde-file-manager/oem-menuextensions/` **新增** 3 个
  `fsearch-search*.desktop`（并把该目录属主改为当前用户，便于在首选项里开关）
- **不执行 `apt install` 替换系统库、不升级 glibc/GTK、不动 systemd 系统级单元**
- glibc 核心库（libc / libm / libpthread / libdl / librt / libgcc_s /
  libstdc++ / ld-linux）刻意**不打包**，一律用系统自带的，
  避免 glibc 版本不匹配导致无法启动

---

## 六、常见问题

**Q：右键菜单没出现「用 FSearch 搜索…」**
A：先跑一键诊断脚本（只读，不改任何东西）：

```bash
cd ~/桌面/fsearch-0.3.2-enhanced/uos20-build
bash 诊断右键菜单.sh
```

它会逐项检查：是否装好、有无残留进程占着单实例、OEM 菜单文件是否落盘、
`Exec` 行是否为「空格 + `%u`」、dfm 是否在跑、配置开关状态、上次触发记录。
把输出发我们即可定位。

几个最常见的原因：

1. **fsearch 已在运行**（托盘常驻）→ 右键会走单实例转发，
   `~/.cache/fsearch/last-search-in.log` 里 `argv=` 只有程序路径而没有 `--search-in`。
   测之前先从托盘**完全退出** fsearch。
2. **dfm 未重新扫描** → dfm 5.x 只监听 OEM 目录的**文件新增/删除**，
   不监听内容变化。首次安装约 0.5 秒自动生效；若菜单没出现：
   ```bash
   killall dde-file-manager
   ```
3. **在文件管理器里浏览桌面目录时右键** → dde-file-manager 在进入目录后**不再叠加** OEM 菜单，
   这是它的设计。测「文件夹快捷方式」请**回到桌面图标上右键**。
4. 首选项里的「集成到右键菜单」被关掉了（默认是开的）→ 打开它，或直接改
   `~/.config/fsearch/fsearch.conf` 里的 `context-menu=true`。

**Q：编译时报「源码里缺少 src/fsearch_compat.h」**
A：说明编译的是上游原版 0.3.2 目录，不是增强版。请确认路径里有
`fsearch-0.3.2-enhanced/src/fsearch_compat.h`。

**Q：编译时 meson 报 GLib 版本不满足**
A：0.3.2 上游要求 GLib ≥ 2.62，而 UOS 20 是 2.58.3。本增强版已把
`src/meson.build` 下限调回 2.58 并提供 `src/fsearch_compat.h` 垫片，
正常不应出现；若出现请确认源码目录正确。

**Q：deb 双击没反应 / 提示无法打开**
A：多为"应用安全"拦截，见第三节的说明。

**Q：装完右键点了没反应**
A：看 `cat ~/.cache/fsearch/last-search-in.log`。
若 `argv` 里 `--search-in` 后面是字面量 `%f`/`%u`，把该文件发给我们。
