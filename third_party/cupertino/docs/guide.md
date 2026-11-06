# CupertinoUi 使用指南

本文是 `README.md` 的详细补充：布局引擎的工作方式、控件目录、
窗口与覆盖层的组合方式，以及接入时容易踩的坑。

所有 API 都在 `namespace Cupertino` 下；`Cupertino.h` 已经包含了全部公共头文件。

---

## 1. 坐标、缩放与两遍布局

### 点与像素

对外 API 的尺寸、间距、圆角、字体大小都以 **点（pt）** 为单位。
框架内部乘以：

```cpp
float scale = Environment().displayScale * Environment().zoom;
float pixels = points * scale;
```

- `displayScale`：显示器缩放，Retina 为 2
- `zoom`：用户界面缩放，建议范围 0.75–2

绘制回调（`Canvas` / `Background`）拿到的是 **像素** 矩形 `ImRect`，
需要反算点数时用 `Cupertino::Px(points)`，或直接除以 `Environment().Scale()`。

### 容器如何布局

`VStack` / `HStack` / `Form` / `ScrollView` 都是基于 `Layout::Container` 的容器：

1. 容器把可用空间（`Proposal()`）提供给子视图；
2. 子视图通过 `Place()` 申报尺寸、是否弹性、文本基线等；
3. 容器计算每个子视图的槽位，再绘制背景和分隔线。

新容器第一次出现时会多跑一遍「测量 pass」，此时 `Layout::IsMeasuring()`
为 `true`，子视图只测量不绘制。因此 **布局回调可能被调用两次**：

```cpp
// 错误：两遍布局会加两次
VStack([] {
    ++counter;
    Text(std::to_string(counter));
});

// 正确：状态在外部
++counter;
VStack([&] { Text(std::to_string(counter)); });
```

容器尺寸依赖上一帧（SwiftUI 的布局语义）。窗口第一次显示时可能有
一帧的尺寸过渡，这是预期行为。

---

## 2. 布局容器

### 2.1 栈

```cpp
VStack({.alignment = HorizontalAlignment::Leading, .spacing = 8.0f}, [] {
    Text("First");
    Text("Second");
});

HStack({.alignment = VerticalAlignment::Center, .spacing = 6.0f}, [] {
    Image(Symbols::FolderFill);
    Text("Name");
    Spacer();
    Text("Value", {.foreground = Foreground::Secondary});
});
```

`VStackOptions`：`alignment`（`Leading` / `Center` / `Trailing`）、`spacing`。
`HStackOptions`：`alignment`（`Top` / `Center` / `Bottom` / `FirstTextBaseline`）、`spacing`。

`ZStack` 让子视图叠放，使用 `Alignment`（水平 + 垂直）：

```cpp
ZStack({.alignment = {HorizontalAlignment::Center, VerticalAlignment::Center}}, [] {
    Image(Symbols::Photo);
    Text("Overlay");
});
```

### 2.2 弹性空间与分隔线

```cpp
Spacer();          // 吃掉主轴剩余空间
Spacer(24.0f);     // 至少 24 pt

Divider();         // 跟随父容器方向
Divider(color);    // 自定义颜色
```

### 2.3 Frame / Padding

```cpp
Frame({.width = 240.0f, .height = 32.0f}, [] { Text("Fixed"); });

Frame({.minHeight = 44.0f, .maxWidth = Infinity,
       .alignment = {HorizontalAlignment::Leading, VerticalAlignment::Center}}, [] {
    Text("Stretch horizontally");
});

Padding(EdgeInsets::All(12.0f), [] { Text("12 pt all around"); });
Padding(EdgeInsets::Symmetric(16.0f, 8.0f), [] { Text("16 x 8"); });
```

`FrameOptions` 字段：`width`、`height`、`minWidth`、`minHeight`、`maxWidth`、
`maxHeight`、`alignment`。值为 0 表示不限制；`Infinity` 表示填满。

### 2.4 滚动视图

