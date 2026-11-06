# CupertinoUi

CupertinoUi 是一套基于 **Dear ImGui** 的 macOS 15 / iOS 18 风格界面库：
它把 SwiftUI 式的声明式布局（`VStack` / `HStack` / `Form` / `NavigationSplitView`）
和 AppKit 风格的控件外观（按钮、开关、滑杆、侧边栏、警告框、Popover…）
带到 ImGui 应用里。所有控件都由 ImGui 绘制，可以直接嵌进现有 ImGui 窗口，
也可以像示例一样用 `Cupertino::Window` 组成完整的 macOS 窗口。

- **一行启动**：`Cupertino::Run(options, [] { /* UI */ })`，框架自带原生窗口、
  GLFW/OpenGL3/ImGui 初始化、渲染循环、DPI 与主题
- 原生无边框窗口：右上角红绿灯、标题栏拖动、四边四角缩放、双击缩放；
  Windows 11 使用 DWM 圆角，Windows 10 用圆角窗口区域裁剪（无黑色方角/黑边）
- 内置调试工具页（F12）：点击区域高亮、UI 脚本录制/回放、控件树导出、截图
- 无运行时资源依赖：字体、SF Symbols、字距表全部编译进静态库
- 布局单位是「点」（pt），通过 `displayScale × zoom` 换算成像素
- 支持浅色 / 深色、强调色、控件尺寸、焦点环、键盘导航
- 附带完整 Showcase（仿 ImGui `ShowDemoWindow`），可逐项查看所有控件与状态

> 本目录是 UI 框架的独立副本，只包含框架源码与使用文档。

---

## 1. 目录结构

```text
CupertinoUi/
├─ Cupertino.h / Cupertino.cpp   公共入口（Run / Application / Initialize / EndFrame）
├─ Showcase.cpp                  全部控件的演示窗口
├─ app/Application.cpp           原生窗口宿主：GLFW + OpenGL3 + ImGui + 渲染循环
├─ debug/                        UI 探针、脚本播放、调试工具页
├─ controls/                     按钮、开关、滑杆、文本框、选择器、进度…
├─ core/                         环境值、主题、颜色、绘制、排版、字体、符号
│  └─ generated/                 预烘焙字体与字距表（约 25 MB）
├─ layout/                       栈布局、网格、滚动视图、分栏
├─ overlays/                     警告框、通知、Popover、Sheet、Tooltip、菜单
├─ views/                        表单、侧边栏、列表、表格、图表…
├─ window/                       macOS 窗口、工具栏、标题栏、原生窗口桥接
├─ examples/main.cpp             最小应用：只写 UI，框架负责窗口与渲染
├─ examples/showcase.cpp         原生窗口中的完整控件演示
├─ examples/gallery.cpp          控件测试台：全控件 + 环境开关 + 事件日志
├─ third_party/stb/stb_image.h   图片加载（供 Image / Bitmap 使用）
├─ bin/cupertino_hello.exe       已构建的最小示例
├─ bin/cupertino_showcase.exe    已构建的 Showcase
├─ CMakeLists.txt
├─ build.bat / build.ps1         Windows 一键构建
├─ LICENSE / NOTICE.md           许可证与第三方素材说明
└─ docs/guide.md                 完整用法与控件目录
```

---

## 2. 环境要求

| 项目 | 要求 |
|---|---|
| C++ | C++20 |
| CMake | 3.24+ |
| Dear ImGui | **docking 分支**（使用 `imgui_internal.h`） |
| 图形后端 | 内置 GLFW 3.4 + OpenGL3 宿主；也支持手动接入已有 ImGui 应用 |
| 平台 | Windows / macOS / Linux（示例在 Windows 下验证） |

`CMakeLists.txt` 会用 `FetchContent` 拉取 ImGui（docking）和 GLFW 3.4。
首次配置需要网络；之后可以离线增量构建。

---

## 3. 构建与运行

Windows 下可以直接双击/运行：

```bat
build.bat
```

它会自动查找 Visual Studio 的 C++ 工具链、CMake 和 Ninja，完成配置与构建。
产物：

```text
build/cupertino_hello.exe      最小应用（推荐从这里开始）
build/cupertino_showcase.exe   全控件 Showcase
build/cupertino_gallery.exe    控件测试台（环境开关、状态覆盖、事件日志）
```

