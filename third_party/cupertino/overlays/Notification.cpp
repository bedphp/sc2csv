#include "Notification.h"

#include "core/Draw.h"
#include "core/Environment.h"
#include "core/Interaction.h"
#include "core/Metrics.h"
#include "core/Motion.h"
#include "core/State.h"
#include "core/Symbols.h"
#include "core/Theme.h"
#include "core/Typography.h"
#include "layout/Layout.h"
#include "layout/Stacks.h"
#include "overlays/PopUpMenu.h"

#include <algorithm>
#include <string>

namespace Cupertino {
    // Title and subtitle are semibold, narrower than the kit's bold (the Tahoe and Ars Technica banners @2x).
    static Font TitleFont() {
        return Font::Style(TextStyle::Body).Weight(FontWeight::Semibold);
    }

    // The banner laid out in points: the title cut to its line beside the timestamp, the body wrapped in its column, the
    // text at least two lines high, and the part above the actions, where the icon is centered.
    struct BannerLayout {
        std::string title;
        std::string subtitle;
        float bodyColumn = 0.0f;
        float bodyHeight = 0.0f;
        float textTop = 0.0f;
        float contentHeight = 0.0f;
        float height = 0.0f;
    };

    static BannerLayout LayOut(const NotificationContent& content, size_t actions, bool custom_content) {
        const Metrics::NotificationMetrics& metrics = Metrics::Notification();
        const Font title_font = TitleFont();
        const float column = metrics.width - metrics.textLeading - metrics.textTrailing;
        const float timestamp = content.timestamp ? Pt(Typography::Width(Font::Style(TextStyle::Subheadline), content.timestamp)) + metrics.timestampSpacing : 0.0f;
        BannerLayout layout;
        layout.bodyColumn = content.attachment.IsEmpty() ? column : metrics.width - metrics.attachmentTrailing - metrics.attachment - metrics.attachmentSpacing - metrics.textLeading;
        layout.title = Typography::Truncate(title_font, content.title, Px(column - timestamp));
        if (content.subtitle)
            layout.subtitle = Typography::Truncate(title_font, content.subtitle, Px(layout.bodyColumn));
        if (content.body && *content.body)
            layout.bodyHeight = Pt(Typography::Measure(Font::Style(TextStyle::Body), content.body, Px(layout.bodyColumn)).y);
        const float text = title_font.lineHeight * (content.subtitle ? 2.0f : 1.0f) + layout.bodyHeight;
        layout.textTop = metrics.top + (std::max(text, metrics.minTextHeight) - text) * 0.5f;
        layout.contentHeight = metrics.top + std::max(text, metrics.minTextHeight) + metrics.bottom;
        const bool extra_row = actions > 0 || custom_content;
        layout.height = layout.contentHeight + (extra_row ? metrics.actionHeight + metrics.actionsBottom : 0.0f);
        return layout;
    }

    // How many cards of its group stand under a banner, and the points they add below it.
    static int StackCards(const NotificationContent& content) {
        return ImClamp(content.stacked, 0, Metrics::Notification().stackCards);
    }

    static float StackHeight(const NotificationContent& content) {
        return Metrics::Notification().stackPeek * float(StackCards(content));
    }

    // Shifts the vertices added since first, sliding a banner in from the trailing edge.
    static void OffsetVertices(ImDrawList* draw, int first, float dx, float dy) {
        if (dx == 0.0f && dy == 0.0f)
            return;
        for (int i = first; i < draw->VtxBuffer.Size; ++i) {
            draw->VtxBuffer[i].pos.x += dx;
            draw->VtxBuffer[i].pos.y += dy;
        }
    }

    // Multiplies the alpha of the vertices added since first, fading a banner that is sliding in.
    static void FadeVertices(ImDrawList* draw, int first, float opacity) {
        if (opacity >= 0.999f)
            return;
        opacity = ImSaturate(opacity);
        for (int i = first; i < draw->VtxBuffer.Size; ++i) {
            ImDrawVert& vertex = draw->VtxBuffer[i];
            const int alpha = int(float((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) * opacity);
            vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT);
        }
    }

