<a name="sc2csv"></a>

<p align="center">
  <img src="icon/ico.png" width="120" alt="sc2csv icon">
</p>

<h1 align="center">sc2csv</h1>

<p align="center">
  sc2csv is a Windows desktop tool for unpacking and packing Brawl Stars CSV files. It turns Supercell's compressed CSV containers into plain text and packs plain text back into containers, handling both older LZMA assets and the plaintext CSVs shipped by newer versions. Batch conversion and a macOS-style interface in Chinese, English and Japanese are built in.
</p>

English | [中文](#中文)

> **Important**
>
> This repository does not provide any instructions on how to modify any Supercell game or their assets. All information and all Assets were taken from public sources. You assume all risks from the use of this tool.

- Single-page interface: scans `assets/csv_logic`, `assets/csv_client`, and `assets/localization`, in both directions:
  - **Unpack** container → plain CSV
  - **Pack** plain CSV → container
- The log folds away at the bottom of the main page: it records scanning and conversion as they happen, and when expanded can be filtered by level, cleared, and exported.

Parsing SC bundles is outside the scope of this tool.

## Supported forms

| Form | Characteristics | Unpack | Pack |
|---|---|---|:---:|
| LZMA container | `0x5D` + 256 KiB dictionary + u32 unpacked size + raw LZMA1 | ✔ | ✔ |
| Plain CSV | Text as-is (newer installs ship this) | copied as-is | ✔ |

Scanning detects the actual form and shows it in the "Format" column, so one window handles both old compressed packages and new plaintext ones.

## CSV container format

The `.csv` files in the three `assets/` directories are not text; they are:

| Offset | Size | Meaning |
|---:|---:|---|
| `0x00` | 1 | LZMA properties, fixed `0x5D` (lc=3, lp=0, pb=2) |
| `0x01` | 4 | dictionary size, LE, fixed `0x00040000` (256 KiB) |
| `0x05` | 4 | unpacked size, LE u32 |
| `0x09` | ... | raw LZMA1 stream, **no EOS marker** |

The decoded text is Supercell's standard three-row CSV: column names on row 1, types on row 2, data from row 3.

## Build

Requires Visual Studio with the Desktop development with C++ workload. The first configure uses CMake `FetchContent` to download Dear ImGui (docking branch) and GLFW 3.4, so it needs network access.

```bat
:: Release (default)
build-release.bat          :: same as build.bat

:: Debug
build-debug.bat            :: same as build.bat -Config Debug

:: Configure only
configure.bat              :: Release
configure.bat -Config Debug
```

Each configuration gets its own build directory: Ninja fixes the configuration type at configure time, and one directory cannot hold both:

| Configuration | Build directory | Output | Console window |
|---|---|---|---|
| Release | `build\` | `build\bin\sc2csv.exe` | none (`/SUBSYSTEM:WINDOWS`) |
| Debug | `build-debug\` | `build-debug\bin\sc2csv.exe` | yes (console subsystem, `fprintf(stderr, …)` visible) |

The second directory reuses the dependency sources already fetched by the first (`FETCHCONTENT_SOURCE_DIR_GLFW/IMGUI`), so a checkout needs network access only once. The scripts are backed by PowerShell and take the same arguments: `.\build.ps1 -Config Debug`.

The two configurations differ only in subsystem (a `$<CONFIG:Debug>` generator expression in [CMakeLists.txt](CMakeLists.txt)); the entry point is still `main`. Release closes the console, but headless `--decode` / `--encode` still work and write through the caller's console.

Both configurations also build `sc2csv-cli.exe`, the console front end described under [Running](#running).

### Icon

`icon/ico.png` is the only icon source. At build time [cmake/GenerateIcon.py](cmake/GenerateIcon.py) generates:

| Output | Purpose |
|---|---|
| `build/generated/icon/sc2csv.ico` | Multi-size icon (16 / 24 / 32 / 48 / 64 / 128 / 256 px), compiled into the exe by rc.exe |
| `build/generated/icon/sc2csv.rc` | Resource script (`IDI_APPICON 101`) |
| `src/generated/AboutIcon.h` | 128 px PNG shown by the About panel |

After editing `icon/ico.png`, just rebuild; there is no need to export an `.ico` by hand. Explorer, the taskbar, and Alt+Tab read the icon from the executable's resource section, and the window icon loads from the same resource, so no `.ico` file needs to sit next to the exe.

Generating the icon requires Python 3 and Pillow (`pip install pillow`). Without Python the step is skipped: the About panel uses the committed `AboutIcon.h`, and the executable gets no icon resource.

## Running

```bat
:: Open the interface
build\bin\sc2csv.exe

:: Specify input/output directories
build\bin\sc2csv.exe --input <assets directory> --output <output directory>

:: Choose the interface language (zh-cn / en-us / ja-jp); the Language menu switches at any time
build\bin\sc2csv.exe --lang ja-jp

:: Interface zoom (0.75-2, default 1.1); scales the UI without changing the window size
build\bin\sc2csv.exe --zoom 1.2
```

Without `--lang`, the system UI language is used: Chinese → `zh-cn`, Japanese → `ja-jp`, anything else → `en-us`.

### Command-line version

`build\bin\sc2csv-cli.exe` is the console build. It links only the conversion core, always keeps its console (Release included), and uses the same options and semantics as the windowed program:

```bat
:: container -> plain CSV
build\bin\sc2csv-cli.exe --decode <input> <output> [--overwrite] [--flat]

:: plain CSV -> container
build\bin\sc2csv-cli.exe --encode <input> <output> [--overwrite] [--flat]

:: rewrite the input files in place
build\bin\sc2csv-cli.exe --decode <input> --in-place
build\bin\sc2csv-cli.exe --encode <input> --in-place
```

`<input>` is a directory (its `csv_logic`, `csv_client` and `localization` folders are scanned, or every `.csv` below it when those folders are absent) or a single `.csv` file.

| Option | Effect |
|---|---|
| `--overwrite` | overwrite existing output files (default: skip them) |
| `--flat` | do not keep the input's subdirectories under `<output>` |
| `--in-place` | write back over the input files; no `<output>` is needed |
| `-q`, `--quiet` | print only failures and the summary |
| `--lang <code>` | interface language: `zh-cn`, `en-us`, `ja-jp` |

Without `--lang`, the CLI follows the system UI language (Chinese → `zh-cn`, Japanese → `ja-jp`, anything else → `en-us`), exactly like the window. Progress, summary, usage text and conversion errors are all translated.

Exit status: `0` all files converted or skipped, `1` at least one file failed, `2` invalid arguments or the input could not be scanned.

`--help` prints the full usage. The windowed exe still accepts `--decode` / `--encode` as well, but `sc2csv-cli.exe` is the one to script against.

- `F12`: the CupertinoUi debug page (widget highlighting, widget-tree dump, UI script playback)
- The window is borderless and macOS-styled; use the traffic lights at the top right, drag the title bar, or drag an edge to resize.

## Output options

The sidebar's "Output" section has two switches:

| Switch | Default | Effect |
|---|---|---|
| Skip existing output files | ✔ | Skip a file whose destination already exists (the status column shows "Exists" and the log counts it as skipped); uncheck to overwrite |
| Overwrite in place | ☐ | Don't use the output directory; rewrite the CSVs in the **input** directory directly: unpack replaces a container with text, pack replaces text with a container |

With "Overwrite in place" checked: the output-directory button is disabled (this run does not write to it), and the "Skip existing output files" row is hidden — in-place writing overwrites exactly the file it reads, so skipping makes no sense. In-place mode applies to both directions and **always writes back**, regardless of the skip switch.

The start button follows the mode: "Unpack" in unpack mode, "Pack" in pack mode.

**Switching modes clears the input/output directories**: the two directions use the directories differently (unpack reads containers, pack reads text). Rather than guessing which one to reuse, both are reset, so a mode switch leaves a clean initial state and never runs with the previous direction's paths.

## Menu bar

The menu bar sits below the title bar. Mode, language and About live there; output options and the file list are in the main window:

| Title | Items | Shortcuts |
|---|---|---|
| File | Choose input directory…, Choose output directory…, Rescan, Quit | `Ctrl+O`, `Ctrl+Shift+O`, `Ctrl+R`, `Ctrl+Q` |
| Mode: Unpack / Mode: Pack | Unpack, Pack (the current mode is checked and the title follows it; switching clears input/output) | |
| Language | 中文 / English / 日本語 (the current language is checked) | |
| About | Clicking the title opens the About panel directly instead of expanding a menu | |

About uses the framework's `MenuBarAction` (clicking the title acts immediately); the same mechanism now includes `MenuBarToggle` in `third_party/cupertino/window/MenuBar.*` (draws a 12 pt checkbox and flips the value).

Menu text comes from the `menu.*` and `about.*` keys in `src/i18n/*.csv` (the mode title is `menu.mode_title`, with `%s` replaced by the current mode). A `\n` in a table cell is a line break (the About body is two lines).

## Translations

Language tables live in `src/i18n/*.csv`. At build time `cmake/GenerateI18n.cmake` merges them into `build/generated/i18n_data.inc` (`kLanguageNames` plus the `kLanguages` array), and `sc2csv::Translator` parses them at run time:

- `$lang:` language code, `$name:` menu name, `$order:` menu order
- The body is a two-column CSV: `ID,Text`; quote a value containing commas
- Adding a language: drop a `.csv` with the same format into `src/i18n/` and rebuild; CMake regenerates the table
- The Language menu switches immediately; `--lang` picks the startup language
- Without `--lang` the system UI language is followed (Chinese/Japanese, English otherwise)
- Log lines already written are not re-translated; later lines use the new language

`sc2csv::AvailableLanguages()` returns the built-in languages (code + name).

## Layout

```text
sc2csv/
├─ CMakeLists.txt
├─ build.bat / build.ps1 / configure.bat
├─ cmake/
│  ├─ GenerateI18n.cmake  src/i18n/*.csv → build/generated/i18n_data.inc
│  └─ GenerateIcon.py     icon/ico.png → build/generated/icon/*.ico + src/generated/AboutIcon.h
├─ icon/
│  └─ ico.png             the only icon source
├─ src/
│  ├─ main.cpp        interface, scan/unpack scheduling, log
│  ├─ cli_main.cpp    console front end (sc2csv-cli)
│  ├─ csv_codec.*     CSV container header parsing, LZMA decoding, three-row CSV parsing
│  ├─ csv_project.*   directory scanning, output paths, per-file conversion, UTF-8 paths
│  ├─ i18n.h/.cpp     translation table loading and lookup
│  └─ i18n/           zh-cn.csv / en-us.csv / ja-jp.csv
└─ third_party/
   ├─ cupertino/      vendored CupertinoUi UI framework (MIT)
   └─ lzma/           LZMA SDK decoder (public domain)
```

## Known limitations

- The interface font is a baked CupertinoUi subset: Chinese renders correctly; RTL languages such as Arabic show `?`.
- The preview shows only the first 400 rows and 24 columns; the full content is written to the output file.
- `.zflex` and SC-related resources are out of scope.

## License

This project is licensed under the [Apache License 2.0](LICENSE).

| Component | License |
|---|---|
| CupertinoUi (`third_party/cupertino`) | MIT |
| LZMA SDK (`third_party/lzma`) | public domain |
| Dear ImGui (fetched at build time) | MIT |
| GLFW (fetched at build time) | zlib/libpng |
| stb_image | public domain / MIT |

CupertinoUi's baked font and symbol data derives from Apple's SF Pro / SF Symbols and is not covered by the MIT license. See [third_party/cupertino/NOTICE.md](third_party/cupertino/NOTICE.md). Full attribution is in [NOTICE](NOTICE).

---

<a name="中文"></a>

<p align="center">
  <img src="icon/ico.png" width="120" alt="sc2csv 图标">
</p>

<h1 align="center">sc2csv</h1>

<p align="center">
  sc2csv 是一个 Windows 桌面端的 Brawl Stars CSV 工具：把 Supercell 的压缩 CSV 容器解包成明文，也能把明文重新打包成容器，兼容旧版 LZMA 资源和新版安装包里的明文 CSV。支持批量转换，界面为 macOS 风格，内置中文、英文和日文。
</p>

[English](#sc2csv) | 中文

> **重要**
>
> 本仓库不提供任何修改 Supercell 游戏或其资源的说明。所有信息和资源均来自公开来源。使用本工具所产生的一切风险由使用者自行承担。

- 单页界面：扫描 `assets/csv_logic`、`assets/csv_client`、`assets/localization`，支持两个方向：
  - **解包** 容器 → 明文 CSV
  - **打包** 明文 CSV → 容器
- 日志折叠在主页面底部：实时记录扫描与转换过程，展开后可按级别筛选、清空、导出。

SC bundle 的解析不在本工具范围内。

## 支持的形式

| 形式 | 特征 | 解包 | 打包 |
|---|---|---|:---:|
| LZMA 容器 | `0x5D` + 256 KiB 字典 + u32 解压后大小 + raw LZMA1 | ✔ | ✔ |
| 明文 CSV | 直接是文本（新版安装包如此） | 原样输出 | ✔ |

扫描时会识别实际形式并显示在「格式」列，所以同一个界面既能处理老的压缩包，也能处理新版的明文包。

## CSV 容器格式

`assets/` 下三处目录里的 `.csv` 不是文本，而是：

| 偏移 | 大小 | 含义 |
|---:|---:|---|
| `0x00` | 1 | LZMA properties，固定 `0x5D`（lc=3, lp=0, pb=2） |
| `0x01` | 4 | dictionary size LE，固定 `0x00040000`（256 KiB） |
| `0x05` | 4 | 解压后大小 LE u32 |
| `0x09` | ... | raw LZMA1 流，**没有 EOS 结束标记** |

解压出的文本是 Supercell 标准三行式 CSV：第 1 行列名，第 2 行类型，第 3 行起数据。

## 构建

需要 Visual Studio（含 C++ 桌面开发工作负载）。首次配置会用 CMake
`FetchContent` 拉取 ImGui（docking 分支）和 GLFW 3.4，需要网络。

```bat
:: Release（默认）
build-release.bat          :: 等同于 build.bat

:: Debug
build-debug.bat            :: 等同于 build.bat -Config Debug

:: 只配置不编译
configure.bat              :: Release
configure.bat -Config Debug
```

两种配置各用一个构建目录——Ninja 在 configure 阶段就固定了配置类型，一个目录存不下两套：

| 配置 | 构建目录 | 产物 | 控制台窗口 |
|---|---|---|---|
| Release | `build\` | `build\bin\sc2csv.exe` | 无（`/SUBSYSTEM:WINDOWS`） |
| Debug | `build-debug\` | `build-debug\bin\sc2csv.exe` | 有（控制台子系统，`fprintf(stderr, …)` 可见） |

第二个目录会复用第一个已经拉下来的依赖源码（`FETCHCONTENT_SOURCE_DIR_GLFW/IMGUI`），所以一个检出只需要
联网一次。底层是 PowerShell 脚本，参数一样：`.\build.ps1 -Config Debug`。

两种配置只有子系统不同（[CMakeLists.txt](CMakeLists.txt) 里按 `$<CONFIG:Debug>` 生成器表达式），入口仍是 `main`。
Release 关闭控制台后，无界面的 `--decode` / `--encode` 依然可用，输出会走调用方的控制台。

两种配置也会同时构建 `sc2csv-cli.exe`，即[运行时](#运行时)里的命令行版本。

### 图标

`icon/ico.png` 是唯一的图标源，构建时由 [cmake/GenerateIcon.py](cmake/GenerateIcon.py) 自动生成：

| 产物 | 用途 |
|---|---|
| `build/generated/icon/sc2csv.ico` | 16 / 24 / 32 / 48 / 64 / 128 / 256 px 多尺寸图标，rc.exe 编进 exe |
| `build/generated/icon/sc2csv.rc` | 资源脚本（`IDI_APPICON 101`） |
| `src/generated/AboutIcon.h` | 128 px PNG，关于面板显示 |

改 `icon/ico.png` 后直接重新构建即可，无需手动导出 .ico。资源管理器、任务栏和 Alt+Tab 的图标来自 exe
的资源段，窗口图标也从同一资源读取，所以 exe 旁边不需要再放 `.ico` 文件。

生成图标需要 Python 3 和 Pillow（`pip install pillow`）；没有 Python 时跳过图标生成，关于面板使用仓库里
已提交的 `AboutIcon.h`，exe 不带图标资源。

## 运行时

```bat
:: 直接打开界面
build\bin\sc2csv.exe

:: 指定输入/输出目录
build\bin\sc2csv.exe --input <assets 目录> --output <输出目录>

:: 指定界面语言（zh-cn / en-us / ja-jp），菜单栏「语言」也能随时切换
build\bin\sc2csv.exe --lang ja-jp

:: 界面缩放（0.75–2，默认 1.1），只放大界面，不改变窗口尺寸
build\bin\sc2csv.exe --zoom 1.2
```

不指定 `--lang` 时按系统界面语言自动选择：中文 → `zh-cn`，日文 → `ja-jp`，其他 → `en-us`。

### 命令行版本

`build\bin\sc2csv-cli.exe` 是控制台版本。它只链接转换核心，Release 下也始终保留控制台，参数和语义与 GUI 版本一致：

```bat
:: 容器 → 明文 CSV
build\bin\sc2csv-cli.exe --decode <输入> <输出> [--overwrite] [--flat]

:: 明文 CSV → 容器
build\bin\sc2csv-cli.exe --encode <输入> <输出> [--overwrite] [--flat]

:: 直接改写输入目录里的文件
build\bin\sc2csv-cli.exe --decode <输入> --in-place
build\bin\sc2csv-cli.exe --encode <输入> --in-place
```

`<输入>` 可以是目录（自动扫描其中的 `csv_logic`、`csv_client`、`localization`，没有这些目录时递归扫描所有 `.csv`），也可以是单个 `.csv` 文件。

| 选项 | 作用 |
|---|---|
| `--overwrite` | 覆盖已存在的输出文件（默认跳过） |
| `--flat` | 不保留输入的子目录结构 |
| `--in-place` | 直接写回输入文件，不需要 `<输出>` |
| `-q`、`--quiet` | 只输出失败项和汇总 |
| `--lang <代码>` | 界面语言：`zh-cn`、`en-us`、`ja-jp` |

不指定 `--lang` 时跟随系统界面语言（中文 → `zh-cn`，日文 → `ja-jp`，其他 → `en-us`），与 GUI 一致。进度、汇总、帮助和转换错误都会跟着切换。

退出码：`0` 全部成功或跳过，`1` 至少一个文件失败，`2` 参数错误或输入无法扫描。

`--help` 查看完整用法。GUI 的 exe 也仍然接受 `--decode` / `--encode`，但脚本里建议使用 `sc2csv-cli.exe`。

- `F12`：CupertinoUi 调试工具页（控件高亮、控件树导出、UI 脚本回放）
- 窗口为无边框 macOS 风格，可用右上角红绿灯、拖动标题栏、拖动边缘缩放

## 输出选项

侧边栏「输出」下面有两个开关：

| 开关 | 默认 | 作用 |
|---|---|---|
| 跳过已存在的输出文件 | ✔ | 目标文件已存在就跳过（状态列显示「已存在」，日志计入跳过）；取消勾选则覆盖 |
| 原地覆盖 | ☐ | 不使用输出目录，直接改写**输入目录**里的 CSV：解包把容器换成明文，打包把明文换成容器 |

勾上「原地覆盖」后：输出目录的选择按钮变灰（这次运行不写输出目录），「跳过已存在的输出文件」整行隐藏——原地覆盖
写的就是它读的那个文件，跳过无从谈起。原地覆盖对解包和打包都生效，并且**总是写回**，不受「跳过」影响。

开始按钮跟着模式走：解包模式显示「解包」，打包模式显示「打包」。

**切换模式会清空输入/输出目录**：两个方向用到的目录含义不同（解包读容器、打包读明文），与其猜用户想复用哪个，
不如一律重新选。所以换完模式就是干净的初始状态，不会带着上一个方向的路径跑。

## 菜单栏

标题栏下方是菜单栏，模式、语言和关于都在这里，输出选项和文件列表在主界面：

| 标题 | 项目 | 快捷键 |
|---|---|---|
| 文件 | 选择输入目录…、选择输出目录…、重新扫描、退出 | `Ctrl+O`、`Ctrl+Shift+O`、`Ctrl+R`、`Ctrl+Q` |
| 模式：解包 / 模式：打包 | 解包、打包（当前模式打勾，标题跟着变；切换时清空输入/输出目录） | |
| 语言 | 中文 / English / 日本語（当前语言打勾） | |
| 关于 | 点标题直接打开关于面板，不展开菜单 | |

「关于」用的是框架的 `MenuBarAction`（点标题直接执行）；同一套机制里还有
`third_party/cupertino/window/MenuBar.*` 中新增的 `MenuBarToggle`（画 12 pt 复选框并翻转值）。

菜单文字来自 `src/i18n/*.csv` 的 `menu.*`、`about.*` 键（模式标题是 `menu.mode_title`，`%s` 处填当前模式）；
表里用 `\n` 表示换行（关于面板的正文就是两行）。

## 多语言

语言表在 `src/i18n/*.csv`，构建时由 `cmake/GenerateI18n.cmake` 汇总成
`build/generated/i18n_data.inc`（`kLanguageNames` 语言名称数组 + `kLanguages` 语言数组），
运行时由 `sc2csv::Translator` 解析：

- `$lang:` 语言代码，`$name:` 菜单显示名，`$order:` 菜单顺序
- 表体是两列 CSV：`ID,Text`；值里有逗号时用双引号包起来
- 新增语言：往 `src/i18n/` 放一个同格式的 `.csv`，重新构建即可，CMake 会自动重新生成
- 菜单栏「语言」即时切换语言；`--lang` 指定启动语言
- 没有 `--lang` 时跟随系统界面语言（中文/日文，其余英文）
- 日志里已经写入的行不会跟着切换重译，之后的新日志使用新语言

`sc2csv::AvailableLanguages()` 返回内置语言（代码 + 名称）列表。

## 目录结构

```text
sc2csv/
├─ CMakeLists.txt
├─ build.bat / build.ps1 / configure.bat
├─ cmake/
│  ├─ GenerateI18n.cmake  src/i18n/*.csv → build/generated/i18n_data.inc
│  └─ GenerateIcon.py     icon/ico.png → build/generated/icon/*.ico + src/generated/AboutIcon.h
├─ icon/
│  └─ ico.png             唯一的图标源
├─ src/
│  ├─ main.cpp        界面、扫描/解包调度、日志
│  ├─ cli_main.cpp    命令行入口（sc2csv-cli）
│  ├─ csv_codec.*     CSV 容器头解析、LZMA 解码、三行式 CSV 解析
│  ├─ csv_project.*   目录扫描、输出路径、单文件转换、UTF-8 路径
│  ├─ i18n.h/.cpp     语言表加载与查询
│  └─ i18n/           zh-cn.csv / en-us.csv / ja-jp.csv
└─ third_party/
   ├─ cupertino/      CupertinoUi UI 框架副本（MIT）
   └─ lzma/           LZMA SDK 解码器（public domain）
```

## 已知限制

- 界面字体是 CupertinoUi 的烘焙子集：中文正常，阿拉伯语等 RTL 语言显示为 `?`。
- 预览默认只显示前 400 行、前 24 列；完整内容已写入输出文件。
- `.zflex` 及 SC 相关资源不在当前范围内。

## 许可证

本项目采用 [Apache License 2.0](LICENSE)。

| 组件 | 许可证 |
|---|---|
| CupertinoUi（`third_party/cupertino`） | MIT |
| LZMA SDK（`third_party/lzma`） | public domain |
| Dear ImGui（构建时获取） | MIT |
| GLFW（构建时获取） | zlib/libpng |
| stb_image | public domain / MIT |

CupertinoUi 的烘焙字体与符号数据来自 Apple 的 SF Pro / SF Symbols，不在 MIT 授权范围内，
详见 [third_party/cupertino/NOTICE.md](third_party/cupertino/NOTICE.md)。完整归属说明见 [NOTICE](NOTICE)。
