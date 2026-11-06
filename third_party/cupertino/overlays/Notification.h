#pragma once

#include "controls/IconPlate.h"
#include "core/Bitmap.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <functional>
#include <initializer_list>
#include <span>
#include <vector>

namespace Cupertino {
    // Where a presented banner is anchored in the work area. Inline banners and stacks use their own layout.
    enum class NotificationPlacement {
        TopRight,
        BottomRight,
    };

    // The severity a banner shows: its symbol and plate when the content gives no icon of its own.
    enum class NotificationKind {
        Info,
        Success,
        Warning,
        Error,
    };

    struct NotificationContent {
        const char* title = "";
        // A bold line under the title, such as the sender of a message.
        const char* subtitle = nullptr;
        const char* body = nullptr;
        // When it was delivered ("now", "1:38 PM"), at the trailing end of the title's line.
        const char* timestamp = nullptr;
        // The app's icon: a picture of its 40 pt canvas, or a plate and a symbol drawn as the icon's 32 pt body.
        Icon icon;
        // An attachment's thumbnail, 32 pt at the trailing side (the kit's Image variant).
        Bitmap attachment;
        // More notifications of its group under it, as Notification Center stacks them: a card for each, two at most.
        int stacked = 0;
        // Views in the action row's place, such as a ProgressView, buttons or any other content. When set they replace
        // the actions row.
        std::function<void()> content;
        // A close button at the banner's trailing edge. A presented banner dismisses itself; the value overload
        // returns NotificationCloseAction.
        bool showsClose = false;
        // Items of the menu a right-click on the banner opens. Choosing one returns NotificationContextAction + index.
        std::span<const char* const> contextMenu;
        // Severity; when icon is empty, the banner draws this kind's symbol and plate.
        NotificationKind kind = NotificationKind::Info;
        // Seconds until the banner closes itself; 0 keeps it until it is closed or answered. A pointer over it pauses
        // the timer and leaving restarts it. One second by default.
        float autoDismissAfter = 1.0f;
        // Placement for the presented bool* overload; inline banners and notification stacks ignore this field.
        NotificationPlacement placement = NotificationPlacement::TopRight;
    };

    struct NotificationAction {
        const char* title = "";
    };

    // Returned for a click on the banner itself, like UNNotificationDefaultActionIdentifier: the app shows what the
    // notification is about.
    inline constexpr int NotificationDefaultAction = -2;
    // Returned by the close button.
    inline constexpr int NotificationCloseAction = -3;
    // Context menu items return this plus the item's index.
    inline constexpr int NotificationContextAction = 1000;

    // A notification banner as macOS 15 draws it, placed as a view: the app icon, bold title, subtitle and body on the
    // notification material 344 pt wide, the actions as buttons under the text, a stacked group's cards beneath. Returns
    // the index of the action pressed this frame, NotificationDefaultAction for a click elsewhere on it, or -1.
    int NotificationBanner(const NotificationContent& content, std::span<const NotificationAction> actions = {});
    int NotificationBanner(const NotificationContent& content, std::initializer_list<NotificationAction> actions);

    // Presents the banner in the selected work-area trailing corner while *is_presented is true, as macOS delivers a
    // notification: it slides in from the trailing edge, leaves on its own after 5 s without actions (a banner) and
    // stays until it is answered with them (an alert). A click on it or on an action dismisses it. The default placement
    // is TopRight; set NotificationContent::placement to BottomRight for a bottom-right banner.
    int NotificationBanner(bool* is_presented, const NotificationContent& content, std::span<const NotificationAction> actions = {});
    int NotificationBanner(bool* is_presented, const NotificationContent& content, std::initializer_list<NotificationAction> actions);

    // A banner kept by a NotificationStack, with its actions owned by the stack.
    struct StackedNotification {
        NotificationContent content;
        std::vector<NotificationAction> actions;
        // Fade-in progress, 0 to 1, advanced by NotificationStack.
        float appearance = 0.0f;
        // A banner that is leaving: its fade-out progress, advanced by NotificationStack.
        bool closing = false;
        float closeProgress = 0.0f;
        // The slot the banner is animating toward (pixels), and the overlay window it lives in.
        float y = -1.0f;
        unsigned windowId = 0;
        // Whether the stack drew the banner in the last frame. A banner past the height limit is not visible; a
        // caller clearing the list can drop those at once rather than wait for a turn that never comes.
        bool visible = false;
        // When it first appeared, for the auto-dismiss timer, and whether the pointer was over it last frame.
        double shownAt = 0.0;
        bool wasHovered = false;
    };

    struct NotificationStackOptions {
        // The stack shows the newest notifications that fit in this height in points; 0 uses the viewport's bottom.
        float maximumHeight = 0.0f;
        float spacing = 8.0f;
        // Top-trailing corner of the stack in pixels. Zero uses the viewport's top-trailing corner.
        ImVec2 origin = ImVec2(0.0f, 0.0f);
    };

    // Draws notifications newest first in a trailing-aligned column, as a view. The newest that fit are shown; the
    // rest stay in items for a later frame. Closing an item (its close button or an action) removes it and returns its
    // index, with the action in *action (NotificationCloseAction for the close button). Returns -1 otherwise.
    int NotificationStack(const char* id, std::vector<StackedNotification>* items,
                          const NotificationStackOptions& options = {}, int* action = nullptr);

    // The height the stack's items need in points, before any height limit. A caller placing the stack in an overlay
    // uses it to size the overlay to the cards rather than to the whole window.
    float NotificationStackHeight(const std::vector<StackedNotification>& items,
                                  const NotificationStackOptions& options = {});
} // namespace Cupertino