其他平台或想手动控制：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

运行后会出现一个 macOS 风格的 Showcase 窗口，左侧切换分类。
只想配置而不构建时运行 `configure.bat`。

本目录另附已构建好的 `bin/cupertino_hello.exe` 和 `bin/cupertino_showcase.exe`，
可直接双击运行；它们只依赖 Windows 系统组件与 MSVC 运行库。

如果只想把框架作为静态库接入自己的工程：

```cmake
add_subdirectory(CupertinoUi)
target_link_libraries(your_app PRIVATE cupertino_ui)
```

`cupertino_ui` 会公开传播 ImGui、GLFW、`IMGUI_USE_WCHAR32` 和
`IMGUI_DEFINE_MATH_OPERATORS` 等编译设置。

关闭示例目标：

```bash
cmake -S . -B build -DCUPERTINO_BUILD_SHOWCASE=OFF
```

---

## 4. 零样板启动：`Cupertino::Run`

框架内置原生窗口宿主。它负责创建**无系统标题栏的 GLFW 窗口**、初始化
OpenGL3 / Dear ImGui、加载字体、处理 DPI 与渲染循环，并把窗口画成 macOS 风格。
业务代码只需要一个内容回调：

最短形式（默认 960×640、深色、带标题栏）：

```cpp
#include "Cupertino.h"

int main() {
    return Cupertino::Run([] {
        Cupertino::Text("Hello, CupertinoUi");
    });
}
```

带窗口配置：

```cpp
#include "Cupertino.h"

int main() {
    return Cupertino::Run({
        .title = "My App",
        .size = ImVec2(900.0f, 620.0f),
        .appearance = Cupertino::Appearance::Dark,
    }, [] {
        // 这里直接写 UI，不需要 GLFW / ImGui 的任何初始化代码
        Cupertino::Form([] {
            Cupertino::Section({.header = "General"}, [] {
                static bool enabled = true;
                Cupertino::Toggle("Enabled", &enabled);
            });
        });
    });
}
```

运行后得到的是真实的操作系统窗口，而不是 ImGui 内部模拟的窗口：

- 右上角红绿灯可用：关闭 / 最小化 / 缩放
- 拖动标题栏移动窗口；双击标题栏切换缩放
- 拖动窗口边缘缩放（带最小/最大尺寸限制）
- Windows 11 使用 DWM 圆角；Windows 10 用 `SetWindowRgn` 做圆角裁剪，不再有黑色方角
- 自动出现在任务栏和 Alt+Tab 中
- 每帧自动同步 `displayScale`；运行中修改 `Environment().appearance` /
  `Environment().accent` 会立即生效（窗口圆角和 DWM 深色边框也会跟着更新）

### 4.1 `ApplicationOptions`

| 字段 | 默认值 | 说明 |
|---|---|---|
| `title` | `"CupertinoUi"` | 窗口标题 |
| `size` | `960 × 640` | 初始尺寸（pt） |
| `minSize` / `maxSize` | `480 × 320` / 无限制 | 缩放限制；`maxSize` 为 0 表示不限制 |
| `appearance` | `Dark` | 初始外观：`Light` / `Dark`；运行中可通过 `Environment().appearance` 切换 |
| `accent` | `Blue` | 初始强调色；运行中可通过 `Environment().accent` 切换 |
| `zoom` | `1.0` | 初始界面缩放（0.75–2） |
| `resizable` | `true` | 是否允许拖动边缘缩放 |
| `closable` / `minimizable` / `zoomable` | `true` | 红绿灯是否可用 |
| `titleBar` | `true` | 是否在内容上方画 28 pt 标题栏 |
| `wrapContent` | `true` | 自动把内容包进原生窗口；一般保持默认 |
| `compactTitleBar` | `false` | 红绿灯放在 28 pt 标题栏里 |
| `toolbarStyle` | `Unified` | `Unified` / `UnifiedCompact` |
| `fullKeyboardAccess` | `false` | Tab 是否遍历全部控件 |
| `onClose` | 空 | 点击关闭前的回调；返回 `false` 可阻止退出（例如询问未保存修改） |
| `debugTools` | `false` | 启用调试工具页；F12 显示/隐藏 |
| `debugToolsOpen` | `false` | 启动时直接显示调试工具页 |
| `showUiItems` | `false` | 启动时显示可点击区域高亮 |
| `uiScriptPath` | 空 | 启动时播放的 UI 自动化脚本 |
| `uiDumpItemsPath` | 空 | 启动后导出控件树到文件 |
| `fonts` | 内置字体 | 字体配置，字段同 `Configuration` |

