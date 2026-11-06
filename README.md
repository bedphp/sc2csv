# sc2csv

Brawl Stars CSV 解包工具（Windows 桌面端）。

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

无界面批量转换（保持输出目录结构，除非 `--flat`）：

```bat
build\bin\sc2csv.exe --decode <输入目录> <输出目录> [--overwrite] [--flat]
build\bin\sc2csv.exe --encode <输入目录> <输出目录> [--overwrite] [--flat]
```

- `--overwrite` 覆盖已存在的输出（否则跳过已存在的）
- `--flat` 不保留输入的子目录结构

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
