#include "ScrollView.h"

#include "core/Draw.h"
#include "core/Environment.h"
#include "core/Interaction.h"
#include "core/Metrics.h"
#include "core/Motion.h"
#include "core/State.h"
#include "core/Theme.h"
#include "window/NativeHost.h"

#include <cmath>

namespace Cupertino {
    struct ScrollerState {
        float lastScroll = 0.0f;
        float idle = 10.0f;
        AnimatedFloat opacity;
        AnimatedFloat hover;
        // Where the pointer holds the thumb, in pixels from its start; negative when it does not.
        float grab = -1.0f;
        // The offset last written to the binding, to tell a new value from it, and a new one waiting for the content's
        // size to be known (a few frames at most).
        float written = -1.0f;
        float pending = -1.0f;
        int pendingFrames = 0;
        // Press-and-drag scrolling: the press point and the scroll there, in pixels, kept while the button is down.
        bool panArmed = false;
        bool panning = false;
        bool panTouch = false;
        ImVec2 panMouse;
        ImVec2 panScroll;
        // Touch panning keeps its speed when the finger lifts and glides to a stop, as a scroll view does.
        float panVelocity = 0.0f;
        float panLastPointer = 0.0f;
        double panLastTime = 0.0;
        bool gliding = false;
        float glideVelocity = 0.0f;
    };

    struct ScrollEdgeState {
        float offset = 0.0f;
        int frame = -1;
    };

    // The top edge of a view scrolled under a bar: a line, or a hairline with the soft shadow the bar casts.
    static void DrawScrollEdge(ImGuiWindow* window, ScrollEdgeStyle style) {
        const Palette& colors = Theme::Colors();
        const ImRect visible = window->InnerRect;
        if (style == ScrollEdgeStyle::Line) {
            Draw::HorizontalLine(window->DrawList, visible.Min.x, visible.Max.x, visible.Min.y, Px(1.0f), colors.sidebarScrollEdge);
            return;
        }
        // The bar reaches past the sides so its shadow keeps full strength up to them.
        const float reach = Px(10.0f);
        const ImRect bar(visible.Min.x - reach, visible.Min.y - reach, visible.Max.x + reach, visible.Min.y);
        Draw::DropShadows(window->DrawList, bar, CornerRadii(0.0f), Theme::ScrollEdgeShadows());
        Draw::HorizontalLine(window->DrawList, visible.Min.x, visible.Max.x, visible.Min.y, Px(0.5f), colors.scrollEdgeLine);
    }

    // The overlay scroller's geometry along axis in the visible rect, at a thickness: the track and the thumb.
    struct ScrollerFrame {
        ImRect track;
        ImRect thumb;
    };

    static ScrollerFrame ScrollerAt(ImGuiWindow* window, ImGuiAxis axis, float thickness) {
        const Metrics::ScrollViewMetrics& metrics = Metrics::ScrollView();
        const bool vertical = axis == ImGuiAxis_Y;
        const ImRect visible = window->InnerRect;
        const float inset = Px(metrics.scrollerInset);
        const ImRect track = vertical ? ImRect(visible.Max.x - inset - thickness, visible.Min.y + inset, visible.Max.x - inset, visible.Max.y - inset) : ImRect(visible.Min.x + inset, visible.Max.y - inset - thickness, visible.Max.x - inset, visible.Max.y - inset);
        const float length = vertical ? track.GetHeight() : track.GetWidth();
        const float extent = vertical ? visible.GetHeight() : visible.GetWidth();
        const float scroll_max = window->ScrollMax[axis];
        const float thumb_length = ImMax(Px(metrics.scrollerMinLength), length * extent / (extent + scroll_max));
        const float at = (length - thumb_length) * (scroll_max > 0.0f ? window->Scroll[axis] / scroll_max : 0.0f);
        const ImRect thumb = vertical ? ImRect(track.Min.x, track.Min.y + at, track.Max.x, track.Min.y + at + thumb_length) : ImRect(track.Min.x + at, track.Min.y, track.Min.x + at + thumb_length, track.Max.y);
        return {track, thumb};
    }

