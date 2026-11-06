#pragma once

// CupertinoUi: macOS 15 Sequoia and iOS 18 interface for Dear ImGui with SwiftUI-style layout.
#include "controls/Button.h"
#include "controls/ColorPicker.h"
#include "controls/ComboBox.h"
#include "controls/DatePicker.h"
#include "controls/Gauge.h"
#include "controls/IconPlate.h"
#include "controls/ImageWell.h"
#include "controls/KeyRecorder.h"
#include "controls/LabeledContent.h"
#include "controls/LevelIndicator.h"
#include "controls/PathControl.h"
#include "controls/Menu.h"
#include "controls/Picker.h"
#include "controls/Progress.h"
#include "controls/Slider.h"
#include "controls/Stepper.h"
#include "controls/Text.h"
#include "controls/TextField.h"
#include "controls/Toggle.h"
#include "controls/TokenField.h"
#include "core/BakedFiles.h"
#include "core/Bitmap.h"
#include "core/Color.h"
#include "core/Draw.h"
#include "core/Environment.h"
#include "core/Interaction.h"
#include "core/KeyboardShortcut.h"
#include "core/Symbols.h"
#include "core/Theme.h"
#include "core/Typography.h"
#include "layout/Grid.h"
#include "layout/Layout.h"
#include "layout/ScrollView.h"
#include "layout/SplitView.h"
#include "layout/Stacks.h"
#include "overlays/Alert.h"
#include "overlays/Notification.h"
#include "overlays/Popover.h"
#include "overlays/Sheet.h"
#include "overlays/Tooltip.h"
#include "views/BorderedList.h"
#include "views/ContentUnavailable.h"
#include "views/Chart.h"
#include "views/Disclosure.h"
#include "views/Form.h"
#include "views/GroupBox.h"
#include "views/ScopeBar.h"
#include "views/Sidebar.h"
#include "views/Table.h"
#include "window/ColorPanel.h"
#include "window/MenuBar.h"
#include "window/Window.h"

#include <functional>
#include <memory>
#include <string>

namespace Cupertino {
    struct Configuration {
        // Folder with the SF Pro Text and Display and SF Mono OTF files and their derived symbol fonts; empty loads the
        // fonts baked into the library (tools/bake.py).
        std::string fontsDirectory;
        // CJK fallback faces used for text and display, since SF Pro has no
        // CJK glyphs. Empty paths keep the Microsoft YaHei defaults.
        std::string uiFontRegular;
        std::string uiFontBold;
        // Keyboard navigation (System Settings > Keyboard): Tab moves the focus through every control, which shows its
        // focus ring, and Space presses it. Off, as macOS ships, only fields and lists take the focus.
        bool fullKeyboardAccess = false;
    };

    // Loads the fonts and prepares ImGui; call once after ImGui::CreateContext().
    bool Initialize(const Configuration& configuration);

    // Call before ImGui::Render(): draws the tooltip and frees the state of views that disappeared.
    void EndFrame();

    struct ShowcaseOptions {
        // The pane to open first, by its slug ("buttons", "choices", "values", "text", "form", "lists", "boxes",
        // "presentations", "layout", "charts").
        const char* initialPane = nullptr;
        // The window's size and first position in points.
        ImVec2 size = ImVec2(760.0f, 540.0f);
        ImVec2 position = ImVec2(40.0f, 40.0f);
    };

    // Every view of the library in one window, a pane per kind in each of its styles and states, as ShowDemoWindow() is
    // for Dear ImGui; the close button sets open to false when it is given.
    void ShowShowcase(bool* open = nullptr, const ShowcaseOptions& options = {});

    // -----------------------------------------------------------------------
    // Native application host
    // -----------------------------------------------------------------------

    // The host creates one operating system window with a custom (undecorated)
    // frame, initializes GLFW, OpenGL3, Dear ImGui and the Cupertino fonts, and
    // runs the render loop. The content callback is called once per frame.
    struct ApplicationOptions {
        std::string title = "CupertinoUi";
        // Size in points when the window opens.
        ImVec2 size = ImVec2(960.0f, 640.0f);
        ImVec2 minSize = ImVec2(480.0f, 320.0f);
        // 0 keeps the size unbounded.
        ImVec2 maxSize = ImVec2(0.0f, 0.0f);
        Appearance appearance = Appearance::Dark;
        AccentColor accent = AccentColor::Blue;
        float zoom = 1.0f;
        bool resizable = true;
        bool closable = true;
        bool minimizable = true;
        bool zoomable = true;
        // Draw a title bar above the content. Set false for content that draws
        // its own toolbar, such as NavigationSplitView (the traffic lights then
        // sit over the content).
        bool titleBar = true;
        // False lets the content call Cupertino::Window itself; the first one
        // drawn in a frame becomes the native window frame. Most applications
        // leave this true and write the content only.
        bool wrapContent = true;
        bool compactTitleBar = false;
        bool fullKeyboardAccess = false;
        WindowToolbarStyle toolbarStyle = WindowToolbarStyle::Unified;
        // Menu titles drawn inside the window, below its title bar.
        std::function<void()> menuBar;
        // Return false to keep the window open (for example to ask about
        // unsaved changes). Empty always closes.
        std::function<bool()> onClose;
        // F12 opens the built-in debug tools page (UI item overlay, script
        // playback, item dump). The clickable-item probe is enabled with it.
        bool debugTools = false;
        // Start with the debug tools page visible.
        bool debugToolsOpen = false;
        // Show the numbered clickable-item overlay from the first frame.
        bool showUiItems = false;
        // Run a UI automation script (see debug/ui_script.hpp for commands).
        std::string uiScriptPath;
        // Dump the first frame's clickable items to this path.
        std::string uiDumpItemsPath;
        // Fonts passed to Cupertino::Initialize; empty uses the baked fonts.
        Configuration fonts;
    };

    // Owns the native window and the ImGui/OpenGL context.
    class Application {
    public:
        explicit Application(ApplicationOptions options = {});
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) noexcept;
        Application& operator=(Application&&) noexcept;

        // Initializes the window and runs until it closes. Returns 0 on
        // success, 1 when the window cannot be created, 2 when the fonts fail.
        int run(const std::function<void()>& content);

        // Closes the window from inside the content (thread: UI thread).
        void request_close();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // One-call form: creates the host, runs content, tears everything down.
    int Run(const ApplicationOptions& options, const std::function<void()>& content);
    // Default window (960 x 640, dark, title bar): the shortest entry point.
    int Run(const std::function<void()>& content);
} // namespace Cupertino