```cpp
ScrollView({.axis = ImGuiAxis_Y, .spacing = 8.0f}, [] {
    for (int i = 0; i < 100; ++i)
        Text(std::to_string(i));
});

ScrollView({
    .axis = ImGuiAxis_X,
    .showsIndicators = true,
    .padding = EdgeInsets::Symmetric(12.0f, 0.0f),
    .indicator = ScrollIndicator::Always,
}, [] { /* ... */ });
```

可选的 `offset` 指针既用于读取当前滚动位置，也可以写入以程序化滚动：

```cpp
float offset = 0.0f;
ScrollView({.offset = &offset}, [] { /* ... */ });
```

`dragToScroll` 默认开启；`dragAnywhere = true` 时空白区域也能拖动。
`ScrollEdge(id, style, content)` 可以在滚动内容上方的工具栏下面画出
「内容已滚到栏下」的阴影/细线。

### 2.5 分栏

```cpp
float position = 220.0f;

HSplitView({.position = &position, .minimum = 160.0f,
            .maximum = 400.0f, .secondMinimum = 300.0f},
    [] { /* 左栏 */ },
    [] { /* 右栏 */ });
```

`VSplitView` 用法相同，只是上下分栏。

### 2.6 Grid

`layout/Grid.h` 提供网格容器，适合图标墙、设置项对齐等场景。
布局语义接近 SwiftUI 的 `Grid`：每列取最宽单元格的宽度，行内单元格按
`columnAlignment` 对齐。具体字段见头文件。

---

## 3. 控件目录

以下签名均省略了默认参数，完整声明以各头文件为准。

### 3.1 Button

```cpp
bool Button(const char* label, const ButtonOptions& options = {});
```

| 字段 | 说明 |
|---|---|
| `role` | `Normal` / `Default`（强调色，Return）/ `Cancel` / `Destructive`（红色） |
| `style` | `Bordered` / `Borderless` / `Link` / `AccessoryBar` / `AccessoryBarAction` |
| `symbol` | SF Symbol（`Cupertino::Symbols::...`） |
| `minWidth` | 最小宽度（pt） |
| `shortcut` | 菜单项快捷键 |
| `font` | 无边框/链接样式的字体 |
| `badge` / `subtitle` | 菜单项徽标 / 副标题 |

返回值在点击那一帧为 `true`。

相关：

```cpp
bool HelpButton(const char* id = "##help");
bool ButtonToggle(const char* label, bool* is_on, bool lit);
```

`HelpButton` 是右下角带问号的圆形帮助按钮。

### 3.2 Toggle

```cpp
bool Toggle(const char* label, bool* is_on, const ToggleOptions& options = {});
```

样式：

- `Automatic`：表单内是 iOS 风格开关，表单外是复选框
- `Switch` / `Checkbox` / `Radio`
- `Button` / `TitleButton`：按下后保持点亮的按钮
- `AccessoryBar`：工具栏范围的开关

`ToggleOptions` 还支持 `description`（行下方说明）、`icon`（行首图标）、
`mixed`（混合状态，显示为横线）。

### 3.3 Slider

```cpp
bool Slider(const char* label, float* value, float min, float max,
            const SliderOptions& options = {});
```

```cpp
float volume = 0.5f;
Slider("Volume", &volume, 0.0f, 1.0f, {
    .width = 220.0f,
    .ticks = 5,
    .minimumSymbol = Symbols::SpeakerFill,
    .maximumSymbol = Symbols::SpeakerWave3Fill,
});
```

- `ticks`：等距刻度；启用后默认是无填充的条形旋钮
- `filled`：强制显示填充
- `snapsToTicks`：只能取刻度值
- `captions`：轨道下方文字
- `style = SliderStyle::Circular`：圆形滑杆

### 3.4 TextField / SearchField / TextEditor

