// windows.h (pulled in by glfw3native.h) defines min/max macros that break std::min/std::max.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "Cupertino.h"

#include "debug/ui_probe.hpp"
#include "debug/ui_script.hpp"
#include "window/NativeHost.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace Cupertino {
    namespace {
        enum ResizeEdge {
            EdgeLeft = 1,
            EdgeRight = 2,
            EdgeTop = 4,
            EdgeBottom = 8,
        };

        // The display's scale as GLFW reports it, e.g. 1.5 on a 150 % display. Rounding it to a whole number
        // makes every size that many times too large for a window the system scaled by the real factor, which is
        // what used to overlap the controls on anything but 100 % and 200 %.
        float ContentScaleF(GLFWwindow* window) {
            float x = 1.0f;
            float y = 1.0f;
            glfwGetWindowContentScale(window, &x, &y);
            return std::max(1.0f, std::max(x, y));
        }

        int ContentScale(GLFWwindow* window) {
            return std::max(1, int(std::lround(ContentScaleF(window))));
        }

        // Absolute pointer position on the desktop. GLFW reports it relative to
        // the window, and the window moves under the pointer while dragging.
        void AbsoluteCursor(GLFWwindow* window, int& x, int& y) {
            int wx = 0;
            int wy = 0;
            double cx = 0.0;
            double cy = 0.0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetCursorPos(window, &cx, &cy);
            x = wx + int(std::lround(cx));
            y = wy + int(std::lround(cy));
        }

        void ApplyNativeChrome(GLFWwindow* window, bool dark) {
#ifdef _WIN32
            HWND hwnd = glfwGetWin32Window(window);
            if (!hwnd)
                return;
            // Keep the borderless window in the taskbar and Alt+Tab.
            const LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
            SetWindowLongA(hwnd, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            // Windows 11 rounds the frame; Windows 10 returns an error and
            // leaves the square window, which the square fill already covers.
            const BOOL use_dark = dark ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark, sizeof(use_dark));
            const int corner = 2; // DWMWCP_ROUND
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            // Windows 11 draws a one-pixel border around the frame by default;
            // it shows as a black line on the dark theme.
            const COLORREF no_border = DWMWA_COLOR_NONE;
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &no_border, sizeof(no_border));
#else
            (void)window;
            (void)dark;
#endif
        }

        GLFWmonitor* MonitorForWindow(GLFWwindow* window) {
            int wx = 0;
            int wy = 0;
            int ww = 0;
            int wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            const int cx = wx + ww / 2;
            const int cy = wy + wh / 2;
            int count = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&count);
            for (int i = 0; i < count; ++i) {
                int x = 0;
                int y = 0;
                int w = 0;
                int h = 0;
                glfwGetMonitorWorkarea(monitors[i], &x, &y, &w, &h);
                if (w > 0 && h > 0 && cx >= x && cx < x + w && cy >= y && cy < y + h)
                    return monitors[i];
            }
            return glfwGetPrimaryMonitor();
        }

        // Reads the back buffer before glfwSwapBuffers and writes a binary PPM.
        bool WriteScreenshot(const std::filesystem::path& path, int width, int height) {
            if (width <= 0 || height <= 0)
                return false;
            std::vector<unsigned char> pixels(size_t(width) * size_t(height) * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out << "P6\n" << width << ' ' << height << "\n255\n";
            for (int y = height - 1; y >= 0; --y) {
                for (int x = 0; x < width; ++x) {
                    const unsigned char* pixel = &pixels[(size_t(y) * size_t(width) + size_t(x)) * 4];
                    out.write(reinterpret_cast<const char*>(pixel), 3);
                }
            }
            return bool(out);
        }
    } // namespace

    struct Application::Impl {
        ApplicationOptions options;
        GLFWwindow* window = nullptr;
        bool closed = false;
        bool open = true;

        // Native chrome state.
        bool dragging = false;
        bool resizing = false;
        // Tracks button transitions so a control drag entering the edge cannot start a window resize.
        bool left_button_down = false;
        bool maximized = false;
        int hover_edges = 0;
        int resize_edges = 0;
        int drag_start_window_x = 0;
        int drag_start_window_y = 0;
        int drag_start_cursor_x = 0;
        int drag_start_cursor_y = 0;
        int resize_start_window_x = 0;
        int resize_start_window_y = 0;
        int resize_start_width = 0;
        int resize_start_height = 0;
        int resize_start_cursor_x = 0;
        int resize_start_cursor_y = 0;
        int restore_x = 0;
        int restore_y = 0;
        int restore_width = 0;
        int restore_height = 0;
        int region_width = -1;
        int region_height = -1;
        float region_scale = -1.0f;
        bool region_maximized = false;

        // Built-in debug tools page (F12).
        std::unique_ptr<debug::UiScript> script;
        bool debug_open = false;
        bool probe_overlay = false;
        std::string script_path;
        std::string debug_status = "Idle";
        int frame = 0;
        int exit_code = 0;

        void LoadScript() {
            script.reset();
            if (script_path.empty()) {
                debug_status = "Enter a script path.";
                return;
            }
            auto next = std::make_unique<debug::UiScript>();
            std::string error;
            if (!next->load(script_path, &error)) {
                debug_status = error;
                std::fprintf(stderr, "ui-script: %s\n", error.c_str());
                return;
            }
            script = std::move(next);
            debug::set_ui_probe_enabled(true);
            debug_status = "Running " + script_path;
        }

        void StopScript() {
            script.reset();
            debug_status = "Stopped.";
        }

        void DumpItems() {
            std::string error;
            if (debug::dump_ui_probe_items("ui_items.txt", &error))
                debug_status = "Wrote ui_items.txt";
            else
                debug_status = error;
        }

        void DrawDebugTools() {
            if (!debug_open)
                return;
            WindowOptions options;
            options.size = ImVec2(560.0f, 460.0f);
            options.position = ImVec2(72.0f, 72.0f);
            options.minSize = ImVec2(460.0f, 340.0f);
            options.floating = true;
            Window("Debug Tools", &debug_open, options, [this] {
                Form([this] {
                    Section({.header = "UI automation",
                             .footer = "Scripts use logical client coordinates. F12 toggles this page."}, [this] {
                        Toggle("Show clickable items", &probe_overlay);
                        TextField("Script path", &script_path, {.placeholder = "path/to/script.txt"});
                        HStack({.spacing = 8.0f}, [this] {
                            if (Button("Load & Run", {.role = ButtonRole::Default}))
                                LoadScript();
                            if (Button("Stop"))
                                StopScript();
                            if (Button("Dump items"))
                                DumpItems();
                        });
                    });
                    Section({.header = "Status"}, [this] {
                        Text(debug_status, {.foreground = Foreground::Secondary, .wraps = true});
                        LabeledContent("Frames", std::to_string(frame));
                    });
                });
            });
        }

        // Windows 10 has no DWM corner rounding, so clip the borderless window
        // with a rounded region as well. Windows 11 then clips the same shape
        // twice with the same radius.
        void UpdateWindowRegion() {
#ifdef _WIN32
            if (!window)
                return;
            int width = 0;
            int height = 0;
            glfwGetWindowSize(window, &width, &height);
            if (width <= 0 || height <= 0)
                return;
            const float scale = ContentScaleF(window);
            if (width == region_width && height == region_height && scale == region_scale && maximized == region_maximized)
                return;
            region_width = width;
            region_height = height;
            region_scale = scale;
            region_maximized = maximized;
            HWND hwnd = glfwGetWin32Window(window);
            if (!hwnd)
                return;
            if (maximized) {
                SetWindowRgn(hwnd, nullptr, TRUE);
                return;
            }
            // Metrics::Window().radius is 10 points; keep the clip in step with the precisely scaled drawing.
            const int radius = std::max(1, int(std::lround(10.0f * scale)));
            HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
            if (region)
                SetWindowRgn(hwnd, region, TRUE);
#endif
        }

        void RequestClose() {
            closed = true;
            if (window)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int EdgeAtCursor(int border) const {
            if (!window)
                return 0;
            double cx = 0.0;
            double cy = 0.0;
            int w = 0;
            int h = 0;
            glfwGetCursorPos(window, &cx, &cy);
            glfwGetWindowSize(window, &w, &h);
            if (cx < 0.0 || cy < 0.0 || cx >= w || cy >= h)
                return 0;
            int edges = 0;
            if (cx < border)
                edges |= EdgeLeft;
            if (cx >= w - border)
                edges |= EdgeRight;
            if (cy < border)
                edges |= EdgeTop;
            if (cy >= h - border)
                edges |= EdgeBottom;
            return edges;
        }

        void RestoreFromZoom() {
            if (!maximized)
                return;
            glfwSetWindowSize(window, restore_width, restore_height);
            glfwSetWindowPos(window, restore_x, restore_y);
            maximized = false;
            dragging = false;
            resizing = false;
        }

        void ToggleZoom() {
            if (maximized) {
                RestoreFromZoom();
                return;
            }
            glfwGetWindowPos(window, &restore_x, &restore_y);
            glfwGetWindowSize(window, &restore_width, &restore_height);
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
            glfwGetMonitorWorkarea(MonitorForWindow(window), &x, &y, &w, &h);
            if (w <= 0 || h <= 0)
                return;
            glfwSetWindowSize(window, w, h);
            glfwSetWindowPos(window, x, y);
            maximized = true;
            dragging = false;
            resizing = false;
            hover_edges = 0;
        }

        void BeginDrag() {
            if (!window || resizing)
                return;
            if (maximized)
                RestoreFromZoom();
            dragging = true;
            hover_edges = 0;
            glfwGetWindowPos(window, &drag_start_window_x, &drag_start_window_y);
            AbsoluteCursor(window, drag_start_cursor_x, drag_start_cursor_y);
        }

        void UpdateDrag() {
            if (!dragging || !window)
                return;
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
                dragging = false;
                return;
            }
            int cursor_x = 0;
            int cursor_y = 0;
            AbsoluteCursor(window, cursor_x, cursor_y);
            glfwSetWindowPos(window,
                             drag_start_window_x + cursor_x - drag_start_cursor_x,
                             drag_start_window_y + cursor_y - drag_start_cursor_y);
        }

        void UpdateResize() {
            if (!window)
                return;
            const bool left_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool left_pressed = left_down && !left_button_down;
            left_button_down = left_down;
            if (resizing) {
                if (!left_down) {
                    resizing = false;
                    return;
                }
                int cursor_x = 0;
                int cursor_y = 0;
                AbsoluteCursor(window, cursor_x, cursor_y);
                const int dx = cursor_x - resize_start_cursor_x;
                const int dy = cursor_y - resize_start_cursor_y;
                int x = resize_start_window_x;
                int y = resize_start_window_y;
                int w = resize_start_width;
                int h = resize_start_height;
                if (resize_edges & EdgeLeft) {
                    x += dx;
                    w -= dx;
                }
                if (resize_edges & EdgeRight)
                    w += dx;
                if (resize_edges & EdgeTop) {
                    y += dy;
                    h -= dy;
                }
                if (resize_edges & EdgeBottom)
                    h += dy;

                const int min_w = std::max(1, int(options.minSize.x));
                const int min_h = std::max(1, int(options.minSize.y));
                if (w < min_w) {
                    if (resize_edges & EdgeLeft)
                        x -= min_w - w;
                    w = min_w;
                }
                if (h < min_h) {
                    if (resize_edges & EdgeTop)
                        y -= min_h - h;
                    h = min_h;
                }
                const int max_w = options.maxSize.x > 0.0f ? int(options.maxSize.x) : 0;
                const int max_h = options.maxSize.y > 0.0f ? int(options.maxSize.y) : 0;
                if (max_w > 0 && w > max_w) {
                    if (resize_edges & EdgeLeft)
                        x += w - max_w;
                    w = max_w;
                }
                if (max_h > 0 && h > max_h) {
                    if (resize_edges & EdgeTop)
                        y += h - max_h;
                    h = max_h;
                }

                glfwSetWindowSize(window, w, h);
                glfwSetWindowPos(window, x, y);
                return;
            }

            hover_edges = 0;
            if (!options.resizable || maximized || dragging)
                return;
            if (!glfwGetWindowAttrib(window, GLFW_HOVERED))
                return;
            // A corner needs both edge bands to overlap; keep the hit area large enough to find reliably at every DPI.
            const int grip = std::max(4, int(std::ceil(8.0f * ContentScaleF(window))));
            const int edges = EdgeAtCursor(grip);
            hover_edges = edges;
            if (edges != 0 && left_pressed) {
                resizing = true;
                resize_edges = edges;
                dragging = false;
                glfwGetWindowPos(window, &resize_start_window_x, &resize_start_window_y);
                glfwGetWindowSize(window, &resize_start_width, &resize_start_height);
                AbsoluteCursor(window, resize_start_cursor_x, resize_start_cursor_y);
            }
        }

        void ApplyResizeCursor() {
            // A scroll bar or another control owns the pointer over its hit target; only show the host resize cursor
            // when the edge is otherwise empty, or while the host already owns an active resize drag.
            if (!resizing && (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                              ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive()))
                return;
            const int edges = resizing ? resize_edges : hover_edges;
            if (edges == 0)
                return;
            const bool left = (edges & EdgeLeft) != 0;
            const bool right = (edges & EdgeRight) != 0;
            const bool top = (edges & EdgeTop) != 0;
            const bool bottom = (edges & EdgeBottom) != 0;
            if ((left || right) && (top || bottom))
                ImGui::SetMouseCursor((left && top) || (right && bottom) ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
            else if (left || right)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            else
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
    };

    Application::Application(ApplicationOptions options)
        : impl_(std::make_unique<Impl>()) {
        impl_->options = std::move(options);
    }

    Application::~Application() = default;
    Application::Application(Application&&) noexcept = default;
    Application& Application::operator=(Application&&) noexcept = default;

    void Application::request_close() {
        if (impl_)
            impl_->RequestClose();
    }

    int Application::run(const std::function<void()>& content) {
        if (!impl_)
            return 1;
        Impl& state = *impl_;
        state.closed = false;
        state.open = true;

        glfwSetErrorCallback([](int code, const char* description) {
            std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
        });
        if (!glfwInit())
            return 1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        // The application draws its own frame and handles resizing itself.
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

        state.window = glfwCreateWindow(std::max(1, int(state.options.size.x)),
                                        std::max(1, int(state.options.size.y)),
                                        state.options.title.c_str(), nullptr, nullptr);
        if (!state.window) {
            glfwTerminate();
            return 1;
        }
        glfwSetWindowSizeLimits(state.window,
                                std::max(1, int(state.options.minSize.x)),
                                std::max(1, int(state.options.minSize.y)),
                                state.options.maxSize.x > 0.0f ? int(state.options.maxSize.x) : GLFW_DONT_CARE,
                                state.options.maxSize.y > 0.0f ? int(state.options.maxSize.y) : GLFW_DONT_CARE);
        // Center on the monitor that contains the pointer's current monitor.
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        int work_x = 0;
        int work_y = 0;
        int work_w = 0;
        int work_h = 0;
        glfwGetMonitorWorkarea(monitor, &work_x, &work_y, &work_w, &work_h);
        if (work_w > 0 && work_h > 0) {
            int width = 0;
            int height = 0;
            glfwGetWindowSize(state.window, &width, &height);
            glfwSetWindowPos(state.window, work_x + (work_w - width) / 2, work_y + (work_h - height) / 2);
        }

        glfwMakeContextCurrent(state.window);
        glfwSwapInterval(1);
        ApplyNativeChrome(state.window, state.options.appearance == Appearance::Dark);
        state.UpdateWindowRegion();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        ImGui_ImplGlfw_InitForOpenGL(state.window, true);
        ImGui_ImplOpenGL3_Init("#version 330");

        Configuration fonts = state.options.fonts;
        fonts.fullKeyboardAccess = state.options.fullKeyboardAccess;
        if (!Cupertino::Initialize(fonts)) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(state.window);
            state.window = nullptr;
            glfwTerminate();
            std::fprintf(stderr, "CupertinoUi font initialization failed\n");
            return 2;
        }

        const bool debug_enabled = state.options.debugTools || state.options.showUiItems ||
                                   !state.options.uiScriptPath.empty() || !state.options.uiDumpItemsPath.empty();
        if (debug_enabled)
            debug::set_ui_probe_enabled(true);
        state.debug_open = state.options.debugTools && state.options.debugToolsOpen;
        state.probe_overlay = state.options.showUiItems;
        if (!state.options.uiScriptPath.empty()) {
            state.script_path = state.options.uiScriptPath;
            state.LoadScript();
        }

        // Initial theme. Content may change these through Environment() later;
        // the loop only refreshes the DPI scale.
        EnvironmentValues& initial = Environment();
        initial.displayScale = ContentScaleF(state.window);
        initial.zoom = state.options.zoom;
        initial.appearance = state.options.appearance;
        initial.accent = state.options.accent;
        initial.wallpaperTint = state.options.appearance == Appearance::Dark
                                    ? Rgba(0.96f, 0.95f, 1.05f)
                                    : Rgba(0.985f, 0.978f, 0.995f);
        Appearance last_appearance = state.options.appearance;

        NativeHost::Install({
            .close = [this] {
                if (impl_->options.onClose && !impl_->options.onClose())
                    return;
                impl_->open = false;
                request_close();
            },
            .minimize = [this] {
                if (impl_ && impl_->window)
                    glfwIconifyWindow(impl_->window);
            },
            .zoom = [this] {
                if (impl_)
                    impl_->ToggleZoom();
            },
            .begin_drag = [this] {
                if (impl_)
                    impl_->BeginDrag();
            },
        });

        while (!state.closed && !glfwWindowShouldClose(state.window)) {
            glfwPollEvents();
            state.UpdateDrag();
            if (!state.dragging)
                state.UpdateResize();
            state.UpdateWindowRegion();
            NativeHost::SetResizing(state.resizing);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            if (state.script)
                state.script->tick(ImGui::GetIO());
            debug::clear_ui_probe_items();
            ImGui::NewFrame();

            if (state.options.debugTools && ImGui::IsKeyPressed(ImGuiKey_F12, false))
                state.debug_open = !state.debug_open;

            EnvironmentValues& environment = Environment();
            environment.displayScale = ContentScaleF(state.window);
            if (std::getenv("CUPERTINO_DPI_DEBUG")) {
                int w = 0, h = 0, fw = 0, fh = 0;
                float sx = 0.0f, sy = 0.0f;
                glfwGetWindowSize(state.window, &w, &h);
                glfwGetFramebufferSize(state.window, &fw, &fh);
                glfwGetWindowContentScale(state.window, &sx, &sy);
                std::fprintf(stderr, "[dpi] window %dx%d framebuffer %dx%d contentScale %.2f,%.2f -> displayScale %d\n",
                             w, h, fw, fh, sx, sy, int(environment.displayScale));
            }
            // Debug: CUPERTINO_SCALE forces a DPI scale, for checking a HiDPI layout on a 100 % display.
            if (const char* forced = std::getenv("CUPERTINO_SCALE"))
                environment.displayScale = std::max(1.0f, float(std::atof(forced)));
            environment.controlActiveState = glfwGetWindowAttrib(state.window, GLFW_FOCUSED)
                                                 ? ControlActiveState::Key
                                                 : ControlActiveState::Inactive;
            if (environment.appearance != last_appearance) {
                last_appearance = environment.appearance;
                ApplyNativeChrome(state.window, last_appearance == Appearance::Dark);
                environment.wallpaperTint = last_appearance == Appearance::Dark
                                                ? Rgba(0.96f, 0.95f, 1.05f)
                                                : Rgba(0.985f, 0.978f, 0.995f);
            }

            NativeHost::ResetFrame();
            if (content) {
                if (state.options.wrapContent) {
                    WindowOptions window;
                    window.size = state.options.size;
                    window.position = ImVec2(0.0f, 0.0f);
                    window.closable = state.options.closable;
                    window.minimizable = state.options.minimizable;
                    window.zoomable = state.options.zoomable;
                    window.compactTitleBar = state.options.compactTitleBar;
                    window.toolbarStyle = state.options.toolbarStyle;
                    window.minSize = state.options.minSize;
                    window.maxSize = state.options.maxSize;
                    window.style = state.options.titleBar ? WindowStyle::TitleBar : WindowStyle::Automatic;
                    window.menuBar = state.options.menuBar;
                    Window(state.options.title.c_str(), &state.open, window, content);
                } else {
                    content();
                }
            }

            state.DrawDebugTools();
            if (state.probe_overlay)
                debug::draw_ui_probe_overlay();
            if (!state.options.uiDumpItemsPath.empty() && state.frame >= 5) {
                std::string error;
                if (debug::dump_ui_probe_items(state.options.uiDumpItemsPath, &error))
                    debug::clear_ui_probe_items();
                else
                    std::fprintf(stderr, "ui-dump-items: %s\n", error.c_str());
                state.options.uiDumpItemsPath.clear();
            }
            if (state.script && state.script->quit_requested()) {
                state.exit_code = state.script->exit_code();
                state.debug_status = state.exit_code == 0 ? "Script finished." : "Script failed.";
                state.script.reset();
                state.RequestClose();
            }
            ++state.frame;

            state.ApplyResizeCursor();
            Cupertino::EndFrame();
            ImGui::Render();

            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(state.window, &width, &height);
            glViewport(0, 0, width, height);
            if (Environment().IsDark())
                glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
            else
                glClearColor(0.95f, 0.95f, 0.96f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (state.script) {
                if (auto shot = state.script->pending_screenshot()) {
                    const bool ok = WriteScreenshot(*shot, width, height);
                    state.script->notify_screenshot(ok);
                }
            }
            glfwSwapBuffers(state.window);
        }

        debug::set_ui_probe_enabled(false);
        NativeHost::Uninstall();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(state.window);
        state.window = nullptr;
        glfwTerminate();
        return state.exit_code;
    }

    int Run(const ApplicationOptions& options, const std::function<void()>& content) {
        Application application(options);
        return application.run(content);
    }

    int Run(const std::function<void()>& content) {
        return Run(ApplicationOptions{}, content);
    }
} // namespace Cupertino