### 4.2 两种窗口形态

**标题栏模式（默认）**：`titleBar = true`。框架画一条 28 pt 标题栏，内容在下方。
适合表单、单页设置等普通界面。

**全出血模式**：`titleBar = false`。内容占满整个窗口，红绿灯浮在右上角。
适合自己画工具栏的布局，例如 `NavigationSplitView`（它会画 52 pt 工具栏和
贯穿全高的侧边栏）。这是 macOS 系统设置的标准形态。

```cpp
return Cupertino::Run({
    .title = "Settings",
    .size = ImVec2(860.0f, 600.0f),
    .titleBar = false,          // 让 NavigationSplitView 自己画工具栏
}, [] {
    static int page = 0;
    Cupertino::NavigationSplitView({
        .sidebarWidth = 200.0f,
        .title = "General",
    }, /* 侧边栏 */ [] {
        Cupertino::List([] { /* ... */ });
    }, /* 详情 */ [] {
        Cupertino::Form([] { /* ... */ });
    });
});
```

也可以在内容里调用 `Cupertino::Window`：每帧**第一个**被绘制的 `Window` 会接管
原生窗口边框，后续的 `Window` 仍是窗口内部的浮动面板。`ShowShowcase` 就是
这样工作的（设置 `wrapContent = false`，让它自己画窗口）。

### 4.3 已有 ImGui 应用：手动接入

如果项目已经拥有自己的 GLFW/ImGui 主循环，只想要控件，则继续使用底层入口：

```cpp
ImGui::CreateContext();
ImGui_ImplGlfw_InitForOpenGL(window, true);
ImGui_ImplOpenGL3_Init("#version 330");

Cupertino::Configuration config;   // 空路径 = 使用内置烘焙字体
if (!Cupertino::Initialize(config))
    return 1;

while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    DrawMyUi();

    Cupertino::EndFrame();      // 固定：内容之后，ImGui::Render() 之前
    ImGui::Render();
    // ... 清屏并渲染 ImGui draw data
    glfwSwapBuffers(window);
}
```

手动接入时 `Cupertino::Window` 仍是内部 ImGui 窗口；只有通过
`Cupertino::Run` / `Cupertino::Application` 启动时，它才会接管原生窗口。

### 4.4 内置调试工具页（F12）

```cpp
Cupertino::ApplicationOptions options;
options.debugTools = true;       // 启用工具页，F12 显示/隐藏
options.debugToolsOpen = true;   // 可选：启动时直接展开
```

工具页在原生窗口内以浮动面板出现，包含：

- **Show clickable items**：给所有可点击区域画编号框，并显示 ImGui ID；
  悬停的控件高亮
- **Script path / Load & Run / Stop**：播放 UI 自动化脚本
- **Dump items**：把当前控件树写到 `ui_items.txt`（窗口名、编号、ID、坐标、尺寸）
- **Status / Frames**：脚本状态与帧计数

脚本命令（每行一条，`#` 开头为注释）：

```text
wait 10
move 400 300
click 400 300
doubleclick 400 300
rightclick 400 300
drag 100 100 300 100
scroll 0 -3
key ctrl+s
text Hello
click_item "window" 2
click_id 0x1234ABCD
dump_items items.txt
screenshot shot.ppm
quit
```

也可以从启动参数直接运行：

```bat
cupertino_hello.exe --ui-script test.txt
cupertino_hello.exe --ui-dump-items items.txt
```

对应 API：`ApplicationOptions::uiScriptPath`、`uiDumpItemsPath`、`showUiItems`。
脚本通过 ImGui 事件注入，不会移动真实鼠标；`screenshot` 写出客户区的二进制 PPM。

### 初始化配置

```cpp
struct Configuration {
    std::string fontsDirectory;   // 放 SF Pro / SF Mono / 符号字体的目录；空 = 用内置字体
    std::string uiFontRegular;    // CJK 回退字体（常规），默认 Microsoft YaHei
    std::string uiFontBold;       // CJK 回退字体（粗体）
    bool fullKeyboardAccess = false; // true: Tab 遍历全部控件，空格按下
};
```