```cpp
bool TextField(const char* label, std::string* text,
               const TextFieldOptions& options = {});
bool SecureField(const char* label, std::string* text,
                 const TextFieldOptions& options = {});
bool SearchField(const char* label, std::string* text,
                 const SearchFieldOptions& options = {});
bool TextEditor(const char* label, std::string* text,
                const TextEditorOptions& options = {});
```

`TextFieldOptions`：

- `placeholder`：占位文本
- `width`：宽度；0 表示填满可用宽度（表单行里是行尾的一半）
- `alignment`：文本对齐
- `clearButton`：显示清除按钮
- `focusRing`：聚焦时是否画强调色描边
- `onSubmit`：按 Return 时回调

`TextEditor` 需要显式高度（用 `Frame` 包一层），否则会占满父容器。

### 3.5 Picker / SegmentedToggles

```cpp
bool Picker(const char* label, int* selection,
            std::initializer_list<const char*> items,
            const PickerOptions& options = {});

bool SegmentedToggles(const char* label, std::span<bool> on,
                      std::initializer_list<const char*> items,
                      float width = 0.0f);
```

`PickerStyle`：`Automatic` / `Menu` / `RadioGroup` / `Segmented` / `Arrows` / `Inline`。

```cpp
int size = 1;
Picker("Size", &size, {"Small", "Medium", "Large"},
       {.style = PickerStyle::Segmented});

bool formats[3] = {true, false, true};
SegmentedToggles("Formats", formats, {"PNG", "JPEG", "WebP"});
```

### 3.6 Stepper

```cpp
bool Stepper(const char* label, int* value, int min, int max,
             int step = 1, const char* format = nullptr);
bool Stepper(const char* label, float* value, float min, float max,
             float step, const char* format = nullptr);
```

`format` 是 `printf` 风格，例如 `"%d copies"`。

### 3.7 ProgressView

```cpp
void ProgressView();                                        // 不确定进度
void ProgressView(const ProgressViewOptions& options);      // 不确定 + 标签
void ProgressView(float value, const ProgressViewOptions& options = {});
```

`value` 范围 `[0, 1]`；`ProgressViewStyle` 可选 `Automatic` / `Linear` / `Circular`。

```cpp
ProgressView(progress, {
    .style = ProgressViewStyle::Linear,
    .width = 260.0f,
    .label = "Importing",
    .currentValueLabel = "42%",
});
```

### 3.8 Text / Image

```cpp
void Text(std::string_view text, const TextOptions& options = {});
void Image(unsigned symbol, const ImageOptions& options = {});
void Image(const Bitmap& bitmap, ImVec2 size = ImVec2(0, 0));
void Image(const Icon& icon, ImVec2 size);
```

`TextOptions`：

| 字段 | 说明 |
|---|---|
| `font` | `Font::Style(TextStyle::Body)` 等 |
| `foreground` | `Primary` / `Secondary` / `Tertiary` / `Quaternary` / `Accent` / `White` / `Custom` |
| `color` | `foreground == Custom` 时使用 |
| `alignment` | `Leading` / `Center` / `Trailing` |
| `wraps` | 是否按可用宽度换行 |
| `truncation` | `Tail` / `Middle` |

文本样式（`TextStyle`）：`LargeTitle`、`Title`、`Title2`、`Title3`、
`Headline`、`Subheadline`、`Body`、`Callout`、`Footnote`、`Caption`、`Caption2`。
`Font` 还支持 `.Weight(...)`、`.Monospaced()`、`.MonospacedDigit()`、
`.ImageScale(...)` 等链式修饰。

### 3.9 Label / LabeledContent / NavigationLink

```cpp
void Label(std::string_view title, const LabelOptions& options = {});
void LabeledContent(std::string_view label, std::string_view value);
void LabeledContent(std::string_view label, const std::function<void()>& content);

bool NavigationLink(std::string_view title,
                    const NavigationLinkOptions& options = {});
```

`NavigationLink` 在表单里是「标题 + 值 + 箭头」的可点击行；
在侧边栏里用它的另一个重载绑定选择：

