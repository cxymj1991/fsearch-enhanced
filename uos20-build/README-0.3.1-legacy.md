# FSearch 0.3.1 离线安装方案（统信 UOS 20 / Debian 10）

## 你遇到的问题本质

作者提供的预编译包是针对 **Debian 11** 打的，它链接的是 Debian 11 里**更高版本号**的
系统库（如 `libgtk-3.so.0`、glib、pcre2、icu 等的新 SONAME）。而统信 UOS 20 基于
**Debian 10**，系统里只有旧版本号的同名库，所以直接装 Debian 11 的包会报"缺少依赖"。

单位电脑不能上网 → 没法 `apt install` 补依赖；也不能随便升级系统库 → 怕把系统搞崩。

## 解决思路

**在和 Target 同版本（UOS 20 / Debian 10）的虚拟机里重新编译**，得到的二进制会链接
本系统的库版本（Debian 10 的 SONAME），于是拷贝到单位电脑后，正好能用单位电脑上
**已经存在**的那些系统库运行。再把运行依赖一起打包进 tar 包（除 glibc 核心库外），做到
**完全离线、不联网、不动系统包管理器、不升级任何系统库**。

> 关键点：构建机与目标机必须是**同一版本**（UOS 20 或 Debian 10）。
> 纯 Debian 10 虚拟机也可以，UOS 20 虚拟机更稳妥（ABI 完全一致）。

## 流程

```
[能上网的 UOS20 虚拟机]                 [不能上网的单位电脑 UOS20]
源码 tar.gz ──► build_fsearch_uos20.sh
                 （apt 装【编译依赖】，meson 编译，
                  打包运行依赖，产出 tar.gz）
                        │
                        │ U 盘拷贝
                        ▼
                 install_offline.sh
                 （解包到 /opt/fsearch，建软链，
                  加桌面入口；不动 apt，不升级系统）
                        │
                        ▼
                      fsearch 可运行
```

## 步骤一：在能上网的 UOS 20 虚拟机里构建

1. 把单位电脑上那一份 `fsearch-0.3.1.tar.gz`（源码包）拷到虚拟机并解压：
   ```bash
   tar xzf fsearch-0.3.1.tar.gz
   ```
2. 把本目录下的 `build_fsearch_uos20.sh` 也拷进虚拟机，然后运行构建脚本。
   **源码定位支持三种方式（任选其一）：**
   ```bash
   # 方式 A：不传参 —— 自动在脚本同目录找 fsearch-0.3.1/ 或 fsearch-*.tar.gz（推荐）
   sudo bash build_fsearch_uos20.sh

   # 方式 B：传解压后的源码目录
   sudo bash build_fsearch_uos20.sh ./fsearch-0.3.1

   # 方式 C：传源码 tar 包（脚本会自动解压到临时目录再编译）
   sudo bash build_fsearch_uos20.sh ./fsearch-0.3.1.tar.gz
   ```
   > ⚠️ **不要照抄示例里的 `/path/to/fsearch-0.3.1`**——那是占位符，必须换成真实路径，
   > 否则脚本会报「找不到源码目录」。方式 A 最省事，脚本会自动找。

   脚本会：
   - 用 apt 装**编译依赖**（仅构建机需要联网）：meson、ninja、gcc、以及
     `libicu-dev libpcre2-dev libglib2.0-dev libgtk-3-dev` 等 `-dev` 包，
     外加 `appstream libxml2-utils yelp-tools`（生成 metainfo/帮助文档所需）；
   - **自动打 GLib 2.58 兼容补丁**：fsearch 0.3.1 用了 GLib 2.62 才有的
     `g_ptr_array_copy`，而 UOS 20 自带 glib 2.58.3。脚本在编译前会把两处调用
     替换成等价写法（语义不变），确保链接目标机自带的 glib 2.58、能在单位电脑上跑。
     **注意：不要去升级构建机的 glib**，否则产出的二进制在同样是 2.58 的单位电脑上反而跑不起来。
   - 用 meson + ninja 编译 fsearch 0.3.1；
   - 把二进制和它链接到的**运行依赖库**（除 glibc 核心库外）一起收集进
     `/opt/fsearch/{bin,libexec,lib,share}`；
   - 生成一个自带 `LD_LIBRARY_PATH` 的启动器；
   - 产出 `fsearch-0.3.1-uos20-portable.tar.gz`。

   > 如果虚拟机 apt 里的 meson 太旧（UOS20 自带 0.53，满足 ≥0.45，一般没问题），
   > 可用 `pip3 install --user meson ninja` 再构建。

## 步骤二：离线安装到单位电脑

