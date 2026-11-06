#include "NativeHost.h"

namespace Cupertino::NativeHost {
    namespace {
        bool g_active = false;
        bool g_claimed = false;
        bool g_resizing = false;
        Actions g_actions;
    }

    void Install(Actions actions) {
        g_actions = std::move(actions);
        g_active = true;
        g_claimed = false;
    }

    void Uninstall() {
        g_active = false;
        g_claimed = false;
        g_resizing = false;
        g_actions = {};
    }

    bool Active() {
        return g_active;
    }

    bool ClaimMainWindow() {
        if (!g_active || g_claimed)
            return false;
        g_claimed = true;
        return true;
    }

    void ResetFrame() {
        g_claimed = false;
    }

    void RequestClose() {
        if (g_actions.close)
            g_actions.close();
    }

    void Minimize() {
        if (g_actions.minimize)
            g_actions.minimize();
    }

    void Zoom() {
        if (g_actions.zoom)
            g_actions.zoom();
    }

    void BeginDrag() {
        if (g_actions.begin_drag)
            g_actions.begin_drag();
    }

    void SetResizing(bool resizing) {
        g_resizing = resizing;
    }

    bool Resizing() {
        return g_resizing;
    }
} // namespace Cupertino::NativeHost