    // The scroller takes the pointer before the content: the pointer over its edge brings it out, as an overlay scroller
    // shows under the pointer, and over it the scroller widens and drags; a click on the track pages toward the pointer.
    static void ScrollerInput(ImGuiWindow* window, ImGuiAxis axis, ScrollerState& state) {
        if (window->ScrollMax[axis] <= 0.0f)
            return;
        const Metrics::ScrollViewMetrics& metrics = Metrics::ScrollView();
        const ScrollerFrame wide = ScrollerAt(window, axis, Px(metrics.scrollerHover));
        const Interaction::Response response = Interaction::Button(window->GetID("##Scroller"), wide.track, ImGuiButtonFlags_PressedOnClick, ImGuiItemFlags_NoNav);
        state.hover.Update(response.hovered || response.held ? 1.0f : 0.0f, Animation::Linear(metrics.scrollerGrow));
        if (response.hovered || response.held)
            state.idle = 0.0f;
        const float pointer = ImGui::GetIO().MousePos[axis];
        const float thumb_start = wide.thumb.Min[axis];
        const float thumb_length = wide.thumb.Max[axis] - thumb_start;
        if (response.pressed) {
            const bool on_thumb = pointer >= thumb_start && pointer <= wide.thumb.Max[axis];
            state.grab = on_thumb ? pointer - thumb_start : -1.0f;
            if (!on_thumb) {
                const float page = window->InnerRect.GetSize()[axis] * (pointer < thumb_start ? -1.0f : 1.0f);
                const float target = ImClamp(window->Scroll[axis] + page, 0.0f, window->ScrollMax[axis]);
                axis == ImGuiAxis_Y ? ImGui::SetScrollY(window, target) : ImGui::SetScrollX(window, target);
            }
        }
        if (!response.held)
            state.grab = -1.0f;
        if (state.grab >= 0.0f) {
            const float room = wide.track.GetSize()[axis] - thumb_length;
            const float fraction = room > 0.0f ? ImSaturate((pointer - state.grab - wide.track.Min[axis]) / room) : 0.0f;
            const float target = fraction * window->ScrollMax[axis];
            axis == ImGuiAxis_Y ? ImGui::SetScrollY(window, target) : ImGui::SetScrollX(window, target);
        }
    }