    // An action: a flat button in the material's vibrant fill with the label's text.
    static bool DrawAction(ImGuiID id, const ImRect& frame, const char* title) {
        const Palette& colors = Theme::Colors();
        const Interaction::Response response = Interaction::Button(id, frame);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const CornerRadii radii(Px(Metrics::Notification().actionRadius));
        Draw::FillRoundedRect(draw, frame, radii, response.held && response.hovered ? colors.notificationButtonPressed : colors.notificationButton);
        Typography::Draw(draw, Font::Style(TextStyle::Callout), frame, colors.label, title, TextAlignment::Center);
        if (response.focused)
            Draw::FocusRing(draw, frame, radii, colors.accent);
        return response.pressed;
    }

    // Draws the banner in frame (pixels) and runs its clicks; a presented banner casts its shadow past its window.
    static int DrawBanner(ImGuiID id, const ImRect& frame, const NotificationContent& content, const BannerLayout& layout, std::span<const NotificationAction> actions, bool unclipped_shadow) {
        const Metrics::NotificationMetrics& metrics = Metrics::Notification();
        const Palette& colors = Theme::Colors();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const CornerRadii radii(Px(metrics.radius));
        // Every banner in a stack shares the window, so its close/menu/action
        // ids have to be scoped to the banner's own id.
        ImGui::PushID(int(id));
        if (unclipped_shadow)
            draw->PushClipRectFullScreen();
        // The group's other notifications peek out under the banner, the farthest first; the banner's shadow falls on them.
        for (int card = StackCards(content); card >= 1; --card) {
            const float inset = Px(metrics.stackInset + metrics.stackInsetStep * float(card - 1));
            const float drop = Px(metrics.stackPeek * float(card));
            const ImRect rect(frame.Min.x + inset, frame.Min.y + drop, frame.Max.x - inset, frame.Max.y + drop);
            const CornerRadii card_radii(Px(metrics.stackRadius));
            Draw::DropShadows(draw, rect, card_radii, Theme::NotificationShadows());
            Draw::FillRoundedRect(draw, rect, card_radii, card == 1 ? colors.notificationStackNear : colors.notificationStackFar);
            Draw::StrokeRoundedRect(draw, rect, card_radii, colors.notificationRim, Px(1.0f), StrokeAlignment::Inside);
        }
        Draw::DropShadows(draw, frame, radii, Theme::NotificationShadows());
        if (unclipped_shadow)
            draw->PopClipRect();
        Draw::FillRoundedRect(draw, frame, radii, colors.notificationBackground);
        Draw::StrokeRoundedRect(draw, frame, radii, colors.notificationRim, Px(1.0f), StrokeAlignment::Inside);
        // A faint light along the top inside the rim, so the card reads as a material rather than a flat fill.
        draw->PushClipRect(frame.Min, frame.Max, true);
        Draw::FillRect(draw, ImRect(frame.Min.x + Px(metrics.radius), frame.Min.y + Px(1.0f),
                                    frame.Max.x - Px(metrics.radius), frame.Min.y + Px(2.0f)),
                       Rgba::White(0.07f));
        draw->PopClipRect();

        // The banner answers a click anywhere outside its actions, which overlap it.
        const Interaction::Response response = Interaction::Button(id, frame, ImGuiButtonFlags_AllowOverlap);
        int chosen = response.pressed ? NotificationDefaultAction : -1;

        const ImVec2 icon_min(frame.Min.x + Px(metrics.iconLeading), frame.Min.y + Px((layout.contentHeight - metrics.icon) * 0.5f));
        ImRect icon_rect(icon_min, icon_min + Px(ImVec2(metrics.icon, metrics.icon)));
        // The content's own icon, or the severity's.
        Icon icon = content.icon;
        if (icon.IsEmpty()) {
            switch (content.kind) {
                case NotificationKind::Success: icon = {Symbols::CheckmarkCircleFill, IconPlate::Green}; break;
                case NotificationKind::Warning: icon = {Symbols::ExclamationmarkTriangleFill, IconPlate::Yellow}; break;
                case NotificationKind::Error: icon = {Symbols::XmarkCircleFill, IconPlate::Red}; break;
                default: icon = {Symbols::InfoCircle, IconPlate::Blue}; break;
            }
        }
        if (icon.image.IsEmpty())
            icon_rect.Expand(-Px(metrics.iconMargin));
        DrawIcon(draw, icon_rect, icon);

        // The close button stands at the top-trailing corner, as macOS shows it on a banner.
        ImRect close_rect;
        if (content.showsClose) {
            const float side = Px(20.0f);
            close_rect = ImRect(frame.Max.x - Px(metrics.textTrailing) - side,
                                frame.Min.y + Px(6.0f),
                                frame.Max.x - Px(metrics.textTrailing),
                                frame.Min.y + Px(6.0f) + side);
            const Interaction::Response close = Interaction::Button(ImGui::GetID("##close"), close_rect);
            if (close.held && close.hovered)
                Draw::FillRoundedRect(draw, close_rect, CornerRadii(side * 0.5f), colors.quaternaryFill);
            Typography::DrawSymbol(draw, Symbols::Xmark, Font::System(10.0f, FontWeight::Semibold),
                                   close_rect, colors.secondaryLabel, TextAlignment::Center);
            if (close.pressed)
                chosen = NotificationCloseAction;
        }

        const Font title_font = TitleFont();
        const float left = frame.Min.x + Px(metrics.textLeading);
        float y = frame.Min.y + Px(layout.textTop);
        const float line = Px(title_font.lineHeight);
        Typography::DrawWrapped(draw, title_font, ImRect(left, y, frame.Max.x - Px(metrics.textTrailing), y + line), colors.notificationText, layout.title);
        if (content.timestamp) {
            // On the title's baseline, at the trailing edge of the column (clear of the close button).
            const Font font = Font::Style(TextStyle::Subheadline);
            const float top = y + Typography::Baseline(title_font) - Typography::Baseline(font);
            const float right = content.showsClose ? close_rect.Min.x - Px(6.0f)
                                                   : frame.Max.x - Px(metrics.textTrailing);
            Typography::Draw(draw, font, ImRect(left, top, right, top + Px(font.lineHeight)), colors.notificationTimestamp, content.timestamp, TextAlignment::Trailing);
        }
        y += line;
        if (content.subtitle) {
            Typography::DrawWrapped(draw, title_font, ImRect(left, y, left + Px(layout.bodyColumn), y + line), colors.notificationText, layout.subtitle);
            y += line;
        }
        if (layout.bodyHeight > 0.0f)
            Typography::DrawWrapped(draw, Font::Style(TextStyle::Body), ImRect(left, y, left + Px(layout.bodyColumn), y + Px(layout.bodyHeight)), colors.notificationText, content.body);

        const float content_bottom = frame.Min.y + Px(layout.contentHeight);
        if (!content.attachment.IsEmpty()) {
            const ImVec2 max(frame.Max.x - Px(metrics.attachmentTrailing), content_bottom - Px(metrics.attachmentBottom));
            Draw::Image(draw, ImRect(max - Px(ImVec2(metrics.attachment, metrics.attachment)), max), content.attachment, Px(metrics.attachmentRadius));
        }

        // Custom content (a progress view, buttons, any views) takes the
        // actions row's place.
        if (content.content) {
            const ImRect area(frame.Min.x + Px(metrics.textLeading), content_bottom,
                              frame.Max.x - Px(metrics.textTrailing),
                              content_bottom + Px(metrics.actionHeight));
            Layout::ContainerSpec spec;
            spec.arrangement = Layout::Arrangement::Vertical;
            spec.fillWidth = true;
            Layout::Region(ImHashStr("##content", 0, id), area, spec, content.content);
        } else if (!actions.empty()) {
            // Actions share the text column's width, centered under it.
            const float count = float(actions.size());
            const float top = content_bottom;
            const float row_left = frame.Min.x + Px(metrics.textLeading);
            const float gap = Px(metrics.actionGap);
            const float width = (frame.GetWidth() - 2.0f * Px(metrics.textLeading) - gap * (count - 1.0f)) / count;
            ImGui::PushID(int(id));
            for (size_t i = 0; i < actions.size(); ++i) {
                const float action_left = row_left + float(i) * (width + gap);
                if (DrawAction(ImGui::GetID(int(i)), ImRect(action_left, top, action_left + width, top + Px(metrics.actionHeight)), actions[i].title))
                    chosen = int(i);
            }
            ImGui::PopID();
        }

        // A right click opens the banner's menu; its items return
        // NotificationContextAction + index.
        if (!content.contextMenu.empty()) {
            const ImGuiID menu_id = ImHashStr("##context", 0, id);
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                frame.Contains(ImGui::GetIO().MousePos))
                PopUpMenu::Open(menu_id, ImRect(ImGui::GetIO().MousePos, ImGui::GetIO().MousePos), -1, PopUpMenu::Placement::AtPoint);
            if (PopUpMenu::IsOpen(menu_id)) {
                const int item = PopUpMenu::Show(menu_id, content.contextMenu);
                if (item >= 0)
                    chosen = NotificationContextAction + item;
            }
        }
        ImGui::PopID();
        return chosen;
    }

    int NotificationBanner(const NotificationContent& content, std::span<const NotificationAction> actions) {
        const ImGuiID id = Layout::NextViewId();
        const BannerLayout layout = LayOut(content, actions.size(), static_cast<bool>(content.content));
        const ImRect placed = Layout::Place(Px(ImVec2(Metrics::Notification().width, layout.height + StackHeight(content))));
        if (Layout::IsMeasuring())
            return -1;
        return DrawBanner(id, ImRect(placed.Min, placed.Min + Px(ImVec2(Metrics::Notification().width, layout.height))), content, layout, actions, false);
    }

    int NotificationBanner(const NotificationContent& content, std::initializer_list<NotificationAction> actions) {
        return NotificationBanner(content, std::span<const NotificationAction>(actions.begin(), actions.size()));
    }

    struct PresentedNotification {
        AnimatedFloat presentation;
        double shownAt = 0.0;
        bool shown = false;
    };

    int NotificationBanner(bool* is_presented, const NotificationContent& content, std::span<const NotificationAction> actions) {
        const Metrics::NotificationMetrics& metrics = Metrics::Notification();
        const ImGuiID id = ImGui::GetID(is_presented);
        PresentedNotification& state = State::Get<PresentedNotification>(id);
        const double now = ImGui::GetTime();
        if (*is_presented && !state.shown)
            state.shownAt = now;
        state.shown = *is_presented;
        const float presentation = state.presentation.Update(*is_presented ? 1.0f : 0.0f, Animation::EaseOut(metrics.slide));
        if (presentation <= 0.0f)
            return -1;

        // Presented banners sit in the work area's trailing corner, 16 pt from its edge, and slide in from the right.
        const BannerLayout layout = LayOut(content, actions.size(), static_cast<bool>(content.content));
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size = Px(ImVec2(metrics.width, layout.height));
        const float resting = viewport->WorkPos.x + viewport->WorkSize.x - Px(metrics.screenInset) - size.x;
        const float extent = size.y + Px(StackHeight(content));
        const float resting_y = content.placement == NotificationPlacement::BottomRight
                                    ? viewport->WorkPos.y + viewport->WorkSize.y - Px(metrics.screenInset) - extent
                                    : viewport->WorkPos.y + Px(metrics.screenInset);
        const ImVec2 origin = Draw::Snap(ImVec2(resting + (1.0f - presentation) * (size.x + Px(metrics.screenInset)), resting_y));
        const ImRect frame(origin, origin + size);
        char name[40];
        ImFormatString(name, IM_ARRAYSIZE(name), "##CupertinoNotification%08X", id);
        Interaction::BeginOverlay(name, ImRect(frame.Min, frame.Max + ImVec2(0.0f, Px(StackHeight(content)))), ImGuiWindowFlags_NoFocusOnAppearing);
        ImDrawList* banner_draw = ImGui::GetWindowDrawList();
        const int first_vertex = banner_draw->VtxBuffer.Size;
        int chosen = DrawBanner(ImGui::GetID("banner"), frame, content, layout, actions, true);
        FadeVertices(banner_draw, first_vertex, presentation);
        // The pointer keeps a banner on the screen; one without actions leaves once it has shown for its time.
        if (ImGui::IsWindowHovered())
            state.shownAt = now;
        ImGui::End();
        if (!*is_presented)
            return -1;
        if (content.autoDismissAfter > 0.0f && now - state.shownAt >= double(content.autoDismissAfter))
            *is_presented = false;
        if (content.autoDismissAfter <= 0.0f && actions.empty() && !content.content &&
            now - state.shownAt >= double(metrics.duration))
            *is_presented = false;
        if (chosen == NotificationCloseAction) {
            *is_presented = false;
            return -1;
        }
        if (chosen == NotificationDefaultAction ||
            (chosen >= 0 && chosen < NotificationContextAction))
            *is_presented = false;
        return chosen;
    }

    int NotificationBanner(bool* is_presented, const NotificationContent& content, std::initializer_list<NotificationAction> actions) {
        return NotificationBanner(is_presented, content, std::span<const NotificationAction>(actions.begin(), actions.size()));
    }

    float NotificationStackHeight(const std::vector<StackedNotification>& items,
                                  const NotificationStackOptions& options) {
        float height = 0.0f;
        int visible = 0;
        for (const StackedNotification& item : items) {
            const BannerLayout layout = LayOut(item.content, item.actions.size(), static_cast<bool>(item.content.content));
            const float banner = layout.height + StackHeight(item.content);
            height += (visible > 0 ? options.spacing : 0.0f) + banner;
            ++visible;
        }
        return height;
    }

    int NotificationStack(const char* id, std::vector<StackedNotification>* items,
                          const NotificationStackOptions& options, int* action) {
        (void)id;
        if (!items || items->empty())
            return -1;

        const Metrics::NotificationMetrics& metrics = Metrics::Notification();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float width = Px(metrics.width);
        const float shadow = Px(12.0f);
        const float spacing = Px(options.spacing);
        const float right = options.origin.x > 0.0f
                                ? options.origin.x
                                : viewport->WorkPos.x + viewport->WorkSize.x - Px(metrics.screenInset);
        const float top = options.origin.y > 0.0f
                              ? options.origin.y
                              : viewport->WorkPos.y + Px(metrics.screenInset);
        const float limit = options.maximumHeight > 0.0f
                                ? Px(options.maximumHeight)
                                : viewport->WorkPos.y + viewport->WorkSize.y - Px(metrics.screenInset) - top;

        // Which banners fit, and the slot each stands in.
        std::vector<float> targets;
        float cursor = top;
        for (const StackedNotification& item : *items) {
            const BannerLayout layout = LayOut(item.content, item.actions.size(),
                                               static_cast<bool>(item.content.content));
            const float height = Px(layout.height + StackHeight(item.content));
            if (!targets.empty() && cursor + height - top > limit)
                break;
            targets.push_back(cursor);
            cursor += height + spacing;
        }
        if (targets.empty())
            return -1;

        // Record which banners the caller can see and act on this frame.
        for (size_t i = 0; i < items->size(); ++i)
            (*items)[i].visible = i < targets.size();

        static unsigned next_window = 1;
        const float dt = ImGui::GetIO().DeltaTime;
        const double now = ImGui::GetTime();
        // A banner's timer starts when it is delivered, even while it has no slot. Banners past the height limit then
        // expire together with the ones in view instead of surfacing later and outliving notifications sent with them.
        for (size_t i = 0; i < items->size(); ++i) {
            StackedNotification& item = (*items)[i];
            if (item.shownAt <= 0.0)
                item.shownAt = now;
            if (i >= targets.size() && !item.closing && item.content.autoDismissAfter > 0.0f &&
                now - item.shownAt >= double(item.content.autoDismissAfter)) {
                item.closing = true;
                item.closeProgress = 1.0f;
            }
        }
        int chosen = -1;
        int chosen_action = -1;
        // Each banner is its own overlay window: closing one never clips or
        // resizes the others, and they float above the page independently.
        // Drawn bottom to top so a menu opened on a banner stands over the
        // banners below it.
        for (int i = int(targets.size()) - 1; i >= 0; --i) {
            StackedNotification& item = (*items)[size_t(i)];
            if (item.windowId == 0)
                item.windowId = next_window++;
            if (item.closing)
                item.closeProgress = ImMin(1.0f, item.closeProgress + dt * 5.0f);
            else
                item.appearance = ImMin(1.0f, item.appearance + dt * 7.0f);
            if (item.y < 0.0f)
                item.y = targets[size_t(i)];
            // Animate the slot, so inserting or removing a banner slides the others.
            item.y += (targets[size_t(i)] - item.y) * ImSaturate(dt * 14.0f);
            const float opacity = item.closing ? 1.0f - item.closeProgress : item.appearance;
            const BannerLayout layout = LayOut(item.content, item.actions.size(),
                                               static_cast<bool>(item.content.content));
            const float height = Px(layout.height + StackHeight(item.content));
            const ImRect card(right - width, item.y, right, item.y + height);
            const ImRect area(ImVec2(card.Min.x - shadow, card.Min.y - shadow),
                              ImVec2(card.Max.x + shadow, card.Max.y + shadow));
            char name[64];
            ImFormatString(name, IM_ARRAYSIZE(name), "##CupertinoNotificationStack%u", item.windowId);
            Interaction::BeginOverlay(name, area, ImGuiWindowFlags_NoFocusOnAppearing);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const int first_vertex = draw->VtxBuffer.Size;
            int result = -1;
            Padding(EdgeInsets::All(12.0f), [&] {
                result = NotificationBanner(
                    item.content,
                    std::span<const NotificationAction>(item.actions.data(), item.actions.size()));
            });
            // Ease-out-back horizontal travel: the card slides in from the
            // window edge with a small overshoot, and back out while it fades.
            const float u = opacity - 1.0f;
            const float eased = 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;
            OffsetVertices(draw, first_vertex, (1.0f - eased) * Px(36.0f), 0.0f);
            FadeVertices(draw, first_vertex, opacity);
            const bool hovered = ImGui::IsWindowHovered();
            ImGui::End();
            if (item.shownAt <= 0.0)
                item.shownAt = now;
            // The pointer pauses the timer; leaving restarts it.
            if (hovered) {
                item.shownAt = now;
                item.wasHovered = true;
            } else if (item.wasHovered) {
                item.shownAt = now;
                item.wasHovered = false;
            }
            // A click on the banner itself does nothing. Its close button and
            // actions close it; a context menu item is reported to the caller,
            // which decides (Copy keeps it, Close clears it).
            const bool close_action = result == NotificationCloseAction ||
                                      (result >= 0 && result < NotificationContextAction);
            const bool context_action = result >= NotificationContextAction;
            if (close_action && !item.closing && chosen < 0) {
                chosen = i;
                chosen_action = result;
                item.closing = true;
                item.closeProgress = 0.0f;
            } else if (context_action && chosen < 0) {
                chosen = i;
                chosen_action = result;
            }
            if (!item.closing && item.content.autoDismissAfter > 0.0f &&
                now - item.shownAt >= double(item.content.autoDismissAfter)) {
                item.closing = true;
                item.closeProgress = 0.0f;
            }
        }
        // Banners past the height limit are never drawn, so an animation on one would never advance. Retire a closing
        // one at once; otherwise a caller closing the list from the bottom can stall on the hidden tail.
        for (size_t i = targets.size(); i < items->size(); ++i) {
            StackedNotification& item = (*items)[i];
            if (item.closing)
                item.closeProgress = 1.0f;
        }
        // Retire finished closings, from the back so the reported index stays valid.
        for (int i = int(items->size()) - 1; i >= 0; --i) {
            StackedNotification& item = (*items)[size_t(i)];
            if (!item.closing || item.closeProgress < 1.0f)
                continue;
            if (i < chosen)
                --chosen;
            else if (i == chosen)
                chosen = -1;
            items->erase(items->begin() + i);
        }
        if (chosen < 0)
            return -1;
        if (action)
            *action = chosen_action;
        return chosen;
    }
} // namespace Cupertino
