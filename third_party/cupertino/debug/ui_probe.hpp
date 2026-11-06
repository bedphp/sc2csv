#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Cupertino::debug {

// A clickable rectangle registered with ImGui during the last UI frame.
// Cupertino's Interaction::Button reports every control through a function
// pointer; the few direct InvisibleButton sites call record_last_item().
struct UiProbeItem {
    ImGuiID id = 0;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool disabled = false;
    std::string window;
};

void set_ui_probe_enabled(bool enabled);
bool ui_probe_enabled();

// Called by Cupertino's Interaction probe hook (adapted to plain floats so
// this header does not pull in imgui_internal.h).
void record_ui_item(ImGuiID id, float min_x, float min_y, float max_x,
                    float max_y);
// Call right after a direct ImGui item (InvisibleButton, Selectable, ...).
void record_last_item();

const std::vector<UiProbeItem>& ui_probe_items();
void clear_ui_probe_items();

// Draws numbered rectangles over the current frame's items. Call after the
// frame's UI has been drawn and before ImGui::Render().
void draw_ui_probe_overlay();

// Writes the last frame's items as text: one section per window, then
// "index  id  x y w h" lines using logical screen coordinates.
bool dump_ui_probe_items(const std::filesystem::path& path, std::string* error);

}  // namespace Cupertino::debug