1. 用 U 盘把 `fsearch-0.3.1-uos20-portable.tar.gz` 拷到单位电脑桌面。
2. 在终端里执行（需要 root，因为要写 `/opt` 和 `/usr/local/bin`）：
   ```bash
   sudo bash install_offline.sh ~/桌面/fsearch-0.3.1-uos20-portable.tar.gz
   ```
3. 直接运行：
   ```bash
   fsearch
   ```
4. 验证依赖是否齐全（应看不到任何 `not found`）：
   ```bash
   ldd /opt/fsearch/bin/fsearch | grep -i 'not found' || echo '依赖齐全，OK'
   ```

## 为什么这不影响单位电脑正常运行

- **不调用 apt / dpkg**，不安装、不升级任何 `.deb` 包，不触碰系统库。
- 所有文件只落在 `/opt/fsearch` 和 `/usr/local/bin/fsearch` 软链，以及
  当前**真实用户**家目录的 `~/.local/share/applications`（开始菜单入口）和
  `~/桌面/*.desktop`（桌面图标）。
  > 注意：`install_offline.sh` 必须用 `sudo` 运行（要写 `/opt`、`/usr/local/bin`），
  > 脚本会自动取真实用户名（`SUDO_USER`）把桌面入口装到你的家目录，而不是 root 的 `/root`。
- 启动器通过 `LD_LIBRARY_PATH` 优先用自带依赖；glibc 核心库仍用系统自带的，
  避免 glibc 版本冲突。
- **彻底可卸载**：`sudo bash uninstall_offline.sh` 即可（会按真实用户清理开始菜单
  入口、桌面图标、软件本体与软链），不留任何痕迹，对系统零侵入。
  卸载完成后会**交互式询问是否删除用户配置与搜索数据库**（`~/.config/fsearch`）；
  想一次性强制删除可加 `--purge`：`sudo bash uninstall_offline.sh --purge`。

## 新增功能：系统托盘（右下角图标）+ 后台实时索引

本次构建在 fsearch 0.3.1 基础上加入了**系统托盘图标**支持（基于 GTK 自带的
`GtkStatusIcon`，deepin/UOS 20 托盘兼容良好，且**无需额外依赖**）：

- **右下角托盘图标**：启动后，系统托盘（右下角）会出现 fsearch 图标。
- **左键单击托盘图标 = 直接弹出软件界面**：单击图标即可唤出/聚焦 fsearch 主窗口
  （等价于再敲一次 `fsearch` 命令）。
- **右键托盘图标 = 弹出菜单**：菜单里有「显示 FSearch」和「退出 FSearch」。
- **点 × 最小化到托盘**：默认开启。点击窗口右上角「×」时，窗口隐藏到托盘（任务栏不再
  显示窗口），但 **fsearch 仍在后台运行**，数据库索引/监控继续工作。
- **彻底退出**：右键托盘菜单的「退出 FSearch」、菜单的退出项、或快捷键 `Ctrl+Q` ——
  这些都会真正关闭程序，而不是最小化到托盘。

### 设置项

在 fsearch 偏好设置 → 界面里新增了一个勾选项：

> **关闭时最小化到系统托盘**（默认勾选）
> 提示：点击窗口关闭按钮时，隐藏到右下角托盘继续在后台运行（实时索引不受影响）。

如果取消勾选，点「×」就会像原版一样直接退出。

### 实现与依赖说明

- 托盘用 **GtkStatusIcon**（GTK3 内建，UOS 20 自带 gtk+-3.0 已包含），不再依赖
  libappindicator，因此离线包更小、也更省心（无需打包 libappindicator3/libindicator3/
  libdbusmenu-* 等运行库）。
- 极少数纯 SNI、且禁用传统系统托盘的桌面环境可能不显示 GtkStatusIcon；如在你的机器上
  看不到托盘图标，告诉我，我可改回 AppIndicator（SNI）方案（代价是左键/右键都只弹菜单）。

## 新增功能二：开机自启动 / 右键菜单

在「偏好设置 → 界面」里新增了两项开关，开箱即用、均可独立开关：

### 1. 开机自动启动（后台运行）

- **开关**：`开机自动启动（后台运行）`（默认关闭）。
- **效果**：勾选后，系统登录时会自动以**后台方式**启动 fsearch——
  仅驻留托盘图标并持续做实时索引，**不弹主窗口**；点托盘图标随时唤出界面。
- **实现**：在 `~/.config/autostart/fsearch.desktop` 写入自启动项，`Exec` 为
  `fsearch --hidden`（`--hidden` 即"仅托盘、不显示窗口"）。
  关闭开关则删除该文件。完全走标准 freedesktop 自启动规范，不碰任何系统配置。