```cpp
bool NavigationLink(const char* title, int* selection, int tag,
                    const NavigationLinkOptions& options = {});
```

### 3.10 其他控件

| 控件 | 头文件 | 说明 |
|---|---|---|
| `ComboBox` | `controls/ComboBox.h` | 可编辑下拉框 |
| `ColorPicker` | `controls/ColorPicker.h` | 色板、取色器 |
| `DatePicker` | `controls/DatePicker.h` | 日期 / 时间 |
| `Gauge` | `controls/Gauge.h` | 环形 / 线性仪表 |
| `LevelIndicator` | `controls/LevelIndicator.h` | 等级指示器 |
| `PathControl` | `controls/PathControl.h` | 面包屑路径 |
| `TokenField` | `controls/TokenField.h` | 令牌输入 |
| `KeyRecorder` | `controls/KeyRecorder.h` | 快捷键录制 |
| `ImageWell` | `controls/ImageWell.h` | 图片拖放框 |
| `Bezel` | `controls/Bezel.h` | 通用 bezel 容器 |

---

## 4. 表单与列表

### Form + Section

```cpp
Form([] {
    Section({.header = "General",
             .footer = "Applies to all new documents."}, [] {
        Toggle("Show toolbar", &show_toolbar);
        LabeledContent("Version", "1.0.0");
        Picker("Theme", &theme, {"System", "Light", "Dark"});
    });

    Section({.header = "Advanced", .boxed = false}, [] {
        Text("Content that draws its own box");
    });
});
```

- `Form` 自带滚动、20 pt 边距、分组间距
- `Section` 的 `header` / `description` / `footer` 都可选
- `headerAccessory` 可以放一个 spinner 等
- `boxed = false` 适合内部自带背景的列表或表格

### Sidebar

```cpp
List([] {
    Section([] {
        NavigationLink("General", &page, 0, {.icon = general_icon});
        NavigationLink("Advanced", &page, 1, {.icon = advanced_icon, .badge = 2});
    });

    SidebarDisclosureGroup("More", &expanded, [] {
        NavigationLink("About", &page, 2, {.icon = about_icon});
    });
});
```

`SidebarSearchField(&text)` 是固定在侧边栏顶部的圆角搜索框；
`SidebarAccount(...)` 是带头像的账户行。

### BorderedList / Table

`views/BorderedList.h` 提供带边框的列表（支持多选、行事件）；
`views/Table.h` 提供可排序、可滚动的多列表格。
两者都使用 `views/ListRows.h` 的行布局辅助函数。
具体签名较长，建议对照头文件与 `Showcase.cpp` 的 Lists / Boxes 页。

---

## 5. 原生窗口宿主

### 5.1 `Cupertino::Run`

`Run` 是推荐的启动方式。它创建一个无系统标题栏的原生窗口，并完成：

- GLFW 初始化、OpenGL 3.3 上下文、Dear ImGui 上下文与后端
- Cupertino 字体、主题、DPI、缩放
- 每帧的 `NewFrame` / `EndFrame` / `Render` / `SwapBuffers`
- 窗口拖动、边缘缩放、双击标题栏缩放、红绿灯（关闭/最小化/缩放）
- 关闭时按正确顺序销毁 ImGui、GLFW 资源

```cpp
#include "Cupertino.h"

int main() {
    return Cupertino::Run({
        .title = "My App",
        .size = ImVec2(900.0f, 620.0f),
        .appearance = Cupertino::Appearance::Dark,
    }, [] {
        Cupertino::Form([] {
            Cupertino::Section({.header = "General"}, [] {
                static bool enabled = true;
                Cupertino::Toggle("Enabled", &enabled);
            });
        });
    });
}
```

`Run` 返回 `0` 表示正常退出，`1` 表示窗口/GLFW 创建失败，`2` 表示字体加载失败。
内容回调在 UI 线程每帧调用一次；不要在里面做阻塞操作。

