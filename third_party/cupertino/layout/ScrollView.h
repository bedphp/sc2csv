#pragma once

#include "Layout.h"
#include "core/Environment.h"

#include <functional>

namespace Cupertino {
    // NSScrollView's border (kit's Views/Boxes): none, a separator-colored line around the frame, or the same line rounded;
    // it fades by half when disabled.
    enum class ScrollViewBorder {
        None,
        Line,
        Rounded,
    };

    // Desktop scrolling: the overlay scroller can either fade away like macOS's automatic scroll bars, or stay visible
    // like a Windows scroll bar. The app uses Always so every scrollable pane has a usable indicator.
    enum class ScrollIndicator {
        Automatic,
        Always,
    };

    struct ScrollViewOptions {
        // The axis it scrolls along, as ScrollView(.horizontal): its content is stacked along it.
        ImGuiAxis axis = ImGuiAxis_Y;
        // Overlay scroller that appears while scrolling, as with "Show scroll bars: Automatically".
        bool showsIndicators = true;
        EdgeInsets padding;
        float spacing = 0.0f;
        HorizontalAlignment alignment = HorizontalAlignment::Leading;
        Layout::Role role = Layout::Role::None;
        ScrollViewBorder border = ScrollViewBorder::None;
        // How far it is scrolled in points, as scrollPosition: written every frame, and a new value scrolls there.
        float* offset = nullptr;
        // Keep the scroller visible while the content overflows.
        ScrollIndicator indicator = ScrollIndicator::Always;
        // Press the left button and move to pan the content. With dragAnywhere false only a press on empty space pans,
        // leaving sliders and text fields their drags; lists and tables set it since their rows have no drag gesture.
        bool dragToScroll = true;
        bool dragAnywhere = false;
    };

    // A scroll view that takes all the space it is offered, its content stacked along its axis. The overlay scroller
    // fades in while it scrolls; under the pointer it widens over a faint track, drags, and a click on the track pages.
    void ScrollView(const ScrollViewOptions& options, const std::function<void()>& content);
    void ScrollView(const std::function<void()>& content);

    // A bar over the first scroll view in content: that scroll view reports how far it is scrolled under the id and
    // marks its top edge in the given style while content is under the bar.
    void ScrollEdge(ImGuiID id, ScrollEdgeStyle style, const std::function<void()>& content);

    // How far the scroll view under a scroll edge is scrolled this frame, in points; 0 when none reported.
    float ScrollEdgeOffset(ImGuiID id);
} // namespace Cupertino
