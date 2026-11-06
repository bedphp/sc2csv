#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct ImGuiIO;

namespace Cupertino::debug {

// Plays a small text script by injecting ImGui input events. It never moves
// the OS cursor, so it can drive the workbench while the machine is in use.
//
// Commands (one per line, '#' starts a comment):
//   wait N                     wait N frames
//   move X Y                   move the virtual pointer
//   click X Y                  left click
//   doubleclick X Y            double click
//   rightclick X Y             right click
//   drag X1 Y1 X2 Y2           press, move, release (splitter resizing)
//   scroll DX DY               wheel
//   key ctrl+s                 key / shortcut
//   text hello                 UTF-8 text input
//   click_item "window" N      click the Nth item recorded in that window
//   click_id 0x1234ABCD        click the item with that ImGui ID
//   screenshot path.ppm        write a screenshot without quitting
//   dump_items path.txt        dump the last frame's clickable items
//   quit                       finish early
class UiScript {
public:
    UiScript();
    ~UiScript();
    UiScript(const UiScript&) = delete;
    UiScript& operator=(const UiScript&) = delete;

    bool load(const std::filesystem::path& path, std::string* error);
    bool active() const;

    // Call every frame after the backend's NewFrame() and before ImGui's
    // NewFrame(); it queues mouse/keyboard events for this frame.
    void tick(ImGuiIO& io);

    // Screenshot commands are handed to the main loop after rendering.
    std::optional<std::filesystem::path> pending_screenshot() const;
    void notify_screenshot(bool ok);

    bool quit_requested() const;
    int exit_code() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Cupertino::debug