---

## 5. 窗口与页面骨架

最常见的 macOS「设置页」结构（全出血 + 侧边栏）：

```cpp
return Cupertino::Run({
    .title = "My App",
    .size = ImVec2(860.0f, 600.0f),
    .titleBar = false,
}, [] {
    static int page = 0;
    static bool dark = true;
    static int accent = 0;

    Cupertino::NavigationSplitView({
        .sidebarWidth = 200.0f,
        .title = "General",
    }, [] {
        Cupertino::List([] {
            Cupertino::Section([] {
                Cupertino::NavigationLink("General", &page, 0,
                    {.icon = {Cupertino::Symbols::GearshapeFill, Cupertino::IconPlate::Gray}});
                Cupertino::NavigationLink("Appearance", &page, 1,
                    {.icon = {Cupertino::Symbols::PaintbrushFill, Cupertino::IconPlate::Blue}});
            });
        });
    }, [] {
        Cupertino::Form([] {
            Cupertino::Section({.header = "Appearance"}, [] {
                Cupertino::Toggle("Use dark mode", &dark);
                Cupertino::Picker("Accent color", &accent, {"Blue", "Purple", "Graphite"});
            });
        });
    });
});
```

- `NavigationSplitView` 自己画 52 pt 工具栏，红绿灯浮在窗口右上角
- `Form` / `Section` 自动给内部控件应用「表单外观」
  （开关变成 iOS 风格 switch，Picker 变成无边框弹出框）
- 需要普通标题栏时用 `titleBar = true`，内容会自动排在标题栏下方

---

## 6. 布局模型

CupertinoUi 的布局是 SwiftUI 风格的：容器沿一个轴排列子视图，子视图在绘制时
向容器报告尺寸。**所有尺寸单位都是点（pt）**。

```cpp
using namespace Cupertino;

VStack({.alignment = HorizontalAlignment::Leading, .spacing = 12.0f}, [] {
    Text("Title", {.font = Font::Style(TextStyle::Title2)});
    Text("Secondary line", {.foreground = Foreground::Secondary});

    HStack({.spacing = 8.0f}, [] {
        Button("Cancel");
        Spacer();
        Button("OK", {.role = ButtonRole::Default});
    });

    Divider();

    ScrollView({.axis = ImGuiAxis_Y}, [] {
        for (int i = 0; i < 50; ++i)
            Text(std::to_string(i));
    });

    Frame({.width = 320.0f}, [] {
        Text("Fixed width");
    });

    Padding(EdgeInsets::Symmetric(12.0f, 8.0f), [] {
        Text("Padded");
    });
});
```

### 布局要点

| API | 作用 |
|---|---|
| `VStack` / `HStack` / `ZStack` | 垂直 / 水平 / 叠放容器 |
| `Spacer()` | 沿主轴吃掉剩余空间 |
| `Frame({...})` | 固定或限制宽高，`Infinity` 表示填满 |
| `Padding(EdgeInsets, ...)` | 内边距 |
| `Divider()` | 分隔线（自动随父容器方向） |
| `ScrollView({...}, ...)` | 滚动容器，默认占满可用空间 |
| `HSplitView` / `VSplitView` | 可拖动分栏 |
| `Grid` | 网格布局（见 `layout/Grid.h`） |
| `Background(paint, content)` / `Overlay(paint, content)` | 在内容背后/上方绘制 |
| `Id(value, content)` | 用值区分同一段 UI 的多个实例 |

容器第一次出现时会多测量一遍内容（SwiftUI 的两遍布局）。这意味着：

- 布局回调**可能在一帧内执行两次**，不要在回调里做有副作用的事
- 需要持久状态时，状态必须放在回调外部（`static`、成员变量或全局模型）
- 需要动画的值应基于 `ImGui::GetIO().DeltaTime`，不要在布局阶段修改

---

## 7. 控件速查

完整签名与示例见 [`docs/guide.md`](docs/guide.md)。常用控件：

### 按钮

```cpp
Button("Save");                                      // 普通按钮
Button("Delete", {.role = ButtonRole::Destructive}); // 红色
Button("OK", {.role = ButtonRole::Default});         // 强调色，Return 触发
Button("Details", {.style = ButtonStyle::Borderless});
Button("Learn more", {.style = ButtonStyle::Link});
Button("Add", {.symbol = Symbols::Plus});            // 带 SF Symbol
```

