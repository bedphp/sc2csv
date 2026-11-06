#pragma once

// Internal bridge between the Cupertino views and the native window host in
// app/Application.cpp. Applications do not use this header directly.

#include <functional>

namespace Cupertino::NativeHost {
    struct Actions {
        std::function<void()> close;
        std::function<void()> minimize;
        std::function<void()> zoom;
        // Starts moving the native window; the pointer's motion is read by the host.
        std::function<void()> begin_drag;
    };

    // Installs the host callbacks and turns native-window mode on.
    void Install(Actions actions);
    void Uninstall();

    bool Active();

    // The first Cupertino window drawn in a frame replaces the native window's
    // frame; later ones are ordinary floating windows inside it.
    bool ClaimMainWindow();
    // Called by the host once per frame, before drawing the content.
    void ResetFrame();

    void RequestClose();
    void Minimize();
    void Zoom();
    void BeginDrag();

    // Set by the host while the pointer drags a window edge. The chrome must
    // not start moving the window in that frame.
    void SetResizing(bool resizing);
    bool Resizing();
} // namespace Cupertino::NativeHost
