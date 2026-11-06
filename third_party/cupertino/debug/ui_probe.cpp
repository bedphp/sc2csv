#include "ui_probe.hpp"

#include "core/Interaction.h"

#include "imgui_internal.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <unordered_map>

namespace Cupertino::debug {
namespace {

bool g_enabled = false;
std::vector<UiProbeItem> g_items;

void InteractionProbe(ImGuiID id, const ImRect& rect) {
    record_ui_item(id, rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
}

}  // namespace

void set_ui_probe_enabled(bool enabled) {
    g_enabled = enabled;
    if (!enabled) g_items.clear();
    Cupertino::Interaction::g_probeCallback =
        enabled ? &InteractionProbe : nullptr;
}

bool ui_probe_enabled() {
    return g_enabled;
}

void record_ui_item(ImGuiID id, float min_x, float min_y, float max_x,
                    float max_y) {
    if (!g_enabled) return;
    UiProbeItem item;
    item.id = id;
    item.min_x = min_x;
    item.min_y = min_y;
    item.max_x = max_x;
    item.max_y = max_y;
    item.disabled =
        (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window && window->Name) item.window = window->Name;
    g_items.push_back(std::move(item));
}

void record_last_item() {
    if (!g_enabled) return;
    const ImGuiLastItemData& data = GImGui->LastItemData;
    record_ui_item(data.ID, data.Rect.Min.x, data.Rect.Min.y,
                   data.Rect.Max.x, data.Rect.Max.y);
}

const std::vector<UiProbeItem>& ui_probe_items() {
    return g_items;
}

void clear_ui_probe_items() {
    g_items.clear();
}

void draw_ui_probe_overlay() {
    if (!g_enabled || g_items.empty()) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImU32 normal = IM_COL32(0, 255, 150, 120);
    const ImU32 hovered = IM_COL32(255, 210, 0, 230);
    const ImU32 disabled = IM_COL32(170, 170, 170, 90);
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    // Match click_item: the number is the item's position inside its window,
    // not the global draw order.
    std::unordered_map<std::string, int> window_counts;
    for (const UiProbeItem& item : g_items) {
        const int window_index = window_counts[item.window]++;
        const bool under_mouse =
            mouse.x >= item.min_x && mouse.x <= item.max_x &&
            mouse.y >= item.min_y && mouse.y <= item.max_y;
        const ImU32 color = item.disabled ? disabled
                                         : under_mouse ? hovered : normal;
        draw->AddRect(ImVec2(item.min_x, item.min_y),
                      ImVec2(item.max_x, item.max_y), color, 0.0f, 0,
                      under_mouse ? 2.0f : 1.0f);
        char label[48];
        std::snprintf(label, sizeof(label), "w%d %08X", window_index,
                      item.id);
        draw->AddText(font, font_size,
                      ImVec2(item.min_x + 1.0f, item.min_y + 1.0f), color,
                      label);
    }
}

bool dump_ui_probe_items(const std::filesystem::path& path,
                         std::string* error) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path.string();
        return false;
    }
    out << "# CupertinoUi ui items; logical screen coordinates\n";
    out << "# index is per window and matches click_item \"window\" index\n";
    out << "# index  id        x       y       w       h\n";
    std::string current_window;
    std::unordered_map<std::string, int> window_counts;
    for (const UiProbeItem& item : g_items) {
        if (item.window != current_window) {
            current_window = item.window;
            out << "\nwindow \"" << current_window << "\"\n";
        }
        const int index = window_counts[item.window]++;
        out << std::setw(5) << index << "  0x" << std::hex << std::setw(8)
            << std::setfill('0') << item.id << std::dec << std::setfill(' ')
            << std::fixed << std::setprecision(1) << "  " << item.min_x
            << "  " << item.min_y << "  " << (item.max_x - item.min_x)
            << "  " << (item.max_y - item.min_y);
        if (item.disabled) out << "  disabled";
        out << '\n';
    }
    if (!out) {
        if (error) *error = "failed while writing " + path.string();
        return false;
    }
    if (error) error->clear();
    return true;
}

}  // namespace Cupertino::debug
