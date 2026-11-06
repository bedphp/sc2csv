# 第三方组件与素材说明

## 源码

CupertinoUi 的源码基于：

- **imgui-apple** — https://github.com/MisterJerkins/imgui-apple
  - MIT License，Copyright (c) 2026 MisterJerkins
  - 本目录中的 `Cupertino.cpp/.h`、`Showcase.cpp`、`controls/`、`core/`、
    `layout/`、`overlays/`、`views/`、`window/` 均源自该项目
  - 完整许可证文本见 `LICENSE`

## 依赖

构建时会通过 CMake `FetchContent` 获取：

- **Dear ImGui**（docking 分支）— https://github.com/ocornut/imgui
  - MIT License
- **GLFW** 3.4 — https://github.com/glfw/glfw
  - zlib/libpng License

## 字体与符号

`core/generated/` 中的 `BakedFonts.inc`、`KerningTables.inc`、
`SymbolLayers.inc` 由 SF Pro、SF Mono 与 SF Symbols 生成，用于让框架
无需外部字体文件即可显示 macOS 风格界面。

**SF Pro、SF Mono 和 SF Symbols 是 Apple Inc. 的商标与受版权保护的素材。**
它们受 Apple 的字体与符号使用条款约束，不属于本项目 MIT 许可证的授权范围。
随本仓库分发这些烘焙数据仅用于开发、测试与演示。若要在商业产品中使用，
请自行确认并遵守 Apple 的相关授权条款，或替换为你有权分发的字体。

## stb_image

`third_party/stb/stb_image.h` — https://github.com/nothings/stb
- Public Domain（MIT 双许可），作者 Sean Barrett 及贡献者
