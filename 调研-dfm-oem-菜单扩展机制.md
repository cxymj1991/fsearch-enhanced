# dde-file-manager OEM 菜单扩展机制调研报告

调研目标：让「文件夹快捷方式」（.desktop 快捷方式文件）的右键菜单也出现「用 FSearch 搜索」。

---

## 0. 结论速览（TL;DR）

| 问题 | 结论 |
|---|---|
| 根因 | `.desktop` 快捷方式在 dde-file-manager 眼中是**普通文件**，`X-DFM-MenuTypes=SingleDir` 的菜单项被过滤掉 |
| 推荐做法 | 新增一个 `X-DFM-MenuTypes=SingleFile` + `X-DFM-SupportSuffix=desktop` 的 OEM `.desktop`，`Exec` 传 `%f`（拿到 `.desktop` 文件自身路径），由 fsearch 解析 `URL=` / `Exec=` 还原目标目录 |
| 是否需要重启 | **不需要 killall**。源码里有 `QFileWatcher`，`subfileCreated` / `fileDeleted` / `fileAttributeChanged` 触发 500ms 延迟重载 |
| 风险 | 该菜单项会出现在**所有** `.desktop` 文件上（包括应用启动器），需在 fsearch 侧做「目标是目录才搜索」的兜底 |

---

## 1. `X-DFM-MenuTypes` 支持的取值（已查证）

### 1.1 取值白名单：只有 4 个

**已查证**（两份源码一致）：

- `src/dde-file-manager-lib/plugins/dfmadditionalmenu_p.h:36-41`（tag 5.2.45）
  ```cpp
  const QStringList AllMenuTypes {
      "SingleFile",
      "SingleDir",
      "MultiFileDirs",
      "EmptyArea"
  };
  ```
- `src/plugins/common/dfmplugin-menu/oemmenuscene/oemmenu.cpp:72-75`（master）
  ```cpp
  menuTypes << kEmptyArea
            << kSingleFile
            << kSingleDir
            << kMultiFileDirs;
  ```

| 取值 | 含义 |
|---|---|
| `SingleFile` | 选中单个**文件** |
| `SingleDir` | 选中单个**目录** |
| `MultiFileDirs` | 选中多个文件/目录（≥2） |
| `EmptyArea` | 空白区域 |

**关键：白名单机制。** `getValues()` 会把不在白名单里的值**全部剔除**：

`oemmenu.cpp:112-128`（master）
```cpp
QStringList OemMenuPrivate::getValues(const DDesktopEntry &entry, const QString &key,
                                      const QString &aliasKey, const QString &section,
                                      const QStringList &whiteList) const
{
    QStringList values(whiteList);
    if (entry.contains(key, section) || entry.contains(aliasKey, section)) {
        values = entry.stringListValue(key, section) + entry.stringListValue(aliasKey, section);
        if (whiteList.isEmpty())
            return values;
        for (const QString &value : values) {
            if (!whiteList.contains(value))       // ← 不在白名单里就移除
                values.removeAll(value);
        }
    }
    return values;
}
```
5.2.45 的 `getValues()` 逻辑相同（`dfmadditionalmenu.cpp:70-84`）。

### 1.2 明确**不存在**的取值（用户要求穷举的部分）

以下取值在源码白名单中**均不存在**，写了会被静默丢弃（不会报错，也不会生效）：

- `MultiFiles`（复数）— 旧版 JSON 扩展（v15）用过，但 OEM desktop 格式不支持
- `MultiDirs`
- `FileAndDir`
- `Trash` / `Disk` / `Desktop` / `Bookmark` / `Recent`
- `BlankSpace` — 这是 `.conf`（Menu Entry）格式的取值，`.desktop` 格式不支持

**注意**：`desktop deepin 开发者平台官方文档`中「Menu Entry（.conf 格式）」一节列出的 `MultiFiles / MultiDirs / FileAndDir / BlankSpace` 属于**另一套格式**（`/usr/etc/deepin/context-menus/*.conf`），与 `.desktop` OEM 格式**不通用**。见 <https://docs.deepin.org/info/开发入门/桌面环境/文件管理器> 的 Menu Entry 章节。
（状态：文档已查证；但 `.conf` 格式是否在 UOS 20 的 dfm 上启用，未在本轮源码中验证 —— 5.2.45 仓库中只有 `dfmadditionalmenu.cpp` 处理 `.desktop`，**没有** `.conf` 解析器。）

### 1.3 大小写敏感性

**已查证：区分大小写。** `whiteList.contains(value)` 是 `QStringList::contains` 的默认形式，即 `Qt::CaseSensitive`。

`"singledir"`、`"SingleDir "`（带空格）等都会导致**整项被剔除**。但分隔符 `;` 由 `DDesktopEntry::stringListValue` 处理，前后空格是否被 trim 我**未逐行确认**（5.2.45 用 `XdgDesktopFile::value().toString().split(';')`，**不 trim**，所以 `SingleDir; MultiDir;` 里 ` MultiDir` 是无效值，会被剔除）。

> ⚠️ 实践建议：字段值紧贴分号、不加空格 —— `X-DFM-MenuTypes=SingleFile;SingleDir`。

### 1.4 缺省行为

`getValues()` 中若 `whiteList` 非空且字段**不存在**，返回整个白名单（即 4 种类型全中）。
若字段**存在但全被剔除**，`loadDesktopFile()` 会记日志并跳过该文件（`oemmenu.cpp:483-488`）：

```cpp
menuTypes.removeAll("");
if (menuTypes.isEmpty()) {
    fmDebug() << "[OEM Menu Support] Entry will probably not be shown due to empty or have no valid"
              << kMenuTypeKey << " and " << kMenuTypeAliasKey << "key in the desktop file.";
    continue;
}
```

**已查证**（5.2.45 同样逻辑，`dfmadditionalmenu.cpp:250-255`）。

---

## 2. `MimeType=` 的作用与匹配规则

### 2.1 匹配规则（已查证）

`oemmenu.cpp:140-152`（master）：
```cpp
bool OemMenuPrivate::isMimeTypeMatch(const QStringList &fileMimeTypes,
                                     const QStringList &supportMimeTypes) const
{
    for (auto mt : supportMimeTypes) {
        if (fileMimeTypes.contains(mt, Qt::CaseInsensitive))   // 精确匹配（大小写不敏感）
            return true;

        int index = mt.indexOf("*");
        if (index >= 0 && isMimeTypeSupport(mt.left(index), fileMimeTypes))  // glob 模糊匹配
            return true;
    }
    return false;
}
```