`ApplicationOptions::appearance` / `accent` / `zoom` 只是初始值。运行中直接改
`Cupertino::Environment()` 即可切换主题、强调色和缩放，宿主会把 DWM 深色边框
和清屏颜色一起更新：

```cpp
static bool dark = true;
if (Cupertino::Toggle("Dark", &dark))
    Cupertino::Environment().appearance = dark ? Cupertino::Appearance::Dark
                                               : Cupertino::Appearance::Light;
```

### 5.2 `Application`

需要提前拿到窗口控制权（例如从外部触发关闭）时，可以显式持有
`Application`：

```cpp
Cupertino::ApplicationOptions options;
options.title = "My App";
options.size = ImVec2(900.0f, 620.0f);

Cupertino::Application app(options);
// ... 保存指针；后台线程需要关闭时，把请求投递回 UI 线程再调用 request_close()
return app.run([] {
    // UI
});
```

`Application` 不可拷贝，可移动；一个进程只应创建一个实例（GLFW/ImGui
全局状态不支持多个宿主并存）。

拦截关闭（例如提示未保存修改）：

```cpp
options.onClose = [] {
    if (!has_unsaved_changes)
        return true;
    show_save_prompt = true;
    return false;   // 保持窗口打开
};
```

### 5.3 标题栏与内容形态

`ApplicationOptions::titleBar` 决定内容从哪里开始：

| 值 | 形态 | 适用 |
|---|---|---|
| `true`（默认） | 28 pt 标题栏 + 内容 | 普通表单、单页设置 |
| `false` | 内容填满窗口，红绿灯浮在右上角 | `NavigationSplitView`、自绘工具栏 |

`TitleBar` 风格下，内容区已经在标题栏下方，直接写 `Form` / `VStack` 即可。
全出血模式下，顶部 52 pt 是拖动区域，红绿灯在右上角；`NavigationSplitView`
的侧边栏从顶部贯穿到底，正好与之匹配。

### 5.4 内容里的 `Window`

在 `Run` 内，每帧第一个 `Cupertino::Window` 会接管原生窗口边框，忽略它自己的
`position` / `size`，铺满整个客户区。后续 `Window` 仍然是窗口内部的浮动面板。
因此：

- 使用 `wrapContent = true`（默认）时，不要在内容里再写 `Window`
- 需要自己控制窗口时设 `wrapContent = false`，再写一个 `Window`
- `ShowShowcase` 属于后一种，所以示例设置 `wrapContent = false`

### 5.5 平台行为

- **Windows**：无边框 GLFW 窗口；红绿灯在右上角（关闭在最右侧）。Windows 11
  使用 DWM 圆角并关闭默认的 1 px 黑边；Windows 10 额外用 `SetWindowRgn` 做
  圆角裁剪，避免圆角位置出现黑色方角。四条边和四个角都是缩放区，标题栏/
  工具栏是拖动区；任务栏和 Alt+Tab 正常显示
- **macOS**：无边框窗口 + 自定义红绿灯；`glfwWindowHint` 已开启
  forward-compatible 的 3.3 Core 上下文
- **Linux**：无边框窗口由窗口管理器决定是否允许拖动/缩放，逻辑仍走框架的
  手动拖动与边缘缩放

### 5.6 调试工具页

`ApplicationOptions::debugTools = true` 启用内置工具页，F12 切换显示；
`debugToolsOpen = true` 让它启动时就展开。工具页本身是一个普通
`Cupertino::Window`，在原生窗口内浮动。

内容：

- 点击区域高亮（编号 + ImGui ID，悬停高亮）
- UI 自动化脚本路径、Load & Run、Stop
- Dump items（写到 `ui_items.txt`）
- 脚本状态和帧计数

脚本命令与 `debug/ui_script.hpp` 顶部注释一致：`wait`、`move`、`click`、
`doubleclick`、`rightclick`、`drag`、`scroll`、`key`、`text`、`click_item`、
`click_id`、`dump_items`、`screenshot`、`quit`。启动参数版本：