### 开关 / 滑杆 / 步进器

```cpp
Toggle("Enabled", &enabled);                                     // 自动样式
Toggle("Visible", &visible, {.style = ToggleStyle::Checkbox});
Toggle("Option", &on, {.style = ToggleStyle::Radio});

float volume = 0.5f;
Slider("Volume", &volume, 0.0f, 1.0f, {.width = 220.0f});

int copies = 1;
Stepper("Copies", &copies, 1, 99);
```

### 文本输入

```cpp
std::string name;
TextField("Name", &name, {.placeholder = "Untitled"});

std::string search;
SearchField("Search", &search, {.placeholder = "Search"});

std::string password;
SecureField("Password", &password);
```

### 选择器

```cpp
int size = 0;
Picker("Size", &size, {"Small", "Medium", "Large"});
Picker("Style", &style, {"A", "B"}, {.style = PickerStyle::Segmented});

bool segments[3] = {true, false, true};
SegmentedToggles("Formats", segments, {"PNG", "JPEG", "WebP"});
```

### 文本与图片

```cpp
Text("Body text");
Text("Title", {.font = Font::Style(TextStyle::Title2), .foreground = Foreground::Secondary});
Text("Wrapped paragraph", {.wraps = true, .truncation = TruncationMode::Tail});

Image(Symbols::FolderFill, {.foreground = Foreground::Accent});
```

### 进度与标签

```cpp
ProgressView();                       // 不确定进度：旋转指示器
ProgressView(0.42f);                  // 进度条
ProgressView(0.42f, {.style = ProgressViewStyle::Circular});
ProgressView({.label = "Importing…", .currentValueLabel = "42%"});

LabeledContent("Version", "1.0.0");   // 左标签 + 右值
```

---

## 8. 容器与覆盖层

### 表单

```cpp
bool launch_at_login = false;
float cache = 0.5f;

Form([&] {
    Section({.header = "General"}, [&] {
        Toggle("Launch at login", &launch_at_login);
        LabeledContent("Version", "1.0.0");
    });
    Section({.header = "Advanced", .footer = "Changes apply immediately."}, [&] {
        Slider("Cache", &cache, 0.0f, 1.0f);
    });
});
```

### 侧边栏

```cpp
int selection = 0;
Icon icon{Symbols::GearshapeFill, IconPlate::Gray};

List([&] {
    Section([&] {
        NavigationLink("General", &selection, 0, {.icon = icon});
        NavigationLink("Advanced", &selection, 1, {.icon = icon, .badge = 3});
    });
});
```

### 警告框 / 确认框

```cpp
bool show = true;
const int choice = Alert("Delete this item?", &show,
    {{"Cancel", ButtonRole::Cancel}, {"Delete", ButtonRole::Destructive}},
    {.message = "This cannot be undone."});

if (choice == 1) { /* 删除 */ }
```

### Sheet / Popover / 通知

```cpp
std::string name;
bool sheet = false;
if (Button("Edit…")) sheet = true;
Sheet(&sheet, {.width = 420.0f}, [&] {
    Form([&] { Section([&] { TextField("Name", &name); }); });
    SheetFooter([&] { Button("Done", {.role = ButtonRole::Default}); });
});

bool popover = false;
Popover(&popover, {.size = ImVec2(240, 120)}, [&] {
    Button("Show details");
}, [] {
    Text("Popover content");
});

bool notify = true;
NotificationBanner(&notify, {.title = "Export complete", .body = "3 files saved."});
```

### 菜单 / 工具栏 / Tab

```cpp
int tab = 0;

Window("Preferences", &open, {.size = ImVec2(720, 480)}, [&] {
    TitleBar("Preferences");                 // 28 pt 标题栏
    TabBar(&tab, {{"General"}, {"Advanced"}});
    Divider();
    // 页面内容
});

// 工具按钮放在无侧边栏窗口的 52 pt 工具栏里：
bool docs_open = true;
Window("Documents", &docs_open, {.size = ImVec2(720, 480)}, [] {
    Toolbar({.title = "Documents"}, [] {
        Button("New", {.symbol = Symbols::Plus});
    });
    // 内容
});
```