### 2. 集成到右键菜单（用 FSearch 搜索此文件夹）

- **开关**：`集成到右键菜单（用 FSearch 搜索此文件夹）`（默认开启）。
- **效果**：在**文件管理器任意文件夹**上右键，出现「用 FSearch 搜索…」项；
  点击后 fsearch 启动，并把顶部「搜索范围」下拉框的当前项**设为该文件夹**，
  于是搜索结果**只限定在该文件夹（含子目录）内**（类似 Everything 的"在此文件夹中搜索"）。
- **顶部「搜索范围」下拉框**（搜索框左侧，开箱即用，每个窗口各自独立）：
  - 默认项是 **`整个数据库`**——即不限定，搜索全盘索引。
  - 选 **`选择文件夹…`** 会弹出**系统原生的文件夹选择对话框**（与「首选项-数据库-加号」完全一致：
    在 UOS20/deepin 上即系统文件选择器），默认定位到当前已选文件夹（或家目录）；选定后下拉框显示该文件夹路径，搜索限定于它；
    之后想换别的文件夹，直接再选 `选择文件夹…` 即可。
  - 想回到全盘，把下拉框切回 **`整个数据库`** 即可。
  - **最近选择历史**：每次用「选择文件夹…」或在文件管理器右键「用 FSearch 搜索」选定一个文件夹，
    都会记录进下拉框——显示在「选择文件夹…」**下方**，最多保留 **10 个**，**最近选的在最上面**。
    直接点历史里的路径即可一键切回该范围；当前已选文件夹不会在历史里重复出现。
    历史持久化在配置文件 `[Interface] recent_search_folders` 中，跨重启保留。
  - **清除历史记录**：下拉菜单最底部固定一项「清除历史记录」，点一下即清空最近选择历史（不改变当前搜索范围，无确认弹窗）。
  - **宽度限制**：「搜索范围」下拉框的显示宽度被限制为不超过当前窗口宽度的 **2/3**，
    路径很长时也不会把搜索框挤窄；窗口缩放时会自动重新计算上限。
  - 真正的"只搜此文件夹"由 `--search-in` 传入的搜索根目录（search_root）保证，与下拉框仅显示作用一致。
- **多窗口（类似 Everything）**：每个窗口独立、各自带自己的「搜索范围」下拉框和搜索词；
  **右键某个文件夹「用 FSearch 搜索」会打开一个全新的独立窗口**并把它限定到该文件夹，
  因此可以并排打开多个窗口、分别搜索不同文件夹与不同内容。也随时可通过菜单「新建窗口」或快捷键 `Ctrl+N`（仅 FSearch 窗口在前台时生效）再开一个空窗口。
- **实现**：利用 deepin/UOS `dde-file-manager` 的 **OEM 上下文菜单扩展**机制，
  在 `/usr/share/deepin/dde-file-manager/oem-menuextensions/fsearch-search.desktop`
  写入菜单项，`Exec` 为 `fsearch --search-in=%f`（**关键：必须是 `%f`，不是 `%U`/`%p`**：
  在 UOS 20 / deepin v20 的 `dde-file-manager` 里，对 `SingleDir`（右键文件夹）类型菜单，
  只有 `%f` 会被替换为该文件夹的真实路径；`%U`/`%p` 在 v20 上**不被替换**，会被原样当成参数传进来，
  导致 fsearch 收不到路径、退化成全盘搜索）。
  - 安装脚本已经把该文件创建并**归属给真实用户**，所以你在设置里开关此项时，
    fsearch 能直接增删该文件，**无需 root**。
  - 安装后若右键还没出现该项，重启一次文件管理器即可：
    `killall dde-file-manager`（再打开文件管理器）。
  - **排查**：若右键仍是全盘搜索，查看 `~/.cache/fsearch/last-search-in.log`，
    里面记录了 fsearch 实际收到的 `--search-in` 原始值、解析结果与**完整命令行 argv**；
    把内容发我即可精准定位（多半是 dde 没把 `%f` 替换成真实路径）。

### 2.1 文件夹「空白处」右键也能搜（EmptyArea）

- 除了在文件夹上右键，现在在文件管理器**空白处**右键也出现「用 FSearch 搜索…」，
  点击即把搜索范围限定到**当前正在浏览的这个文件夹**。
- 实现：安装脚本额外写入 `fsearch-search-blank.desktop`，
  `X-DFM-MenuTypes=EmptyArea`，`Exec=fsearch --search-in %u`。