```cpp
options.uiScriptPath = "test.txt";
options.uiDumpItemsPath = "items.txt";
options.showUiItems = true;
```

脚本注入的是 ImGui 输入事件，不会移动系统鼠标；坐标是客户区逻辑坐标。
`screenshot` 在交换缓冲前读取帧缓冲，写出二进制 PPM。

---

## 6. 窗口与内部面板

### Window

`Window` 有两种运行方式：

- **原生宿主内**（通过 `Run` / `Application` 启动）：每帧第一个 `Window` 忽略
  `position` / `size`，铺满操作系统窗口并绘制红绿灯、拖动和边缘缩放；之后的
  `Window` 是客户区内的浮动面板。
- **手动 ImGui 宿主内**：`Window` 始终是 ImGui 内部的 macOS 风格窗口，由框架
  自己处理拖动和右下角缩放。

```cpp
bool open = true;

Window("My App", &open, {
    .size = ImVec2(760, 540),
    .position = ImVec2(40, 40),
    .closable = true,
    .minimizable = true,
    .zoomable = true,
    .minSize = ImVec2(600, 400),
    .maxSize = ImVec2(1400, 1000),
    .style = WindowStyle::Automatic,
}, [] {
    // 内容
});
```

- `WindowStyle::Automatic`：内容填满窗口，标题由 `Toolbar` / `TitleBar` 提供
- `WindowStyle::TitleBar`：自带 28 pt 标题栏
- `WindowStyle::Plain`：无边框、无红绿灯，任意空白处可拖动
- `floating = true`：始终置顶的辅助面板
- `compactTitleBar = true`：红绿灯放在 28 pt 标题栏里

关闭按钮会把 `open` 置为 `false`。窗口位置/尺寸由框架保存，
不依赖 `imgui.ini`（示例中把 `IniFilename` 设为了空）。

### NavigationSplitView

```cpp
Cupertino::NavigationStep step = NavigationSplitView({
    .sidebarWidth = 215.0f,
    .title = "Settings",
    .showsHistoryButtons = true,
    .canGoBack = false,
    .canGoForward = false,
    .sidebarVisible = &sidebar_visible,
    .inspector = [] { /* 右侧检查器 */ },
}, [] {
    // 侧边栏
}, [] {
    // 详情
});
```

- 侧边栏宽度可通过 `minSidebarWidth` / `maxSidebarWidth` 允许拖动
- `inspector` 是右侧可拖动宽度的检查器列
- 返回值表示本帧点击了后退/前进

### TitleBar / Toolbar / TabBar

```cpp
Window("App", &open, {.size = ImVec2(720, 480)}, [] {
    TitleBar("Preferences");
    TabBar(&tab, {{"General", Symbols::GearshapeFill},
                  {"Advanced", Symbols::SliderHorizontal3}});
});
```

`Toolbar({.title, .subtitle, .background}, items)` 用于无侧边栏的窗口。
工具栏项可以用 `ToolbarButton(id, slot, symbol, enabled)` 或接受 `ImRect` 的重载。

---

## 7. 覆盖层

所有覆盖层都是「绘制在当前位置，并由框架在帧末提升到最上层」的视图。

### Alert

```cpp
bool presented = true;

int choice = Alert("Delete file?", &presented, {
    {"Cancel", ButtonRole::Cancel},
    {"Delete", ButtonRole::Destructive},
}, {
    .message = "This action cannot be undone.",
    .critical = false,
    .showsHelp = false,
});

if (choice == 1) { /* 确认 */ }
```

- 返回被点击按钮的下标，本帧没有点击则为 `-1`
- 两个按钮时并排，三个及以上时垂直排列，Cancel 单独在最后
- `textFields` 可以在警告框里放输入框：

```cpp
static std::string name;
AlertTextField field{.placeholder = "Name", .text = &name};
Alert("Rename", &presented,
      {{"Cancel", ButtonRole::Cancel}, {"OK", ButtonRole::Default}},
      {.textFields = std::span<const AlertTextField>(&field, 1)});
```