    // Held-button scrolling: a press on the view's background pans it once the pointer has moved past a small threshold.
    // Lists and tables allow the drag to start on a row; there it also takes the press away from the row so releasing
    // after a drag does not select it. Sliders, steppers and fields keep their own drags because their views leave
    // dragAnywhere false and a control's active id cancels the pan.
    static void PanInput(ImGuiWindow* window, ImGuiAxis axis, ScrollerState& state, const ScrollViewOptions& options) {
        if (!options.dragToScroll)
            return;
        ImGuiIO& io = ImGui::GetIO();
        const float scroll_max = window->ScrollMax[axis];
        // A touch drag that has ended keeps its speed and glides to a stop.
        if (state.gliding) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || io.MouseWheel != 0.0f || scroll_max <= 0.0f) {
                state.gliding = false;
            } else {
                const float dt = ImMin(Motion::DeltaTime(), 0.05f);
                const float target = window->Scroll[axis] + state.glideVelocity * dt;
                const float clamped = ImClamp(target, 0.0f, scroll_max);
                axis == ImGuiAxis_Y ? ImGui::SetScrollY(window, clamped) : ImGui::SetScrollX(window, clamped);
                state.glideVelocity = clamped == target ? state.glideVelocity * std::exp(-4.5f * dt) : 0.0f;
                if (ImAbs(state.glideVelocity) < Px(10.0f))
                    state.gliding = false;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && window->InnerRect.Contains(io.MousePos)) {
            bool on_scroller = false;
            if (scroll_max > 0.0f) {
                const ScrollerFrame wide = ScrollerAt(window, axis, Px(Metrics::ScrollView().scrollerHover));
                on_scroller = wide.track.Contains(io.MousePos);
            }
            if (!on_scroller) {
                state.panArmed = true;
                state.panning = false;
                // A finger is less precise than a pointer and has no release speed to lose: give it a wider dead zone
                // and a flick that carries on.
                state.panTouch = io.MouseSource != ImGuiMouseSource_Mouse;
                state.panMouse = io.MousePos;
                state.panScroll = ImVec2(window->Scroll.x, window->Scroll.y);
                state.panVelocity = 0.0f;
                state.panLastPointer = io.MousePos[axis];
                state.panLastTime = ImGui::GetTime();
                state.gliding = false;
            }
        }
        if (!state.panArmed)
            return;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (state.panning && state.panTouch && ImAbs(state.panVelocity) > Px(80.0f)) {
                state.gliding = true;
                state.glideVelocity = ImClamp(state.panVelocity, -Px(3200.0f), Px(3200.0f));
            }
            state.panArmed = false;
            state.panning = false;
            return;
        }
        const ImVec2 delta = io.MousePos - state.panMouse;
        if (!state.panning) {
            const float threshold = Px(state.panTouch ? 10.0f : 4.0f);
            if (ImAbs(delta.x) < threshold && ImAbs(delta.y) < threshold)
                return;
            // A press on a control that owns its drag (a slider, a field, a text selection) wins unless this view
            // explicitly allows dragging from its rows.
            if (!options.dragAnywhere && GImGui->ActiveId != 0) {
                state.panArmed = false;
                return;
            }
            state.panning = true;
            state.panLastPointer = io.MousePos[axis];
            state.panLastTime = ImGui::GetTime();
            state.panVelocity = 0.0f;
            if (GImGui->ActiveId != 0)
                ImGui::ClearActiveID();
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        // Keep the finger's speed, smoothed a little so one slow frame does not stop the glide.
        const double now = ImGui::GetTime();
        const float dt = float(now - state.panLastTime);
        const float pointer = io.MousePos[axis];
        if (dt > 0.0005f) {
            const float speed = -(pointer - state.panLastPointer) / dt;
            state.panVelocity = ImLerp(state.panVelocity, speed, ImClamp(dt * 20.0f, 0.0f, 1.0f));
            state.panLastPointer = pointer;
            state.panLastTime = now;
        }
        if (axis == ImGuiAxis_Y && window->ScrollMax.y > 0.0f)
            ImGui::SetScrollY(window, state.panScroll.y - delta.y);
        if (axis == ImGuiAxis_X && window->ScrollMax.x > 0.0f)
            ImGui::SetScrollX(window, state.panScroll.x - delta.x);
    }

    // How far a notch of the wheel scrolls a view that shows length pixels along the axis: ImGui's vertical step.
    static float WheelStep(ImGuiWindow* window, float length) {
        return ImTrunc(ImMin(5.0f * window->FontRefSize, length * 0.67f));
    }

    // The wheel goes on to the nearest view around that scrolls vertically.
    static void WheelAround(ImGuiWindow* window, float wheel) {
        for (ImGuiWindow* around = window->ParentWindow; around; around = around->ParentWindow) {
            if (around->ScrollMax.y > 0.0f && !(around->Flags & ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::SetScrollY(around, around->Scroll.y - wheel * WheelStep(around, around->InnerRect.GetHeight()));
                return;
            }
            if (!(around->Flags & ImGuiWindowFlags_ChildWindow))
                return;
        }
    }

    // A mouse turns only its vertical wheel, which ImGui gives a view that scrolls sideways only with Shift held (as macOS
    // does): over such a view the wheel scrolls it sideways, and once the view reaches an end it goes on to the view
    // around it. The view claims the wheel a frame ahead while the pointer rests on it and no other view is being wheeled;
    // Shift and a tilting wheel or a trackpad keep scrolling it as ImGui does.
    static void SidewaysWheel(ImGuiWindow* window) {
        ImGuiContext& g = *GImGui;
        const ImGuiID owner = window->GetID("##SidewaysWheel");
        const float wheel = g.IO.MouseWheel;
        if (wheel != 0.0f && ImGui::GetKeyOwner(ImGuiKey_MouseWheelY) == owner) {
            const float target = ImClamp(window->Scroll.x - wheel * WheelStep(window, window->InnerRect.GetWidth()), 0.0f, window->ScrollMax.x);
            if (target != window->Scroll.x)
                ImGui::SetScrollX(window, target);
            else
                WheelAround(window, wheel);
        }
        const bool sideways = window->ScrollMax.x > 0.0f && window->ScrollMax.y <= 0.0f;
        if (sideways && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && (!g.WheelingWindow || g.WheelingWindow == window))
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, owner);
    }

    // The thumb over the content, and the track under the pointer. An automatic scroller fades out after resting; an
    // always-visible one keeps a faint track and thumb so the pane reads as scrollable even before the pointer reaches it.
    static void DrawScroller(ImGuiWindow* window, ImGuiAxis axis, ScrollerState& state, ScrollIndicator indicator) {
        const Metrics::ScrollViewMetrics& metrics = Metrics::ScrollView();
        if (window->ScrollMax[axis] <= 0.0f)
            return;
        state.idle = window->Scroll[axis] != state.lastScroll ? 0.0f : state.idle + Motion::DeltaTime();
        state.lastScroll = window->Scroll[axis];
        const float hover = state.hover.value;
        const bool active = state.idle < metrics.scrollerIdle || hover > 0.0f || state.grab >= 0.0f;
        const bool always = indicator == ScrollIndicator::Always;
        const float target = active ? 1.0f : always ? 0.68f : 0.0f;
        const float opacity = state.opacity.Update(target, Animation::EaseOut(target > 0.0f ? 0.1f : metrics.scrollerFade));
        if (opacity <= 0.0f)
            return;
        const Palette& colors = Theme::Colors();
        const ScrollerFrame frame = ScrollerAt(window, axis, Px(ImLerp(metrics.scroller, metrics.scrollerHover, hover)));
        const float track_opacity = always ? opacity * ImMax(0.18f, hover) : opacity * hover;
        if (track_opacity > 0.0f)
            Draw::FillRoundedRect(window->DrawList, frame.track, CornerRadii(Px(metrics.scrollerHover) * 0.5f), colors.scrollerTrack.Opacity(track_opacity), CornerStyle::Circular);
        const float width = axis == ImGuiAxis_Y ? frame.thumb.GetWidth() : frame.thumb.GetHeight();
        Draw::FillRoundedRect(window->DrawList, frame.thumb, CornerRadii(width * 0.5f), colors.scrollerThumb.Opacity(opacity), CornerStyle::Circular);
    }

    // The border sits just outside the frame, so the content keeps all of it; a square one keeps square corners.
    static void DrawBorder(ImDrawList* draw, const ImRect& rect, ScrollViewBorder border) {
        const Metrics::ScrollViewMetrics& metrics = Metrics::ScrollView();
        const Rgba color = Theme::Colors().separator.Opacity(Environment().enabled ? 1.0f : 0.5f);
        const float width = Px(metrics.border);
        const CornerRadii radii(border == ScrollViewBorder::Rounded ? Px(metrics.roundedRadius) + width : 0.0f);
        Draw::StrokeRoundedRect(draw, ImRect(rect.Min - ImVec2(width, width), rect.Max + ImVec2(width, width)), radii, color, width);
    }

    void ScrollView(const ScrollViewOptions& options, const std::function<void()>& content) {
        const ImGuiID id = Layout::NextViewId();
        Layout::Placement placement;
        placement.size = Layout::Proposal();
        placement.flexibleWidth = true;
        placement.flexibleHeight = true;
        const ImRect rect = Layout::Place(placement);
        if (Layout::IsMeasuring() || rect.GetWidth() <= 0.0f || rect.GetHeight() <= 0.0f)
            return;

        if (options.border != ScrollViewBorder::None)
            DrawBorder(ImGui::GetWindowDrawList(), rect, options.border);
        const ImGuiID edge = Environment().scrollEdge;
        const ScrollEdgeStyle edge_style = Environment().scrollEdgeStyle;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(rect.Min);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | (options.axis == ImGuiAxis_X ? ImGuiWindowFlags_HorizontalScrollbar : 0);
        // A view that has just become a new one (Id) starts as if never drawn: at the top, or where its binding says.
        const bool fresh = Environment().identityChanged;
        if (fresh && !options.offset)
            ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        // Its controls join the window's Tab order.
        if (ImGui::BeginChild(id, rect.GetSize(), ImGuiChildFlags_NavFlattened, flags)) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ScrollerState& scroller = State::Get<ScrollerState>(window->ID);
            if (fresh)
                scroller = {};
            const ImGuiAxis axis = options.axis;
            // A new value in the binding scrolls there, once the content's size is known.
            if (options.offset && *options.offset != scroller.written && scroller.pending < 0.0f) {
                scroller.pending = *options.offset;
                scroller.pendingFrames = 0;
            }
            if (scroller.pending >= 0.0f && (window->ScrollMax[axis] > 0.0f || ++scroller.pendingFrames > 3)) {
                const float target = ImClamp(Px(scroller.pending), 0.0f, window->ScrollMax[axis]);
                axis == ImGuiAxis_Y ? ImGui::SetScrollY(target) : ImGui::SetScrollX(target);
                scroller.pending = -1.0f;
            }
            // Native window resizing owns the pointer while the edge grip is active; do not let a scroll view
            // acquire a competing active ID or pan the content during the same drag.
            if (!NativeHost::Resizing()) {
                if (axis == ImGuiAxis_X)
                    SidewaysWheel(window);
                PanInput(window, axis, scroller, options);
                if (options.showsIndicators)
                    ScrollerInput(window, axis, scroller);
            }
            // ImGui truncates the child's position to whole pixels; the content keeps the view's exact one.
            ImGui::SetCursorScreenPos(ImVec2(rect.Min.x - ImGui::GetScrollX(), rect.Min.y - ImGui::GetScrollY()));
            Layout::ContainerSpec spec;
            spec.arrangement = axis == ImGuiAxis_Y ? Layout::Arrangement::Vertical : Layout::Arrangement::Horizontal;
            spec.spacing = options.spacing;
            spec.padding = options.padding;
            spec.alignment.horizontal = options.alignment;
            spec.fillWidth = axis == ImGuiAxis_Y;
            spec.fillHeight = axis == ImGuiAxis_X;
            spec.role = options.role;
            WithEnvironment([](EnvironmentValues& environment) { environment.scrollEdge = 0; }, [&] { Layout::Container(spec, content); });
            if (edge) {
                State::Get<ScrollEdgeState>(edge) = {Pt(window->Scroll.y), ImGui::GetFrameCount()};
                if (window->Scroll.y > 0.0f)
                    DrawScrollEdge(window, edge_style);
            }
            if (options.showsIndicators)
                DrawScroller(window, axis, scroller, options.indicator);
            if (options.offset && scroller.pending < 0.0f) {
                *options.offset = Pt(window->Scroll[axis]);
                scroller.written = *options.offset;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void ScrollView(const std::function<void()>& content) {
        ScrollView(ScrollViewOptions{}, content);
    }

    void ScrollEdge(ImGuiID id, ScrollEdgeStyle style, const std::function<void()>& content) {
        WithEnvironment([&](EnvironmentValues& environment) {
            environment.scrollEdge = id;
            environment.scrollEdgeStyle = style;
        }, content);
    }

    float ScrollEdgeOffset(ImGuiID id) {
        const ScrollEdgeState& state = State::Get<ScrollEdgeState>(id);
        return state.frame == ImGui::GetFrameCount() ? state.offset : 0.0f;
    }
} // namespace Cupertino
