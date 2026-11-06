#pragma once

#include <functional>

namespace Cupertino {
    struct MenuBarOptions {
        // Text at the trailing end, such as the date and time.
        const char* status = nullptr;
    };

    struct MenuBarWindowOptions {
        // The first titled menu is drawn bold, as macOS bolds the application's menu beside the Apple menu. A bar of
        // plain titles (File, Edit, ...) turns this off.
        bool boldFirstMenu = true;
    };

    // The menu bar across the top of the screen. The Menu calls inside content become its titles in order: a menu with
    // a symbol shows the symbol (the Apple menu), the first titled menu is the application's, in bold. The keyboard
    // shortcuts of all their items work while the bar is shown.
    void MenuBar(const MenuBarOptions& options, const std::function<void()>& content);
    // A menu bar laid out inside the current window, reserving its height in the view layout. The Menu calls inside
    // content become its titles in order; use this after a window's title bar to keep it separate from the 28 pt title.
    void MenuBarInWindow(const std::function<void()>& content);
    void MenuBarInWindow(const MenuBarWindowOptions& options, const std::function<void()>& content);
    // A title that runs an action directly instead of opening a menu.
    void MenuBarAction(const char* label, const std::function<void()>& action);
    // A title with a checkbox: a click flips the value instead of opening a menu. Returns true on the frame it changed.
    bool MenuBarToggle(const char* label, bool* is_on);
} // namespace Cupertino