| 特性 | 结论 | 依据 |
|---|---|---|
| 大小写 | **不敏感**（`Qt::CaseInsensitive`） | 同上 |
| glob 通配 | **支持**。`mt.indexOf("*")` → 取 `*` 前缀做 `contains` 子串匹配 | 同上 |
| `*` 位置 | 可在任意位置，`mt.left(indexOf("*"))` 作前缀子串匹配 | 同上 |
| 不写 `MimeType` | 等价于 `MimeType=*`，所有文件都匹配 | `oemmenu.cpp:614-618` |
| 写 `MimeType=;`（空） | 什么都不匹配，菜单不显示 | `removeAll({})` 后列表为空 → 循环不执行 → match=false |

所以 `MimeType=application/*;` **可用**（对应 `application/` 前缀子串匹配）。

### 2.2 父 MIME 类型自动包含（重要陷阱）

`oemmenu.cpp:580-587`（master）：
```cpp
QStringList fileMimeTypes, fmts;
fileMimeTypes.append(fileInfo->fileMimeType().name());
fileMimeTypes.append(fileInfo->fileMimeType().aliases());
const QMimeType &mt = fileInfo->fileMimeType();
fmts = fileMimeTypes;                       // ← 只含直接类型+别名
d->appendParentMineType(mt.parentMimeTypes(), fileMimeTypes);   // ← 追加所有父类型
```

- **`MimeType=`** 匹配用 `fileMimeTypes`：**含**父类型
- **`X-DFM-ExcludeMimeTypes=`** 匹配用 `fmts`：**不含**父类型

**这正是你现在遇到的问题的另一半！** 你的配置是 `MimeType=inode/directory;`，而快捷方式文件的直接类型是 `application/x-desktop`，其父类型链是 `application/x-desktop → application/octet-stream`（推测，见 §3.2），**不含** `inode/directory` → MimeType 过滤失败 → 菜单项消失。

### 2.3 过滤执行顺序（已查证，官方文档 `docs/extension/02-oem-menu-script.md` 也有）

1. MenuTypes 白名单筛选
2. `X-DFM-NotShowIn`（Desktop / Filemanager）
3. `X-DFM-SupportSchemes`（url.scheme()）
4. `X-DFM-SupportSuffix`（**目录直接跳过**）
5. `X-DFM-ExcludeMimeTypes`（仅直接类型+别名）
6. `MimeType`（含父类型）
7. 特殊：FTP 上移除 "Compress"；MTP `/mtp:host` 特殊处理

**任一不过 → 菜单项被 erase。** 这是「所有过滤都作用于同一个 actions 列表，且循环内 erase」的结构（`oemmenu.cpp:589-637`）。

---

## 3. 字段码：含义与快捷方式场景下的实际值

### 3.1 字段码定义（已查证，源码 + 官方文档）

| 字段码 | 含义 | 官方文档标注的适用场景 | 源码支持 |
|---|---|---|---|
| `%p` | 当前目录路径 | EmptyArea | ✅ |
| `%f` | 焦点文件**路径** | SingleFile/SingleDir | ✅ |
| `%F` | 所有选中文件路径（展开为多个参数） | MultiFileDirs | ✅ |
| `%u` | 焦点文件 URL | SingleFile/SingleDir | ✅ |
| `%U` | 所有选中文件 URL | MultiFileDirs | ✅ |
| `%d` | 右键所在文件夹名 | Menu Entry 格式 | ❌ **Desktop Entry 格式不支持** |
| `%b` | 焦点文件名（无扩展名、无路径） | Menu Entry 的 Name | ❌ 不支持 |
| `%a` | 焦点文件名（含扩展名） | Menu Entry 的 Name | ❌ 不支持 |

**源码依据（master 的 `oemmenu.cpp:51`）**：
```cpp
static const char *const kCommandArg[] { "%p", "%f", "%F", "%u", "%U" };
```
**只有这 5 个。**