- **为什么用 `%u` 而不是 `%p`**：dde-file-manager 5.x（统信 UOS 20 的底座）通过 libqtxdg
  解析 Exec 行，其 `expandExecString()` 里**根本没有 `%p` 分支** —— `%p` 会被原样传成字面量。
  而 5.x 的 `emptyAreaActoins()` 在此场景下把当前目录存进 action data，
  经 `QVariant::toStringList()` 转换后放进 urls 列表，`%u` 取 `urls.at(0)` 正好就是当前目录。
- 注意：选中文件夹用 `SingleDir`、空白处用 `EmptyArea`，两者是**两个独立菜单项**，
  安装/重装后会同时出现。
- 排查：每次右键都会写 `~/.cache/fsearch/last-search-in.log`，
  记录 dde 实际传入的原始值与解析结果，可据此判断字段码是否被正确展开。

### 2.2 选文件夹闪退的调试轨迹

- 顶部「搜索范围」下拉框选「选择文件夹…」会弹出**系统原生文件夹选择对话框**（与「首选项-数据库-加号」同一套实现，`GtkFileChooserNative`/`GtkFileChooserDialog`）。
- 若仍异常，流程每步会写入 `~/.cache/fsearch/scope-browse.log`（带时间戳）：
  `changed->browse` → `idle: start` → `idle: native dialog shown`（原生对话框由系统接管，无 populate 步骤）。
  → `response ACCEPT/CANCEL` → `set_scope`。**闪退前的最后一行就是崩溃点**，把该文件发我即可定位。

> 两项都保存在 fsearch 配置文件里，下次启动自动生效；"右键菜单"与"开机自启动"
> 的开关会在你保存设置时立即写/删对应系统文件。

## 新增功能三：自定义全局唤起快捷键

在「偏好设置 → 界面」里新增了**全局唤起快捷键**输入框（位于「集成到右键菜单」下方）：

- **作用**：设置一个系统级全局热键后，**无论 FSearch 是否在前台、是否仅驻留托盘**，
  按下该组合键都会**立即唤起 / 聚焦**已存在的 FSearch 窗口；若当前没有任何窗口（例如仅后台运行），则自动新开一个窗口。
  相当于给 FSearch 一个"随时一键呼出"的能力，不再需要去点托盘图标或切到桌面。
- **默认不启用**：留空即关闭，行为与原版一致。
- **填写格式**：标准 GTK 加速键字符串。常见写法：
  - `<Super>space` —— 推荐，Super（即 Win 键）+ 空格，最顺手；
  - `<Control><Alt>f` —— Ctrl+Alt+F；
  - `<Alt>F2`、`F10`、`<Control><Alt>s` 等均可。
  - 多个修饰键用 `<...><...>` 包裹，键名用小写（如 `space`、`f`、`Return`、`Escape`）。
- **即时生效**：保存偏好设置后立即重新注册热键，无需重启；空着保存即注销热键。
- **实现要点**：
  - 仅 **X11** 会话生效（UOS 20 / deepin v20 默认 X11）。若在 Wayland 下运行则整体为 no-op（日志会有 warning），需切到 X11 会话。
  - 通过 `XGrabKey` 在 X 根窗口抓取组合键，并用 GDK 根窗口事件过滤器捕获按键触发回调；
    对「无锁键 / NumLock / CapsLock / ScrollLock」共 8 种锁键状态各抓一次，开着 NumLock 也能触发。
  - 修饰键只剥离 NumLock/CapsLock/ScrollLock 三种"锁键"，保留 Shift/Ctrl/Alt/Super 等真实修饰键
    （否则 Super 组合键会被错误清零、抓不到键）。
  - 触发回调通过 `g_idle_add` 延后到主循环空闲再操作窗口，避免从事件过滤器内部重入 GTK 窗口 API。
- **注意**：若该组合键已被**窗口管理器或其它软件的全局快捷键**占用（例如某些桌面把 Super 键留给启动器），
  会被对方抢先、FSearch 收不到。遇到这种情况换一个组合键即可（比如改用 `<Control><Alt>f`）。
- **相关文件**：`src/fsearch_global_hotkey.c`（模块实现）、`fsearch_config.h/.c`（配置字段）、
  `fsearch_preferences_dialog.c/.ui`（设置入口）、`fsearch.c`（startup/shutdown/apply 接线）。

## 新增功能四：搜索结果批量重命名