覆盖层（Alert / Sheet / Popover / Notification / Tooltip）应在同一窗口的内容里调用，
`Cupertino::EndFrame()` 会把它们统一绘制到最上层。

---

## 9. 外观与环境值

所有控件从 `Cupertino::Environment()` 读取当前环境，可以在任意层级用
`WithEnvironment` 临时覆盖：

```cpp
// 全局
Cupertino::Environment().appearance = Cupertino::Appearance::Dark;
Cupertino::Environment().accent = Cupertino::AccentColor::Blue;
Cupertino::Environment().controlSize = Cupertino::ControlSize::Small;
Cupertino::Environment().displayScale = 1.5f;  // Retina = 2
Cupertino::Environment().zoom = 1.0f;

// 局部
Cupertino::WithEnvironment([](Cupertino::EnvironmentValues& env) {
    env.controlSize = Cupertino::ControlSize::Large;
}, [] {
    Button("Large button");
});

// 禁用一组控件
Cupertino::Disabled(true, [] {
    Button("Unavailable");
});
```

常用环境字段：

| 字段 | 说明 |
|---|---|
| `platform` | `MacOS` / `IOS`，影响字号与控件形态 |
| `appearance` | `Light` / `Dark` |
| `accent` | `Multicolor`、`Blue`、`Purple`、`Pink`、`Red`、`Orange`、`Yellow`、`Green`、`Graphite` |
| `controlSize` | `Mini` / `Small` / `Regular` / `Large` / `ExtraLarge` |
| `displayScale` | 显示缩放（2 = Retina） |
| `zoom` | 用户界面缩放（0.75–2） |
| `controlActiveState` | 窗口是否为 key/main/inactive，影响控件灰度 |
| `enabled` | 控件是否可用 |
| `insideForm` | 由 `Form` 自动设置，不要手动改 |

语义颜色用 `Foreground`：`Primary` / `Secondary` / `Tertiary` / `Quaternary` /
`Accent` / `White` / `Custom`。需要精确颜色时直接构造 `Rgba`：

```cpp
Cupertino::Rgba color = Cupertino::Rgba::Hex(0x0A84FF);
```

---

## 10. 字体与中文

默认情况下字体已烘焙进 `core/generated/BakedFonts.inc`，无需任何外部文件。
`Cupertino::Initialize` 还会加载 CJK 回退字体，默认是 **Microsoft YaHei**，
所以中文可以直接显示。

如果想使用自己的字体：

```cpp
Cupertino::Configuration config;
config.fontsDirectory = "assets/fonts";   // SF Pro Text/Display、SF Mono 等
config.uiFontRegular = "C:/Windows/Fonts/msyh.ttc";
config.uiFontBold    = "C:/Windows/Fonts/msyhbd.ttc";
Cupertino::Initialize(config);
```

`fontsDirectory` 为空时使用内置字体；非空时优先从该目录加载。
注意：内置数据只包含框架用到的 SF Symbol 子集，未收录的符号会显示为回退字形。

---

## 11. 状态、ID 与事件

CupertinoUi 不引入额外的响应式系统，状态就是普通的 C++ 变量：

```cpp
static bool enabled = true;          // 单实例
static int selection = 0;
static std::string name;
```

控件返回 `bool` 表示「本帧发生改变或点击」：

```cpp
if (Button("Save"))
    Save();
if (Toggle("Enabled", &enabled))
    Rebuild();
if (Slider("Volume", &volume, 0.0f, 1.0f))
    ApplyVolume(volume);
```

ImGui 的 ID 由控件在层级中的位置生成。**同一段 UI 在循环里重复出现时**，
用 `Id` 或 `ImGui::PushID` 区分：

```cpp
for (int i = 0; i < items.size(); ++i) {
    Id(i, [&] {
        Toggle(items[i].name.c_str(), &items[i].enabled);
    });
}
```

注意：

- 不要在布局回调里增删容器外的状态（两遍测量会执行两次）
- 状态值改变时，旧控件的 ImGui 状态（动画、焦点、滚动位置）会自然重建
- 列表 / 表格使用选择索引时，索引必须与数据源保持一致

---

## 12. Showcase

想看某个控件在浅色 / 深色、悬停 / 按下 / 禁用下的样子，直接运行：