`ConfirmationDialog` 是 macOS 的确认对话框（动作列表 + 自动追加 Cancel），
返回动作下标，Cancel 返回动作数量。

### Sheet

```cpp
bool presented = false;

if (Button("Edit…")) presented = true;

Sheet(&presented, {.width = 420.0f}, [] {
    Form([] {
        Section([] {
            TextField("Name", &name);
            Toggle("Enabled", &enabled);
        });
    });
    SheetFooter([] {
        Button("Cancel", {.role = ButtonRole::Cancel});
        Button("Save", {.role = ButtonRole::Default});
    });
});
```

`height = 0` 时 sheet 高度随内容；给固定高度时 `Form` 内部可以滚动。
`SheetFooter` 固定在底部，按钮自动有最小宽度。

### Popover

```cpp
bool presented = false;

Popover(&presented, {.size = ImVec2(240, 140),
                     .arrowEdge = ArrowEdge::Automatic},
    [&] { Button("Details"); },   // anchor
    []  { Text("Popover content"); });
```

点击面板外部或按 Escape 会关闭并重置 `presented`。

### Notification / Tooltip

```cpp
bool notify = true;
NotificationBanner(&notify, {
    .title = "Export complete",
    .subtitle = "Documents",
    .body = "3 files were saved.",
    .timestamp = "now",
}, {{"Show in Finder"}, {"Dismiss"}});
```

`NotificationBanner` 默认从工作区右上角滑入；将 `NotificationContent::placement` 设为 `NotificationPlacement::BottomRight` 可改为右下角；没有动作时 5 秒后自动消失。
`Help(text, content)` 给任意内容加悬停提示：

```cpp
Help("Save the current document", [] {
    Button("Save");
});
```

### 菜单

`controls/Menu.h`、`overlays/PopUpMenu.h` 提供菜单与弹出菜单；
在 `MenuBar` / `PopUpMenu` 的内容里，`Button` 会自动绘制成菜单项，
`Toggle` 会自动带勾选标记，`Picker` 会变成子菜单/内联分组。

---

## 8. 图标与颜色

### SF Symbols

```cpp
Image(Symbols::FolderFill, {.foreground = Foreground::Accent});
Button("Add", {.symbol = Symbols::Plus});
```

`Symbols.h` 里是 `inline constexpr unsigned` 常量，命名与 SF Symbols 一致
（`.` 换成首字母大写）。图标也可以放进 `Icon`：

```cpp
Icon icon{Symbols::GearshapeFill, IconPlate::Gray};
NavigationLink("General", &page, 0, {.icon = icon});
```

`IconPlate` 提供系统设置风格的圆角底板颜色。

### 颜色

```cpp
Rgba blue = Rgba::Hex(0x0A84FF);
Rgba translucent = Rgba(1.0f, 0.0f, 0.0f, 0.5f);
```

语义颜色通过 `Palette` 获取（`core/Theme.h`），大多数场景直接用
`Foreground` 枚举即可。主题色由 `Environment().accent` 决定。

---

## 9. 自定义绘制

需要画框架没有提供的图形时，用 `Canvas` 拿到最终的像素矩形：

```cpp
Canvas(ImVec2(120.0f, 80.0f), [](ImDrawList* draw, const ImRect& rect) {
    draw->AddRectFilled(rect.Min, rect.Max,
                        ImGui::GetColorU32(ImVec4(0.2f, 0.5f, 1.0f, 1.0f)),
                        6.0f * Cupertino::Environment().Scale());
});
```

`Background(paint, content)` 和 `Overlay(paint, content)` 可以在任意视图
背后/上方绘制，`paint` 的矩形是内容的最终像素范围：

```cpp
Background([](ImDrawList* draw, const ImRect& rect) {
    draw->AddRectFilled(rect.Min, rect.Max, IM_COL32(255, 255, 255, 20), 8.0f);
}, [] {
    Text("Card");
});
```