- **入口**：在搜索结果列表**选中一个或多个文件**后，右键菜单新增「**批量重命名…**」。
- **规则**（可多选叠加，任一控件改动即时刷新预览）：
  - **查找替换**：把文件名中的文本替换为指定文本，可选**正则**模式；
    新增「出现次数」选项：**全部** / **仅第 N 个** / **第 X 至第 Y 个** / **仅最后一个**——
    例如把「变电站站址选择」中第二个「站」替换为「新站」：选「仅第」+ 2，结果为「变电站新站址选择」；
    若要把第 2 到第 3 个「站」都替换：选「第…至第…」+ 2 至 3。
  - **添加前缀 / 添加后缀**：在文件名前/后（后缀加在扩展名之前）插入文本；
  - **添加序号**：起始值、步长、位数（自动补零），位置可选「前缀 / 后缀」；
  - **大小写**：全大写 / 全小写 / 首字母大写 / 每个单词首字母大写（UTF-8 安全，中文不受影响）；
  - **修改扩展名**：整体替换扩展名，或直接删除扩展名；
  - **删除字符**：从第 N 个字符起删除 M 个字符（按字符计，中文一个字符算一个）。
- **预览与冲突检测**：对话框中部实时列出「原名称 → 新名称 → 状态」，
  自动标记三类冲突（目标文件已存在 / 新名非法含 `/` / 列表内重名），冲突项**红色**显示、执行时跳过。
- **执行与撤销**：点击「执行重命名」按顺序 `rename()`，弹窗汇总成功 / 失败 / 跳过数量；
  成功后对话框不关闭（可继续调规则），并出现「**撤销**」按钮——点击后把本次改名**全部还原**（反向 `rename()`）；
  数据库同步：移除旧路径条目、新条目由文件监控自动收录，撤销同理。
- **相关文件**：`src/fsearch_batch_rename.c/.h`（对话框与规则引擎）、
  `src/fsearch_window_actions.c`（注册 `win.batch_rename` action 与启用逻辑）、
  `src/menus.ui`（右键菜单项）、`src/meson.build`（源文件登记）。

## ⚠️ 最容易踩的坑：必须编译「本工作区改过的源码」，不是原版 tar.gz

这两项和托盘重写**都只在改过的源码里**，原版 `fsearch-0.3.1.tar.gz` 没有任何改动。
如果你编译/安装的是原版源码，就会看到：设置里没有那三个开关、开机不自启、全局快捷键无效、
右键菜单（安装脚本会单独写那个 `.desktop`，所以菜单项照常出现）点了没反应、而且托盘也退回旧行为。

**务必用本工作区 `src/fsearch-0.3.1/` 这个目录去编译**，不要解原版 tar.gz。

### 装完第一时间验证（确认跑的是新二进制）

```bash
fsearch --help      # 或 /opt/fsearch/bin/fsearch --help
```

新版本 `--help` 输出里**必须出现这两行**：

```
  --hidden           Start hidden in the background (tray only)
  --search-in=FOLDER Limit search to the given folder
```

- 如果**没有**这两行 → 说明跑的还是旧/原版二进制，请重新按下面流程编译并安装。
- 如果**有**这两行 → 新功能已就位，再去「偏好设置 → 界面」里就能看到三个开关。

> 说明：新二进制通过启动器脚本运行，`/proc/self/exe` 会指向 shell 解释器，
> 因此自启动/右键菜单写出的 `Exec=` 入口改用启动器脚本 export 的 `FSEARCH_LAUNCHER`
> （即 `/opt/fsearch/bin/fsearch`），保证带对 `LD_LIBRARY_PATH`、能正常启动。

## 常见问题

- **`ldd ... not found` 还有缺失库**：极少见（同版本系统库本应齐全）。把缺失的库名
  告诉我，可把它手动加进包的 `lib/` 目录，或直接 `apt install` 对应运行库（仅当该机能
  临时联网时）。
- **桌面/开始菜单没有快捷方式**：确认是用 `sudo bash install_offline.sh ...` 安装的。
  旧版脚本在 `sudo` 下会把 `.desktop` 装到 root 的 `/root` 家目录（看不到）；
  新版已改为按真实用户安装，并额外在 `~/桌面` 放一个可双击的图标。
  若仍不显示，注销重登录一次（让桌面环境重新扫描入口）。
- **想更省事/更隔离**：也可以把 tar 包改成 AppImage（在构建机用 linuxdeploy + gtk
  插件制作），单文件、双击即跑、无需解包。需要的话我可另出一版脚本。

## 文件清单

- `build_fsearch_uos20.sh` —— 在联网的 UOS20 虚拟机里运行，产出便携 tar 包。
- `install_offline.sh` —— 在单位离线电脑上运行，解包并创建启动入口。
- `README.md` —— 本说明。