```bash
bin/cupertino_hello.exe       # 最小应用
bin/cupertino_showcase.exe    # 完整控件目录
```

在原生宿主里嵌入 Showcase：

```cpp
int main() {
    Cupertino::ApplicationOptions options;
    options.title = "CupertinoUi Showcase";
    options.size = ImVec2(1180.0f, 780.0f);
    options.appearance = Cupertino::Appearance::Dark;
    options.wrapContent = false;   // ShowShowcase 自己画窗口

    return Cupertino::Run(options, [] {
        static bool open = true;
        Cupertino::ShowShowcase(&open, {
            .initialPane = "buttons",   // buttons / choices / values / text / form /
                                        // lists / boxes / presentations / layout / charts
            .size = ImVec2(1000.0f, 680.0f),
            .position = ImVec2(40.0f, 40.0f),
        });
    });
}
```

`ShowShowcase` 本身就是一个 `Window`；在 `Run` 内它的第一个窗口会成为原生窗口
边框，关闭窗口即退出进程。

### 12.1 Control Gallery（控件测试台）

`build/cupertino_gallery.exe` 是给框架做验收用的测试台，不是示例：

- 左侧 13 个分类，覆盖 `controls/`、`views/`、`overlays/`、`window/` 的公开控件
- **环境**页可实时切换外观、强调色、控件尺寸、界面缩放和平台，并预览禁用状态
- 每个控件尽量列出全部样式与状态（正常 / 悬停 / 禁用 / 混合 / 带图标 / 带说明）
- 底部状态栏显示最近一次控件的回调结果，便于确认返回值
- 启动时启用 F12 调试工具（控件高亮、控件树导出、UI 脚本）

源码在 `examples/gallery.cpp`。发现某个控件不符合预期时，直接在该页复现并记录。

---

## 13. 常见问题

**控件不显示 / 布局错乱**
用 `Cupertino::Run` 时不会出现初始化顺序问题；手动接入时检查
`Cupertino::EndFrame()` 是否在 `ImGui::Render()` 之前调用，以及是否漏掉
`Cupertino::Initialize()`。

**中文显示为方框**
`uiFontRegular` / `uiFontBold` 指向系统中文字体；默认值假设 Windows 上存在
`Microsoft YaHei`。跨平台时应显式提供字体路径。

**编译报错找不到 `imgui_internal.h`**
必须使用 Dear ImGui 的 **docking 分支**；master 分支没有本库依赖的内部 API。

**高 DPI 下尺寸不对**
`Run` 会自动读取 `glfwGetWindowContentScale` 并设置 `displayScale`。手动宿主
需要每帧自己设置。布局单位是点，框架会自动换算像素。

**窗口圆角 / 拖动 / 缩放**
原生窗口完全由框架处理。Windows 11 使用 DWM 圆角并去掉默认黑边；Windows 10
用 `SetWindowRgn` 做圆角裁剪，不再出现黑色方角。四条边和四个角的 5–6 px 都是
缩放区（角上优先缩放，不会误触拖动）；标题栏（或全出血模式的顶部 52 pt）是
拖动区，双击标题栏切换缩放。红绿灯在右上角，关闭在最右侧。

**关闭按钮没反应**
`Run` 的自动窗口会处理关闭。自定义 `wrapContent = false` 内容里的
`Cupertino::Window` 如果传了 `open = nullptr`，关闭仍会触发原生窗口退出。

**和原生 ImGui 控件混用**
可以。Cupertino 控件就是普通 ImGui 绘制，只需保证
`Cupertino::EndFrame()` 的调用顺序。原生 ImGui 窗口建议放在 Cupertino 窗口之后。

**动画/焦点状态在切换页面后残留**
给页面内容套一层 `Id(page_index, ...)`，值变化时框架会把它当成全新的视图树。

---

## 14. 许可证与致谢

框架源码基于 **MIT License**，见 [`LICENSE`](LICENSE)。
上游项目：[MisterJerkins/imgui-apple](https://github.com/MisterJerkins/imgui-apple)。

内置字体由 SF Pro / SF Symbols 生成。SF 字体与 SF Symbols 是 Apple 的
授权素材，随本库分发仅用于开发与演示；将烘焙字体数据用于商业产品前，
请自行确认 Apple 的字体授权条款。详见 [`NOTICE.md`](NOTICE.md)。