`Canvas` 的第三个参数是首行文本基线；没有文本时保持默认 `-1`。

---

## 10. 焦点、键盘与交互状态

- `fullKeyboardAccess = true`：Tab 遍历所有控件，Space 按下；默认只有输入框和列表获得焦点
- 文本字段的 `focusRing` 控制聚焦描边
- `WithInteractionPreview(...)` 可以强制绘制悬停/按下/聚焦状态，
  用于截图或设计预览
- `WithControlActiveState(...)` 模拟窗口不是 key/main 时的外观
- `Disabled(true, content)` 禁用整段内容并降低不透明度

```cpp
WithInteractionPreview(InteractionPreview::Hovered, [] {
    Button("Hovered preview");
});
```

---

## 11. 状态管理建议

框架本身不提供数据绑定或观察者，推荐做法：

1. **状态集中存放**：`AppState` 结构体或单例，生命周期长于帧；
2. **回调只改状态**：控件返回值只用于触发动作；
3. **列表使用稳定 ID**：`Id(item.id, ...)`，不要用易变的排序下标；
4. **异步任务回到主线程再改 UI 状态**：ImGui/Cupertino 都不是线程安全的；
5. **动画用 `ImGui::GetIO().DeltaTime`**，不要依赖固定帧率。

```cpp
struct AppState {
    bool dark = true;
    int page = 0;
    std::string name;
    std::vector<Item> items;
};
```

---

## 12. 常见陷阱

| 现象 | 原因与处理 |
|---|---|
| 控件点击无响应 | 被其他窗口/覆盖层遮挡；确认调用顺序，覆盖层应在内容之后 |
| 文本或控件闪烁一次 | 新视图树的第一帧在测量，属于正常现象；用 `Id` 保持身份可减少重建 |
| 布局回调执行两次 | 新容器的测量 pass；不要在回调里修改外部状态 |
| 中文显示为方框 | 设置 `uiFontRegular` / `uiFontBold` 指向中文字体 |
| SF Symbol 显示为问号 | 使用 `Symbols.h` 中存在的常量；未收录符号会回退 |
| 窗口内容被裁剪 | 检查是否在 `Window` 外绘制；`Window` 的内容应填满窗口 |
| 高 DPI 模糊 | 用 `Run` 时框架自动处理；手动宿主需每帧设置 `displayScale = glfwGetWindowContentScale` |
| 关闭窗口后崩溃 | `Window` 的 `open` 变为 `false` 后仍引用已释放的状态；先清理状态再退出循环 |
| 内容被红绿灯挡住 | 使用 `titleBar = true`，或改用 `NavigationSplitView` 并设 `titleBar = false` |
| 同时创建多个宿主 | GLFW/ImGui 是进程级全局状态；一个进程只创建一个 `Application` |
| 与原生 ImGui 混用错位 | 原生 ImGui 使用像素单位；在 Cupertino 内容里绘制时用 `Px()` 换算 |

---

## 13. 版本与兼容性

- 需要 Dear ImGui **docking 分支**；`imgui_internal.h` 的 API 不稳定，
  升级 ImGui 后请重新编译整个框架
- 建议固定 ImGui 提交，而不是跟随分支漂移
- 本项目在 Windows 10/11 + MSVC + OpenGL3 下验证；
  macOS 的 GLFW/OpenGL 后端同样可用，但字体回退路径需要自行配置

---

## 14. 更多示例

- `Showcase.cpp`：所有控件的实际用法，按 Buttons / Choices / Values /
  Text / Form / Lists / Boxes / Presentations / Layout / Charts 分类
- `examples/main.cpp`：最小 `Run` 应用，只写 UI
- `examples/showcase.cpp`：在原生窗口中打开完整 Showcase
- 各头文件顶部的注释：记录了对应 macOS/iOS 控件的设计来源与细节差异
