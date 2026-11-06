#include "MenuBar.h"

#include "controls/Bezel.h"
#include "core/Draw.h"
#include "core/Environment.h"
#include "core/Interaction.h"
#include "core/KeyboardShortcut.h"
#include "core/MenuContent.h"
#include "core/Metrics.h"
#include "core/Theme.h"
#include "core/Typography.h"
#include "layout/Layout.h"
#include "overlays/PopUpMenu.h"

#include "imgui_internal.h"

namespace Cupertino {
    // The bar being filled: its frame, where the next title goes, whether the application's title is placed and whether
    // the first title is drawn as the application's (bold).
    struct BarFill {
        ImDrawList* draw = nullptr;
        ImRect bar;
        float x = 0.0f;
        bool applicationPlaced = false;
        bool boldFirst = true;
        // Whether the open menu's own title was drawn this frame: a title whose text changed (the mode menu shows the
        // current mode) gets a new id, and its menu would otherwise stay open with nothing left to close it.
        bool ownsOpenMenu = false;
    };

    static BarFill* filling = nullptr;
    // The menu open from the bar, 0 when none.
    static ImGuiID openMenu = 0;

    // A menu of the bar: its title in order along the bar, and its menu while open.
    static void BarMenu(const char* label, unsigned symbol, const std::function<void()>& content) {
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        const Palette& colors = Theme::Colors();
        BarFill& fill = *filling;
        const ImGuiID id = ImGui::GetID(label);
        const bool application = !symbol && !fill.applicationPlaced && fill.boldFirst;
        fill.applicationPlaced |= !symbol;
        const Font font = application ? Font::Style(TextStyle::Body).Weight(FontWeight::Bold) : Font::Style(TextStyle::Body);
        const Font symbol_font = Font::System(metrics.symbolSize);
        const std::string_view title = Interaction::VisibleLabel(label);
        // Each title takes its width in whole points (the titles of the 512 Pixels captures drift from fractional ones).
        const float width = symbol ? Typography::SymbolWidth(symbol, symbol_font) : Typography::Width(font, title);
        const ImRect item(fill.x, fill.bar.Min.y, fill.x + Px(ImCeil(Pt(width)) + 2.0f * metrics.titlePadding), fill.bar.Max.y);
        fill.x = item.Max.x;
        const ImRect plate(item.Min.x - Px(metrics.plateOutset), item.Min.y + Px(metrics.plateInset), item.Max.x + Px(metrics.plateOutset), item.Max.y - Px(metrics.plateInset));
        // The bar titles handle input manually, so report them to the debug probe too.
        if (Interaction::g_probeCallback)
            Interaction::g_probeCallback(id, item);

        // A click opens the menu under its title; while one is open the pointer moving onto another title opens that
        // one instead. A menu that is already fading out takes no input, so a click on its title opens it again rather
        // than landing in a menu that ignores it.
        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = item.Contains(io.MousePos);
        const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
        bool open = PopUpMenu::IsOpen(id);
        const bool closing = open && PopUpMenu::IsClosing(id);
        const bool other_open = openMenu && openMenu != id && PopUpMenu::IsOpen(openMenu);
        if (id == openMenu)
            fill.ownsOpenMenu = true;
        if (hovered && ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (!open || closing)) || (moved && other_open))) {
            if (other_open)
                PopUpMenu::Close(openMenu);
            if (closing)
                PopUpMenu::Close(id);
            const ImRect anchor(plate.Min.x + Px(metrics.menuInset), plate.Min.y, plate.Max.x, fill.bar.Max.y + Px(metrics.menuGap));
            PopUpMenu::Open(id, anchor, -1, PopUpMenu::Placement::MenuBar);
            openMenu = id;
            fill.ownsOpenMenu = true;
            open = true;
        }
        // Like the titles that run an action, a menu's title takes a plate while the pointer is on it, so the whole bar
        // answers the pointer the same way; it keeps the plate while its menu is open.
        if (open || hovered)
            Draw::FillRoundedRect(fill.draw, plate, CornerRadii(Px(metrics.plateRadius)), colors.menuBarSelection);
        if (symbol) {
            Typography::DrawSymbol(fill.draw, symbol, symbol_font, item, colors.menuBarTitle);
        } else {
            const float top = item.Min.y + Px(metrics.baseline) - Typography::Baseline(font);
            Typography::Draw(fill.draw, font, ImRect(item.Min.x + Px(metrics.titlePadding), top, item.Max.x, top + Px(font.lineHeight)), colors.menuBarTitle, title);
        }

        // The items' keyboard shortcuts work while the bar is shown; the menu shows while it is open.
        const std::span<const MenuContent::Entry> entries = MenuContent::Collect(id, content);
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].kind == MenuContent::EntryKind::Item && entries[i].enabled && IsPressed(entries[i].shortcut))
                MenuContent::Choose(id, int(i));
        }
        if (!open) {
            if (openMenu == id)
                openMenu = 0;
            return;
        }
        const int picked = PopUpMenu::Show(id, entries);
        if (picked >= 0)
            MenuContent::Choose(id, picked);
    }

    static void DrawMenuBar(ImDrawList* draw, const ImRect& bar, const std::function<void()>& content, bool boldFirst) {
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        Draw::FillRect(draw, bar, Theme::Colors().menuBarBackground);

        BarFill fill{draw, bar, bar.Min.x + Px(metrics.leading), false, boldFirst, false};
        BarFill* outer = filling;
        const MenuContent::BarMenu outer_menu = MenuContent::CurrentBarMenu();
        filling = &fill;
        MenuContent::SetBarMenu(BarMenu);
        content();
        MenuContent::SetBarMenu(outer_menu);
        // A menu whose title is no longer drawn (its text changed, so its id did) has no title left to close it: drop it
        // here, or it would stay open behind the bar and block the titles from opening their own menus.
        if (openMenu && !fill.ownsOpenMenu) {
            PopUpMenu::Close(openMenu);
            openMenu = 0;
        }
        filling = outer;
    }

    void MenuBarAction(const char* label, const std::function<void()>& action) {
        if (!filling) return;
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        const Palette& colors = Theme::Colors();
        BarFill& fill = *filling;
        const Font font = Font::Style(TextStyle::Body);
        const std::string_view title = Interaction::VisibleLabel(label);
        const float width = Typography::Width(font, title);
        const ImRect item(fill.x, fill.bar.Min.y,
                          fill.x + Px(ImCeil(Pt(width)) +
                                      2.0f * metrics.titlePadding),
                          fill.bar.Max.y);
        fill.x = item.Max.x;
        const ImRect plate(item.Min.x - Px(metrics.plateOutset),
                           item.Min.y + Px(metrics.plateInset),
                           item.Max.x + Px(metrics.plateOutset),
                           item.Max.y - Px(metrics.plateInset));
        if (Interaction::g_probeCallback)
            Interaction::g_probeCallback(ImGui::GetID(label), item);
        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = item.Contains(io.MousePos);
        // While a menu is open its title keeps the highlight: a title that only runs an action shows no plate of its own
        // then, so the two plates cannot overlap over the gap between two adjacent titles.
        const bool highlighted = hovered && !(openMenu && PopUpMenu::IsOpen(openMenu));
        if (highlighted) {
            Draw::FillRoundedRect(fill.draw, plate,
                                  CornerRadii(Px(metrics.plateRadius)),
                                  colors.menuBarSelection);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            action();
        const float top =
            item.Min.y + Px(metrics.baseline) - Typography::Baseline(font);
        Typography::Draw(
            fill.draw, font,
            ImRect(item.Min.x + Px(metrics.titlePadding), top,
                   item.Max.x, top + Px(font.lineHeight)),
            colors.menuBarTitle, title);
    }

    bool MenuBarToggle(const char* label, bool* is_on) {
        if (!filling)
            return false;
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        const Palette& colors = Theme::Colors();
        BarFill& fill = *filling;
        const Font font = Font::Style(TextStyle::Body);
        const std::string_view title = Interaction::VisibleLabel(label);
        // A small checkbox before the title, on the title's line.
        const float box = Px(12.0f);
        const float gap = Px(5.0f);
        const float title_width = Px(ImCeil(Pt(Typography::Width(font, title))));
        const ImRect item(fill.x, fill.bar.Min.y,
                          fill.x + Px(metrics.titlePadding) + box + gap + title_width + Px(metrics.titlePadding),
                          fill.bar.Max.y);
        fill.x = item.Max.x;
        const ImRect plate(item.Min.x - Px(metrics.plateOutset), item.Min.y + Px(metrics.plateInset),
                           item.Max.x + Px(metrics.plateOutset), item.Max.y - Px(metrics.plateInset));
        // Like the other titles, it handles input directly, so report it to the debug probe too.
        if (Interaction::g_probeCallback)
            Interaction::g_probeCallback(ImGui::GetID(label), item);

        const ImGuiIO& io = ImGui::GetIO();
        const bool hovered = item.Contains(io.MousePos);
        const bool highlighted = hovered && !(openMenu && PopUpMenu::IsOpen(openMenu));
        bool changed = false;
        if (highlighted)
            Draw::FillRoundedRect(fill.draw, plate, CornerRadii(Px(metrics.plateRadius)), colors.menuBarSelection);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            *is_on = !*is_on;
            changed = true;
        }
        const float top = item.Min.y + Px(metrics.baseline) - Typography::Baseline(font);
        const float line_height = Px(font.lineHeight);
        const ImRect box_rect(item.Min.x + Px(metrics.titlePadding), ImFloor(top + (line_height - box) * 0.5f),
                              item.Min.x + Px(metrics.titlePadding) + box,
                              ImFloor(top + (line_height - box) * 0.5f) + box);
        Bezel::Check(fill.draw, box_rect, CornerRadii(Px(3.0f)),
                     *is_on ? Bezel::Mark::Check : Bezel::Mark::None, false);
        Typography::Draw(fill.draw, font,
                         ImRect(box_rect.Max.x + gap, top, item.Max.x, top + line_height),
                         colors.menuBarTitle, title);
        return changed;
    }

    void MenuBar(const MenuBarOptions& options, const std::function<void()>& content) {
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        const Palette& colors = Theme::Colors();
        ImGuiViewportP* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
        const ImRect bar(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + Px(metrics.height)));
        // The screen's work area starts under the bar, as notifications and other screen-placed panels expect.
        viewport->BuildWorkInsetMin.y += bar.GetHeight();
        // Above every window, under the menus it opens.
        Interaction::BeginOverlay("##CupertinoMenuBar", bar);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        DrawMenuBar(draw, bar, content, true);

        if (options.status) {
            const Font font = Font::Style(TextStyle::Body);
            const float top = bar.Min.y + Px(metrics.baseline) - Typography::Baseline(font);
            Typography::Draw(draw, font, ImRect(bar.Min.x, top, bar.Max.x - Px(metrics.statusTrailing), top + Px(font.lineHeight)), colors.menuBarTitle, options.status, TextAlignment::Trailing);
        }
        ImGui::End();
    }

    void MenuBarInWindow(const std::function<void()>& content) {
        MenuBarInWindow(MenuBarWindowOptions{}, content);
    }

    void MenuBarInWindow(const MenuBarWindowOptions& options, const std::function<void()>& content) {
        const Metrics::MenuBarMetrics& metrics = Metrics::MenuBar();
        const ImRect bar = Layout::Place(Layout::Placement{
            .size = ImVec2(Layout::FullProposal().x, Px(metrics.height)),
            .ignoresChildInsets = true,
        });
        if (Layout::IsMeasuring())
            return;
        DrawMenuBar(ImGui::GetWindowDrawList(), bar, content, options.boldFirstMenu);
    }
} // namespace Cupertino