**5.2.45 走的是另一条路径**（`dfmadditionalmenu.cpp:275-278`），用 `XdgDesktopFile::startDetached(files)`，字段码由 libqtxdg 展开。`libqtxdg/src/qtxdg/xdgdesktopfile.cpp:1204-1341` 的 `expandExecString()` 支持 `%f %F %u %U %i %c %k`，**而 `%d %D %n %N %v %m 被当作「已废弃」直接 `continue` 丢弃**（第 1325-1332 行）：

```cpp
// Deprecated.
// Deprecated field codes should be removed from the command line and ignored.
if (token == "%d"_L1 || token == "%D"_L1 ||
    token == "%n"_L1 || token == "%N"_L1 ||
    token == "%v"_L1 || token == "%m"_L1)
{
    continue;
}
```

→ **`%d` 在 UOS 20 上确定不可用，两条代码路径都不支持。**

### 3.2 右键 .desktop 快捷方式时，`%f` 替换成什么？

**已查证（master / 6.x）**：`oemmenuscene.cpp:145` + `oemmenu.cpp:434-451`
```cpp
QStringList OemMenuPrivate::applyDynamicArg(const QStringList &args, ArgType type,
                                            const QUrl &dir, const QUrl &focus,
                                            const QList<QUrl> &files) const
{
    switch (type) {
    case kDirPath:  return replace(cmdArgs, kCommandArg[type], dir.toLocalFile());
    case kFilePath: return replace(cmdArgs, kCommandArg[type], focus.toLocalFile());   // ← %f
    case kFilePaths:return replaceList(cmdArgs, kCommandArg[type], urlListToLocalFile(files));
    case kUrlPath:  return replace(cmdArgs, kCommandArg[type], urlToString(focus));
    case kUrlPaths:return replaceList(cmdArgs, kCommandArg[type], urlListToString(files));
    ...
```

`focus` = `selectFiles.first()`（`oemmenuscene.cpp:68-72`），即**被右键的那个文件的本地路径**。

**结论：`%f` = `/home/user/Desktop/我的文件夹.desktop`（.desktop 文件自身的路径）**

**5.2.45（UOS 20 实际走的路径）**：`dfmadditionalmenu.cpp:411-418` 把 `files`（DUrl 字符串列表）塞进 `action->setData(files)`，触发时 `dfmadditionalmenu.cpp:275-278`：
```cpp
connect(action, &QAction::triggered, this, [action, file]() {
    QStringList files = action->data().toStringList();
    file.startDetached(files);          // ← 传入的是 DUrl::toString()
});
```
`files` 来自 `dfilemenumanager.cpp:531-534`：`files << url.toString();` → 是 **URL 字符串**（如 `file:///home/user/Desktop/xxx.desktop`）。

而 libqtxdg 的 `%f` 展开（第 1236-1242 行）是：
```cpp
if (token == "%f"_L1) {
    if (!urls.isEmpty())
        result << expandEnvVariables(urls.at(0));   // ← 原样传 URL 字符串
    continue;
}
```
`%u` 才会尝试 `url.toLocalFile()` 转换（第 1256-1264 行）。

**⚠️ 重要版本差异**：
- **6.x（master）**：`%f` → `/home/user/Desktop/xxx.desktop`（纯路径）
- **5.x（UOS 20）**：`%f` → `file:///home/user/Desktop/xxx.desktop`（file URL）

→ **fsearch 侧必须同时兼容纯路径和 `file://` URL 两种形式**（剥离 `file://` 前缀 + URL 解码）。这一点很重要。（**补充**：后来发现 fsearch 增强版用 `--search-in=%f` 的写法在 5.x 上其实是完全失效的，见文末「重大修正」章节 —— 所以「对普通目录生效」这个前提本身不成立。）

### 3.3 是否有字段码能直接取到快捷方式指向的路径？

**未确证 / 结论：没有。** 5 种字段码（`%p %f %F %u %U`）全部指向「被右键对象自身」或「当前目录」，**没有任何字段码指向 `.desktop` 的 `URL=` 目标**。

已查证反面证据：`oemmenu.cpp` 与 `oemmenuscene.cpp` 中不存在任何解析 `.desktop` 内容/`URL=` 字段的逻辑；`%d` 虽在 `.conf` 格式的文档里被定义为「右键时所出的文件夹名」，但源码明确丢弃。

**推测（未验证）**：`Exec`/`%f` 拿到 `.desktop` 路径后，由 fsearch 自己读该文件解析 `URL=`（Type=Link）或 `Exec=`（Type=Application 目录启动器）→ 这就是推荐方案。

---

## 4. 文件夹快捷方式在 dde-file-manager 里被识别为什么类型？

### 4.1 结论：是**普通文件**，`menuType` 被判为 `SingleFile`

**已查证**（`dfmadditionalmenu.cpp:330-334`，tag 5.2.45）：
```cpp
if (files.count() == 1) {
    menuType = QFileInfo(DUrl(files.first()).toLocalFile()).isDir() ? "SingleDir" : "SingleFile";
} else {
    menuType = "MultiFileDirs";
}
```

**关键：`QFileInfo(path).isDir()` —— 对 `.desktop` 文件返回 `false`（它是普通文件），所以走 `SingleFile`。**

master（6.x）同样（`oemmenu.cpp:552-562`）：
```cpp
if (1 == files.count()) {
    auto fileInfo = DFMBASE_NAMESPACE::InfoFactory::create<FileInfo>(
        files.first(), Global::CreateFileInfoType::kCreateFileInfoAuto, &errString);
    if (!fileInfo) { return {}; }
    menuType = fileInfo->isAttributes(OptInfoType::kIsDir) ? kSingleDir : kSingleFile;
}
```

`InfoFactory::create<FileInfo>` 对 `.desktop` 后缀会返回 `DesktopFileInfo`（`desktopfileinfo.cpp:364-381` 的 `convert()`），而 `DesktopFileInfo` **没有覆盖 `isAttributes(kIsDir)`**，它继承 `ProxyFileInfo` 并代理到 `.desktop` 文件本身的 FileInfo → `isDir = false`。

**→ 这就是根因：`X-DFM-MenuTypes=SingleDir` + `MimeType=inode/directory` 双重失配。**

### 4.2 .desktop 快捷方式的 MIME 类型

**已查证：`application/x-desktop`**

- `src/dde-file-manager-lib/shutil/fileutils.cpp:1745-1749`（5.2.45）：
  ```cpp
  bool FileUtils::isDesktopFile(const QString &filePath)
  {
      QMimeType mt = DMimeDatabase().mimeTypeForFile(filePath);
      return mt.name() == "application/x-desktop" && mt.suffixes().contains("desktop", Qt::CaseInsensitive);
  }
  ```
  dfm 自己判定「是不是 desktop 文件」用的就是 `application/x-desktop`。

- **父 MIME 类型**：`application/x-desktop` 的父类型链在 shared-mime-info 中定义为 `application/x-desktop` → `application/octet-stream`。
  **状态：推测**（未直接读到 freedesktop.org.xml，被 GitLab/GitHub raw 阻断），但 dfm 源码的 `appendParentMineType` 逻辑（`oemmenu.cpp:404-432`）保证父类型会被展开并加入匹配列表。
  **实践影响：`MimeType=application/octet-stream;` 会误匹配所有 .desktop（可能还有更多）。所以不要用它。**

- **master 版已不再用 MIME 来判定 desktop 文件**，`fileutils.cpp:260-268` 改为纯后缀判断（注释自承 "It's not rigorous"）：
  ```cpp
  bool FileUtils::isDesktopFileSuffix(const QUrl &url)
  {
      // It's not rigorous,
      // but there are interfaces that call "isDesktopFile"
      // so often that it would be a performance loss to
      // create a fileinfo
      if (!url.toString().endsWith(".desktop"))
          return false;
      return !ProtocolUtils::isRemoteFile(url);
  }
  ```
  `mimetypedisplaymanager.cpp:143` 仍把 `application/x-desktop` 映射为 `FileInfo::FileType::kDesktopApplication`，说明该 MIME 名在 master 中依然有效。

  **⚠️ 由此推论（重要，已查证推导链）**：`DesktopFileInfo` 的**转换依据是后缀 `.desktop`，而 OEM `MimeType=` 的匹配依据是 MIME 数据库**。二者不是同一套判据。理论上若某系统 MIME 库缺失 `application/x-desktop` 的 glob 规则，`MimeType=application/x-desktop;` 就会失配。UOS 20 的 shared-mime-info 来自 freedesktop 标准包，该 glob 必然存在，所以方案 A 成立；但这说明**`X-DFM-SupportSuffix=desktop` 比 `MimeType=application/x-desktop;` 更稳健**（见 §6 方案 A 修订）。

### 4.3 dde-file-manager 对 .desktop 的右键菜单是否加载 OEM 扩展？

**已查证：是。** 关键代码在 `dfilemenumanager.cpp`：

`src/dde-file-manager-lib/interfaces/dfilemenumanager.cpp:509-522`（5.1.2 tag，结构与 5.2.45 一致）：
```cpp
QList<QAction *> DFileMenuManager::loadNormalPluginMenu(DFileMenu *menu, const DUrlList &urlList,
                                                        const DUrl &currentUrl, bool onDesktop)
{
    QStringList files;
    foreach (DUrl url, urlList) {
        files << url.toString();
    }
    ...
    QList<QAction *> actions;
    if (DFileMenuData::additionalMenu) {
        actions = DFileMenuData::additionalMenu->actions(files, currentUrl.toString(), onDesktop);
    }
    foreach (QAction *action, actions) {
        menu->insertAction(lastAction, action);
    }
```

调用点在 `createNormalMenu()` 末尾（`dfilemenumanager.cpp:512-514`），**无条件执行**（只有一个例外，见下）。

**唯一的 early-return 例外**（`dfilemenumanager.cpp:509-514`）：
```cpp
if (currentUrl == DesktopFileInfo::computerDesktopFileUrl()
        || currentUrl == DesktopFileInfo::trashDesktopFileUrl()
        || currentUrl == DesktopFileInfo::homeDesktopFileUrl()) {
    return menu;
}
```
这判断的是 **`currentUrl`（当前所在目录）**，而非被右键文件。这三个 URL 是 `~/Desktop/dde-{trash,computer,home}.desktop`（`desktopfileinfo.cpp:368-384`）—— 即「正在浏览桌面目录时不加载 OEM 菜单」。

**⚠️ 这个 early-return 很重要**：如果用户在文件管理器里浏览 `~/Desktop` 目录并右键里面的快捷方式，OEM 菜单**不会**加载（无论怎么配 MenuTypes）。这是「在桌面目录右键快捷方式没反应」的第二层原因。**但在桌面（dde-desktop）上右键图标是另一条代码路径，不受此限制。**

**master（6.x）** 走 menu scene 架构，`OemMenuScene` 作为 scene 插入到菜单链，无 early-return（`oemmenuscene.cpp` 全文未见 `return` 跳过 OEM 加载的条件）。

### 4.4 桌面（dde-desktop）上的快捷方式

**未完全确证。** master 中桌面图标由 `ddplugin-canvas` 渲染，快捷方式走 `DesktopFileInfo` / `MergedDesktopFileInfo`。`mergeddesktopfileinfo.cpp:65-68` 的 `VirtualEntryInfo::isDir()` 硬编码 `return true`（用于「主目录」「计算机」等虚拟项），但普通 `.desktop` 快捷方式走的是 `DesktopFileInfo`（`isDir` = false）。

**状态：推测** — 桌面上右键文件夹快捷方式，OEM 菜单项同样会被判为 `SingleFile`；但因 `onDesktop=true`，`X-DFM-NotShowIn=Desktop;` 会屏蔽它。你现有的配置没有 `NotShowIn`，所以理论上桌面也该出现 —— 值得实测。

---

## 5. 扫描路径与生效方式

### 5.1 扫描路径（已查证，两代不同！）

**5.x（UOS 20 实际）** — `dfmadditionalmenu.cpp:36`：
```cpp
#define MENUEXTENSIONS_PATH  "/usr/share/deepin/dde-file-manager/oem-menuextensions/"
```
**只有一个路径，硬编码，无用户级目录。** 5.1.2 与 5.2.45 完全一致（`v512_additionalmenu.cpp:34` 同）。

**master（6.x）** — `oemmenu.cpp:53-57, 68-70`：
```cpp
static QString oemMenuExtensionsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/deepin/dde-file-manager/oem-menuextensions");
}
// ...
oemMenuPath << QStringLiteral("/usr/etc/deepin/menu-extensions")
            << QStringLiteral("/etc/deepin/menu-extensions")
            << oemMenuExtensionsPath();
```

| 加载顺序 | 路径 | 5.x | 6.x |
|---|---|---|---|
| 1 | `/usr/etc/deepin/menu-extensions/` | ❌ | ✅ 直接加载 |
| 2 | `/etc/deepin/menu-extensions/` | ❌ | ✅ 直接加载 |
| 3 | `~/.local/share/deepin/dde-file-manager/oem-menuextensions/` | ❌ | ✅（`GenericDataLocation` = `~/.local/share`） |
| — | `/usr/share/deepin/dde-file-manager/oem-menuextensions/` | ✅ **唯一路径** | ⚠️ 官方文档称「不直接加载，通过自动同步机制分发到用户目录」 |

**⚠️ 关键结论：在 UOS 20（5.x）上，`~/.local/share/deepin/dde-file-manager/oem-menuextensions/` 不被扫描！必须写到 `/usr/share/deepin/dde-file-manager/oem-menuextensions/`（你现在用的路径是对的）。**

关于 `$XDG_DATA_HOME`：`QStandardPaths::GenericDataLocation` 在未设置 `XDG_DATA_HOME` 时等于 `~/.local/share`；设置后则跟随 `$XDG_DATA_HOME`。**这是 6.x 的行为，5.x 不适用。**

**⚠️ 6.x 的 `/usr/share/...` 同步机制：已定位到实现（完整 master 源码复验）**

实现在 `src/plugins/common/dfmplugin-menu/oemmenuscene/extensionmonitor.cpp`：

```cpp
static const char *const kOemMenuExtensionsPath = "/usr/share/deepin/dde-file-manager/oem-menuextensions";

ExtensionMonitor::ExtensionMonitor(QObject *parent) : QObject(parent)
{
    QString xdgDataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString oemDataPath = xdgDataHome + "/deepin/dde-file-manager/oem-menuextensions";
    extensionMap.insert(kOemMenuExtensionsPath, oemDataPath);   // /usr/share → ~/.local/share
}

void ExtensionMonitor::start()
{
    QTimer::singleShot(5000, this, [this] {      // ← 启动后 5 秒才做首次同步
        copyInitialFiles();
        setupFileWatchers();
    });
}
```

**⚠️ 但这个机制对文件管理器进程不生效！** `menu.cpp:230-235`：

```cpp
bool Menu::start()
{
    const auto &appName = qApp->applicationName();
    if (appName == "org.deepin.dde-shell" || appName == "dde-desktop")   // ← 只有桌面进程
        ExtensionMonitor::instance()->start();
    return true;
}
```

**`ExtensionMonitor` 只在 dde-desktop / dde-shell 进程里启动，文管进程（dde-file-manager）不启动它。**

由此得出 6.x 的实际行为（对部署方案有直接影响）：

| 场景 | `/usr/share/...` 里的文件能否生效 |
|---|---|
| 在**文管**里浏览目录 | ❌ **不能**。文管只扫 `~/.local/share/...`（`oemmenu.cpp:68-70` 三个路径不含 `/usr/share`），且不做同步 |
| 在**桌面**上右键 | ✅ 能。但需 dde-desktop 已启动 ≥5 秒，且该文件已被复制到 `~/.local/share/...` |

**这个同步机制还有两个缺陷（会导致菜单不出现）：**

1. **只复制、不覆盖**（`extensionmonitor.cpp:80-81`）：
   ```cpp
   if (QFile::exists(targetFile))
       continue;                    // ← 目标已存在就跳过
   ```
   → 若 `~/.local/share/.../fsearch-shortcut.desktop` 已存在旧版本，`/usr/share` 的新版本**永远不会覆盖它**。升级 fsearch 后菜单不更新就是这个原因。

2. **不同步删除**（同上，`processExtensionDirectory` 只做 copy）→ 卸载软件包后 `~/.local/share` 里的残留文件会永久存在。

3. **延迟 5 秒**（`QTimer::singleShot(5000, ...)`）→ dde-desktop 刚启动的 5 秒内创建的 `/usr/share` 文件不会被同步（但之后的 `subfileCreated` 会补上）。

**→ 对本项目（UOS 20 / 5.x）的结论不变**：写 `/usr/share/deepin/dde-file-manager/oem-menuextensions/`，这是 5.x 唯一被扫描的路径。若未来要兼容 6.x，**必须同时写 `~/.local/share/deepin/dde-file-manager/oem-menuextensions/`**（因为 6.x 文管不扫 `/usr/share`，同步器只在桌面进程跑）。

### 5.2 是否需要 killall dde-file-manager？

**已查证：不需要。有热加载。**

5.2.45 `dfmadditionalmenu.cpp:48-68`：
```cpp
DFMAdditionalMenuPrivate::DFMAdditionalMenuPrivate(DFMAdditionalMenu *qq)
    : q_ptr(qq)
{
    m_delayedLoadFileTimer = new QTimer(qq);
    m_delayedLoadFileTimer->setSingleShot(true);
    m_delayedLoadFileTimer->setInterval(500);        // ← 500ms 防抖
    QObject::connect(m_delayedLoadFileTimer, &QTimer::timeout, qq, &DFMAdditionalMenu::loadDesktopFile);
    DUrl url = DUrl::fromLocalFile(MENUEXTENSIONS_PATH);
    DAbstractFileWatcher *dirWatch = DFileService::instance()->createFileWatcher(qq, url, qq);
    if (dirWatch) { dirWatch->startWatcher(); }
    QObject::connect(dirWatch, &DAbstractFileWatcher::fileDeleted,      m_delayedLoadFileTimer, [=](){ m_delayedLoadFileTimer->start(); });
    QObject::connect(dirWatch, &DAbstractFileWatcher::subfileCreated,  m_delayedLoadFileTimer, [=](){ m_delayedLoadFileTimer->start(); });
}
```

master 增加了第三个信号（`oemmenu.cpp:92-100`）：
```cpp
QObject::connect(watcher, &LocalFileWatcher::fileDeleted,          delayedLoadFileTimer.data(), [=](){ delayedLoadFileTimer->start(); });
QObject::connect(watcher, &LocalFileWatcher::subfileCreated,      delayedLoadFileTimer.data(), [=](){ delayedLoadFileTimer->start(); });
QObject::connect(watcher, &LocalFileWatcher::fileAttributeChanged, delayedLoadFileTimer.data(), [=](){ delayedLoadFileTimer->start(); });
```

| 操作 | 是否触发重载 | 延迟 |
|---|---|---|
| 新增 `.desktop`（`subfileCreated`） | ✅ | 500ms |
| 删除 `.desktop`（`fileDeleted`） | ✅ | 500ms |
| **修改已有 `.desktop` 内容** | 5.x ❌ / 6.x ✅ | 500ms |

**⚠️ 5.x 的坑：修改已有 OEM 文件的内容不会触发重载！** 只监听 created/deleted。所以调试时必须「删除旧文件 + 写入新文件」（或 `mv` 覆盖，新 inode 视为 created）才能生效。

**5.x 没有监听父目录不存在的情况** —— `createFileWatcher` 对不存在的目录返回空，`if (dirWatch)` 直接跳过，此后新建目录也不会被监听。**安装脚本必须先 `mkdir -p` 再放文件。**

**保守兜底**：官方 wiki（转述于 <https://www.listera.top/...>）注 1 明确写「菜单列表仅在文件管理器启动时识别并记录一次…您可能需要结束所有文件管理器进程」。这与源码的热加载机制**不完全一致**（源码明显有 watcher），但说明实际环境中可能存在 watcher 失效的情况（多进程、加速启动的常驻进程）。

**建议：安装脚本里加 `pkill -f dde-file-manager` 作为兜底，但要在脚本注释里说明「正常情况下不需要」。**

---

## 6. 可选做法与评估

### 方案 A（推荐）：新增 `SingleFile` + `MimeType=application/x-desktop` 的 OEM 项

**做法**：新增一个 `.desktop`（不要改现有的），路径 `/usr/share/deepin/dde-file-manager/oem-menuextensions/fsearch-shortcut.desktop`：

```ini
[Desktop Entry]
Type=Application
Name=Search with FSearch…
Name[zh_CN]=用 FSearch 搜索…
X-DFM-SupportSuffix=desktop;
Exec=/opt/fsearch/bin/fsearch --search-in %f
Terminal=false
X-DFM-MenuTypes=SingleFile
X-DFM-SupportSchemes=file;
X-DFM-NotShowIn=Desktop;
```

> **⚠️ 修订（基于完整 master 源码复验）**：初版建议用 `MimeType=application/x-desktop;`。现改为推荐 `X-DFM-SupportSuffix=desktop;`，理由见下方「方案 A 修订说明」。若两者都写，则需**同时**满足才显示（`isValid()` 与 MimeType 检查是 AND 关系，`oemmenu.cpp:589-634`）；实测若发现菜单不显示，优先怀疑二者 AND 关系导致。

同时 fsearch 侧新增参数处理：接收一个 `.desktop` 文件路径 → 解析 `URL=`（Type=Link）或 `Exec=`（Type=Application 且指向目录）→ 还原目标目录 → 搜索。

**可行性评估**：

| 问题 | 结论 | 依据 |
|---|---|---|
| Exec 能否拿到被点击的 .desktop 文件路径？ | ✅ **能**。`%f` = focus 的本地路径 | `oemmenu.cpp:441` `case kFilePath: return replace(cmdArgs, "%f", focus.toLocalFile())` |
| `SingleFile` 能否命中？ | ✅ **能**。快捷方式 `isDir=false` → menuType=`SingleFile` | `dfmadditionalmenu.cpp:331` `QFileInfo(...).isDir() ? "SingleDir" : "SingleFile"`；master `oemmenu.cpp:559` 同理 |
| `DesktopFileInfo` 会不会把 `isDir` 改成 true？ | ❌ **不会**，故上条成立 | `desktopfileinfo.cpp` 全文**无** `isAttributes` / `isDir` / `fileMimeType` 覆写；`ProxyFileInfo::isAttributes` 直接 `CALL_PROXY` 转发给被代理的 `.desktop` 文件本身（`proxyfileinfo.cpp:360-365`） |
| `X-DFM-SupportSuffix=desktop` 能否命中？ | ✅ **能**，且比 MimeType 更稳健 | `oemmenu.cpp:196-217`，`supportList.contains(cs, CaseInsensitive)`，`cs` 取 `kCompleteSuffix` |
| 5.x 下 `%f` 是路径还是 URL？ | ⚠️ **是 `file://` URL**，需剥离 | `dfilemenuanager.cpp:533` `files << url.toString()` + libqtxdg `%f` 原样传出 |
| 桌面（dde-desktop）上生效？ | ⚠️ **加了 `NotShowIn=Desktop` 就不会生效** | 建议：若希望桌面也支持，去掉 `NotShowIn=Desktop` |

#### 方案 A 修订说明：为何改用 `X-DFM-SupportSuffix=desktop`

完整 master 源码复验后发现的推导链：

1. `DesktopFileInfo` 的转换依据是**后缀** `.desktop`（`fileutils.cpp:260-268` `isDesktopFileSuffix`）
2. OEM `MimeType=` 的匹配依据是 **MIME 数据库**（`fileMimeType().name()` + aliases + 父类型）
3. 两者是**不同的判据**。虽然 UOS 20 的 shared-mime-info 必有 `*.desktop → application/x-desktop` 的 glob（这是 freedesktop 标准，5.2.45 的 `fileutils.cpp:1748` 就依赖它），但 `X-DFM-SupportSuffix` 直接走字符串比较，**不依赖 MIME 库，零风险**

另外 `oemmenu.cpp:196-206` 显示后缀判定对 `7z.*` 这类会回退到 `kCompleteSuffix`，`.desktop` 是单一后缀，无此复杂度。

**建议**：以 `X-DFM-SupportSuffix=desktop;` 为主。若要双保险，可同时写 `MimeType=application/x-desktop;`，但**必须实机确认 AND 语义下菜单仍能显示**。

**风险与不足**：

1. **菜单污染**：所有 `.desktop` 文件（含应用启动器）都会显示「用 FSearch 搜索」。必须在 fsearch 侧判断「目标是否为目录」，非目录则静默退出（不弹错误）。这是**必做项**。
2. **`%f` 的路径/URL 双重形态**：5.x 是 `file://` URL，6.x 是裸路径。fsearch 需同时兼容。
3. **解析逻辑需自建**：要处理 `URL=file:///path/`（含 URL 编码、`%20` 等）和 `Exec=dde-file-manager -O /path`（含 `-O` 参数、`xdg-open` 等多种形态）。deepin 创建的目录快捷方式实际是 `Type=Link` + `URL=file:///...`（推测，**需实测确认**）。
4. **快捷方式失效**：目标目录已删除时无法搜索，应给出友好提示或静默失败。
5. **5.x 修改内容不热重载**：调试需删+建。
6. **多选场景**：若用户同时选中 `.desktop` 和普通文件，`menuType` 变为 `MultiFileDirs`，此菜单项不出现 —— 这是**期望行为**（无法确定对哪个目标搜索）。

**风险等级：中低**（可控，主要是实现解析逻辑）。

---

## ⚠️ 重大修正（2026-10-02，交叉核对后）：`--opt=%f` 写法在 dfm 5.x 上完全失效

**本报告 §6 方案 A 给出的 `Exec=... --search-in=%f` 配置是错的**，会与 `%p` 一样失效。以下为查证过程。

### 根因：libqtxdg 只认「独立的字段码 token」，不支持子串拼接

`xdgdesktopfile.cpp` 的 `expandExecString()` 对字段码是**精确相等比较**，不是子串替换。三个版本一致：

| 版本 | 行号 | 代码 |
|---|---|---|
| 3.8.0 | 1120 | `if (token == QLatin1String("%f"))` |
| 3.12.0 | 1214 | `if (token == QLatin1String("%f"))` |
| master | 1237 | `if (token == "%f"_L1)` |

而 `parseCombinedArgString()` 的切分规则（3.8.0 第 1028-1035 行）：

```cpp
if (!inQuote && program.at(i).isSpace()) {    // ← 只在引号外按【空白】切分
    if (!tmp.isEmpty()) { args += tmp; tmp.clear(); }
} else {
    tmp += program.at(i);
}
```

**`=` 不是空白，所以 `--search-in=%f` 被切分成一个完整 token**，它 `!= "%f"` → 落到函数末尾的兜底 `result << expandEnvVariables(token)` → **原样传给程序**。

fsearch 实际收到的 argv 是 `["fsearch", "--search-in=%f"]`，GOption 正常解析为「选项 `search-in`，值 `%f`」，随后 `normalize_search_root()` 因 `strchr(raw, '%')` 命中而返回 NULL。

### 已确认 dfm 5.2.45 走的是这条路径

- `dfmadditionalmenu.cpp:31` `#include <XdgDesktopFile>`
- 触发时 `dfmadditionalmenu.cpp:275-278` 调 `file.startDetached(files)`
- **全文无自研字段码替换**：grep `applyDynamicArg` / `replaceList` / `execDynamicArg` 结果为 **0**（这三个函数是 6.x `oemmenu.cpp` 才有的）

### 与 6.x 的对比（这就是两代行为不同的原因）

6.x 走自研 `applyDynamicArg()`，用 `QString::replace()` 做**子串替换**：

```cpp
QStringList OemMenuPrivate::replace(QStringList &args, const QString &before, const QString &after) const
{
    ...
    int index = arg.indexOf(before);        // ← indexOf，子串匹配
    if (index >= 0) {
        rets << arg.replace(index, before.size(), after);
```

**所以 `--search-in=%f` 在 6.x 上能用，在 5.x 上不能用。** 官方 OEM 文档（`docs/extension/02-oem-menu-script.md`，随 6.x 发布）里的示例 `Exec=/usr/bin/tool --file=%f` 是按 6.x 行为写的，照搬到 UOS 20 会失效。

### 正确写法：`=` 改空格

```ini
Exec=$PREFIX/bin/fsearch --search-in %f
```

`%f` 成为独立 token → 命中精确比较 → 正常展开。

⚠️ 注意：`%F` / `%U` 必须**作为独立参数**（`replaceList()` 的注释明确写了「only independent parameters are supported, and other combinations are not processed」，`oemmenu.cpp:362-379`）。`--search-in %F` 在多元素时会展开成 `fsearch --search-in file1 file2`，`file2` 变成游离位置参数。

### 规范依据

XDG Desktop Entry Specification 1.5：

> The `%F`, `%U` and `%i` field codes may only be used as an argument on their own.

即**规范本身就要求字段码独立成参数**，`--opt=%f` 属违规写法（GLib/GNOME 宽松支持，Qt/KDE 系不支持）。

### 本报告受影响的结论

| 位置 | 原结论 | 修正 |
|---|---|---|
| §6 方案 A 配置 | `Exec=... --search-in=%f` | ❌ 改为 `Exec=... --search-in %f` |
| §3.1 字段码表 | 「`%f` 适用于 SingleFile/SingleDir」 | ✅ 结论不变，但**必须独立成参数** |
| §3.1 关于 `%p` | 「5.x 走 libqtxdg，`%p` 被丢弃」 | ✅ 结论不变（`%p` 是完全未识别的 token，与 `--opt=%p` 无关） |
| §0 速览表推荐配置 | 含 `--search-in=%f` | ❌ 需改为空格形式 |

**注**：`%p` 那个坑和 `--opt=%f` 这个坑是**两个独立问题**，但在同一份配置里同时出现，症状都是「右键后行为异常」，容易只查一个。0.3.1 同时踩了两个（`%p` + `=`），0.3.2 修了 `%p` 但仍踩 `=`。

---

### 方案 B：找 deepin/UOS 特有字段码直接取目标路径

**结论：❌ 不可行。** 已查证字段码只有 `%p %f %F %u %U`（`oemmenu.cpp:51`），`%d %D %n %N %v %m` 在 libqtxdg 中被明确标记为 deprecated 并丢弃（`xdgdesktopfile.cpp:1325-1332`）。**不存在能取到 `.desktop` 目标路径的字段码。**

---

### 方案 C：换用 `.conf`（Menu Entry）格式

官方文档（<https://docs.deepin.org/info/开发入门/桌面环境/文件管理器> 的 Menu Entry 章节）描述了 `.conf` 格式，支持更多 MenuTypes 取值（`SingleFile/MultiFiles/SingleDir/MultiDirs/FileAndDir/BlankSpace`）和 `Name` 中的 `%d %b %a` 动态参数。

**⚠️ 但 5.2.45 仓库中只有 `dfmadditionalmenu.cpp`（处理 `.desktop`），没有任何 `.conf` 解析器。** 社区博客（<https://blog.silence.pink/p/uos-dfm-context-menu>）提到 UOS 20 上 `.conf` 可用，但该作者自己标注「dde-file-manager 5.2.45」并最终选用 `.conf`——**与源码证据矛盾。**

**状态：未确证，倾向不可行。** 若要尝试需先在实机验证。

---

### 方案 D：改用 dfm-extension C++ 插件（`DFMExtMenuPlugin`）

接口签名（社区博客 <https://blog.silence.pink/p/uos-dfm-context-menu> 引用官方示例）：
```cpp
bool buildNormalMenu(DFMExtMenu *main, const std::string &currentUrl,
                     const std::string &focusUrl,
                     const std::list<std::string> &urlList, bool onDesktop);
```

**优点**：可在 C++ 里读 `DesktopFileInfo` 拿 `Exec` / `URL` 字段，逻辑更直接；不受 MenuTypes 白名单限制。
**缺点**：
- 需 `dde-file-manager >= 5.5.10`（**UOS 20 的 5.2.45 不满足，此路直接不通**）
- 需编译 C++ 动态库放 `/usr/lib/<arch>/dde-file-manager/plugins/extensions`
- 复杂度远高于方案 A

**状态：UOS 20 上不可行。**

---

### 方案 E（补充）：同时为 `EmptyArea` 加一个「搜索当前目录」

顺带提示：现有 `X-DFM-MenuTypes=SingleDir` 那条只覆盖目录。若想覆盖空白区域需另建 `EmptyArea` + `%p` 的项。**注意 `%p` 在 5.x 走 libqtxdg 路径时不支持**（libqtxdg 的 `expandExecString` 只处理 `%f %F %u %U %i %c %k`，`%p` 会**原样传给程序**作为字面量 `%p`！）。

⚠️ **这是一个独立的严重坑**：`EmptyArea` + `Exec=xxx %p` 在 5.x 上，`%p` 不会被替换，程序会收到字面字符串 `%p`。而 master（6.x）用自研的 `applyDynamicArg` 正确处理了 `%p`（`oemmenu.cpp:438-439`）。

**如果 fsearch 增强版里有 EmptyArea 菜单项用了 `%p`，在 UOS 20 上它是坏的 —— 建议改用 `%F`/`%f`（空选中时为空列表）配合 `-p` 显式参数，或直接放弃 EmptyArea 项。**

---

## 7. 推荐做法与备选

### 推荐：方案 A

1. 保留现有 `fsearch-dir.desktop`（`SingleDir` + `inode/directory`）不动 —— 它对真目录工作正常
2. 新增 `fsearch-shortcut.desktop`（`SingleFile` + `X-DFM-SupportSuffix=desktop`），`Exec` 传 `%f`
3. fsearch 侧加一层「参数归一化 + 快捷方式解析」：
   - 剥离 `file://` 前缀并 URL 解码 → 得到 `.desktop` 绝对路径
   - 若该路径是目录（`-d` 判定）→ 现有逻辑直接搜索
   - 若以 `.desktop` 结尾 → 读文件，解析 `URL=`（Type=Link）；若为空则解析 `Exec=`（Type=Application），提取目录参数
   - 目标不是目录 / 目标不存在 → **静默退出，不弹错误**
4. 安装脚本：`mkdir -p` 目标目录 → 写入文件；调试期加 `pkill -f dde-file-manager` 兜底（因 5.x 修改内容不热重载）

**若未来要兼容 dfm 6.x**：必须**额外**写一份到 `~/.local/share/deepin/dde-file-manager/oem-menuextensions/`。因为 6.x 文管只扫「`/usr/etc/deepin/menu-extensions` + `/etc/deepin/menu-extensions` + `~/.local/share/...`」三条路径，**不扫 `/usr/share/...`**；而官方文档说的「自动同步」只发生在 dde-desktop 进程里（`menu.cpp:230-235` 限定 `appName == "dde-desktop" || "org.deepin.dde-shell"`），文管进程不做同步。详见 §5.1。

**风险**：中低。主要风险是菜单会出现在所有 `.desktop` 上（靠 fsearch 侧兜底消除），以及 `.desktop` 解析的边界情况。

### 备选 1：方案 A + 去桌面限制

若用户希望在**桌面**上也能右键搜索快捷方式 → 去掉 `X-DFM-NotShowIn=Desktop;`。
**未确证**：桌面走 dde-desktop 的独立代码路径，OEM 是否加载、`onDesktop` 参数如何传，需实机验证。风险：可能出现菜单重复或不显示。

### 备选 2：方案 C（`.conf` 格式）

仅在实机验证确认 UOS 20 支持 `.conf` 后的备选。收益是 MenuTypes 取值更多、支持 `Name` 动态参数；风险是源码无实现证据。

### 不推荐：方案 B / D

B 不存在可用字段码；D 版本门槛不满足。

---

## 8. 需要实机验证的未确证项清单

以下为**源码无法回答、必须在 UOS 20 实机上验证**的项，已按优先级排序。

### 高优先级（阻塞方案 A 落地）

1. **UOS 20 实际的 dfm 版本号** —— 我按 5.2.45 分析（`dpkg -l | grep dde-file-manager` 确认）。5.1.2 与 5.2.45 的相关源码结论完全一致，所以风险低；但若实际是 5.0.x 或更早，`%f` 的形态可能不同，需重新核对。

2. **UOS 20 上 deepin 创建的「文件夹快捷方式」实际内容** —— 是 `Type=Link` + `URL=file:///...`？还是 `Type=Application` + `Exec=dde-file-manager -O ...`？还是符号链接？这**直接决定 fsearch 的解析逻辑**。实机命令：
   ```bash
   # 在桌面或文件管理器里对一个文件夹「创建快捷方式」，然后：
   cat ~/Desktop/某个快捷方式.desktop
   ```

3. **`%f` 在 UOS 20 实际传的是 `file://` URL 还是裸路径** —— 源码推导是 URL，但需实测确认。实测方法：临时把 Exec 改成写文件的脚本
   ```ini
   Exec=/tmp/probe.sh %f
   ```
   ```bash
   #!/bin/bash
   printf '%s\n' "$@" > /tmp/probe.log
   ```
   然后右键菜单点一下，看 `/tmp/probe.log` 内容。**这个实测能一次性验证 §6 方案 A 的所有字段替换行为。**

### 中优先级（影响体验，不阻塞）

4. **桌面上右键文件夹快捷方式，OEM 菜单是否出现** —— 5.x 走 dde-desktop 独立路径，`onDesktop=true` 时 `X-DFM-NotShowIn=Desktop` 会屏蔽。若希望桌面可用，需去掉该字段；但去掉后**桌面上的应用启动器也会显示此菜单项**（污染更严重）。

5. **在文件管理器内浏览 `~/Desktop` 目录右键快捷方式** → 因 `dde-{trash,computer,home}.desktop` early-return（`dfilemenumanager.cpp:509-514`），**OEM 菜单必然不加载**。这是已查证的必然行为，需确认这是否是用户遇到的「另一种没反应」场景。**注意这意味着：无论怎么配 OEM，桌面目录内的右键都无法用 OEM 实现**，只能靠方案 D（插件）或引导用户改在桌面图标上右键。

6. **`X-DFM-SupportSuffix=desktop` + `MimeType=application/x-desktop;` 同时写是否仍能显示** —— 两者是 AND 关系（`oemmenu.cpp:589-634`），理论上都匹配时能显示，但需实测确认无意外。

### 低优先级

7. `.conf`（Menu Entry）格式在 UOS 20 上是否可用 —— 源码无实现证据，倾向不可用。

8. `application/x-desktop` 的完整父 MIME 类型链 —— 推测为 `→ application/octet-stream`（未直接读到 shared-mime-info 的 freedesktop.org.xml）。**仅影响「不要用 `MimeType=application/octet-stream`」这个结论的方向，不影响方案 A。**

9. `EmptyArea` + `%p` 在 5.x 上是否真的传字面量 `%p` —— 源码证据充分（libqtxdg 不认 `%p`），值得实测确认（可能 dfm 有其他兜底路径）。

---

## 9. 附：本次调研的证据来源

| 类型 | 来源 |
|---|---|
| 源码（5.x） | `github.com/linuxdeepin/dde-file-manager` tag `5.2.45`、`5.1.2` 原始文件 |
| 源码（6.x） | 同仓库 master（commit `dcf6611`，2026-09-30）完整克隆 |
| 依赖库源码 | `lxqt/libqtxdg` → `src/qtxdg/xdgdesktopfile.cpp:1204-1341`（`expandExecString`） |
| 官方规范 | <https://docs.deepin.org/info/开发入门/桌面环境/文件管理器>（deepin 开发者平台，版本 1.0，2022-03-10） |
| 官方 qdoc | `docs/qdoc/knowledgebase/contextmenuext_zh.qdoc`（仓库内） |
| 官方 OEM 文档 | `docs/extension/02-oem-menu-script.md`（仓库内，master） |
| 历史文档 | <https://www.listera.top/gua-yong-yu-deepin-v20-ban-ben-de-wen-jian-guan-li-qi-you-jian-cai-dan-kuo-zhan-ge-shi-wen-dang>（`dde-file-manager-menu-oem` wiki 转载，2020） |
| 社区实践 | <https://blog.silence.pink/p/uos-dfm-context-menu>、deepin 论坛、UOS 论坛 |

**说明**：本文所有「已查证」结论均给出源码文件路径与行号，或官方文档 URL。**未在 UOS 20 实机上验证过任何一项** —— 源码分析无法替代实机确认，§8 列出了必须实测的 9 项。
