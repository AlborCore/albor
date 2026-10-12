#pragma once

#include <functional>
#include <span>

#include "core/scrollable_content.h"
#include "widgets/widget.h"

namespace albor
{
    // when a scrollbar is drawn, which is a per instance decision rather than a global one (§8)
    enum class ScrollbarVisibility
    {
        // the bar appears only while the content exceeds the viewport (§8)
        AsNeeded,

        // the content still scrolls, with no visible indicator at all (§8)
        AlwaysOff,

        // the bar is drawn whether or not the content overflows (§8)
        AlwaysOn
    };

    // the viewport over content larger than it shows, which clips, scrolls and reports its own offset (§8, AD-13)
    class ScrollView final : public Widget
    {
    public:
        // build an empty scroll view over the engine that settles its properties (§8)
        explicit ScrollView(BindingEngine* engine, std::string explicit_id = {});

        // measure the viewport the parent offers, which is independent of how large the content is (§8.2)
        void measure(const SizeConstraints& constraints, Size& out_size) override;

        // place the content at the current offset, which is what makes the offset move the content (§8.2)
        void arrange(const Rect& bounds) override;

        // override: claim the wheel and the paging keys, which is what scrolling the content means (§8.3)
        void dispatch_event(Event& event) override;

        // override: report whether a point lands inside the viewport, because a scrolled away row is not there (§8.3)
        [[nodiscard]] bool hit_test(Point logical_point) const override;

        // override: clip the children to the viewport and draw the bar over them (§6.2)
        void paint_children(RenderContext& context) override;

        // override: announce the viewport, which is what assistive technology scrolls (§10.1)
        [[nodiscard]] SemanticNode semantic_node() const override;

        // set the extent the viewport draws, which is what an author writes instead of a stretch (§8.2)
        void set_viewport_extent(Size size);

        // hand out the declared viewport extent, which is what the measure pass uses (§8.2)
        [[nodiscard]] Size viewport_extent() const;

        // set the height one virtualized row takes, which is what makes an index a position (§8.2, AD-13)
        void set_row_height(float pixels);

        // hand out the height one virtualized row takes, which the rows are built against (§8.2, AD-13)
        [[nodiscard]] float row_height() const;

        // choose when the bar is drawn, which is the policy this instance carries (§8)
        void set_scrollbar_visibility(ScrollbarVisibility visibility);

        // hand out when the bar is drawn, which is the policy this instance carries (§8)
        [[nodiscard]] ScrollbarVisibility scrollbar_visibility() const;

        // report whether the bar is drawn right now, which is what the policy resolves to (§8)
        [[nodiscard]] bool scrollbar_visible() const;

        // set how far beyond the visible area content is kept built, which is what hides a rebuild at the edge (§8.2)
        void set_overscan(float pixels);

        // hand out the overscan, which is how much content sits outside the visible area (§8.2)
        [[nodiscard]] float overscan() const;

        // bind the offset over its sources, which lets a log view pin itself to the bottom (§7.3, AD-24)
        void bind_offset(Property<float>::StaticEvaluator evaluator, void* context, std::span<const PropertyId> dependencies);

        // hand the offset to a caller, which is an imperative write and takes precedence over a binding (§7.3, AD-24)
        void set_offset(float offset);

        // hand out the offset the viewport currently presents, which a binding or a user scroll wrote (§7.3)
        [[nodiscard]] float offset() const;

        // report whether a user scroll is currently overriding the bound offset (§7.3, AD-24)
        [[nodiscard]] bool user_scrolled() const;

        // hand out how far the content can still move, which is zero while it fits (§8.2)
        [[nodiscard]] float maximum_offset() const;

        // hand out the extent of the content being scrolled, which the container owns (§8.2)
        [[nodiscard]] Size content_extent() const;

        // report whether the content extends past the viewport, which is what the as needed policy reads (§8)
        [[nodiscard]] bool scrollable() const;

        // move the offset by a signed amount, which a wheel notch and a key press both ask for (§8.3)
        void scroll_by(float amount);

        // move the offset to an absolute place, which a scrollbar drag and a binding both ask for (§8.2)
        void scroll_to(float offset);

        // install the work told about every change of the offset, which is what the signal reports (§8.3)
        void set_on_scroll(std::function<void(float)> handler);

        // report whether a handler is installed, because an unwatched scroll view still scrolls (§8.3)
        [[nodiscard]] bool watches() const;

    private:
        // hand out the amount one wheel notch moves the content, which is the conversion this view owns (§8.3)
        [[nodiscard]] float wheel_scale(const Event& event) const;

        // hand out the viewport the bounds describe, which the clip and the bar both read (§8.2)
        [[nodiscard]] Rect viewport_rect() const;

        // draw the bar over the content, which is what reports where the offset sits (§6.1)
        void paint_scrollbar(RenderContext& context) const;

        // hand out the bar the current offset puts at this place, which is what the travel is read from (§8.2)
        [[nodiscard]] Rect thumb_rect() const;

        // follow a pointer that is dragging the thumb, which maps the travel onto the offset (§8.3)
        void drag_to(Point logical_position);

        // hand out the groove colour for the current state, which is null without a theme (§6.1)
        [[nodiscard]] const Color* track_colors() const;

        // hand out the thumb colour for the current state, which is null without a theme (§6.1)
        [[nodiscard]] const Color* thumb_colors() const;

        // hand the content its window, which is what builds only the rows that intersect the viewport (§8.2, AD-13)
        void push_viewport() const;

        // the scrollable content the viewport is showing, which is null until one is adopted (§8.2)
        [[nodiscard]] ScrollableContent* scrollable_content() const;

        // the offset the viewport presents, which a binding or a user scroll writes (§7.3, AD-24)
        Property<float> m_offset;

        // the extent the viewport draws, which is the offered ceiling while an author writes none (§8.2)
        Size m_viewport_extent{0.0f, 0.0f};

        // the height one virtualized row takes, which is what the content is divided by (§8.2, AD-13)
        float m_row_height = 0.0f;

        // the extent of the content being scrolled, which the measure pass settled (§8.2)
        Size m_content_extent{0.0f, 0.0f};

        // the content extent the last measure settled, which is what a change is compared against (§7.3, AD-24)
        Size m_settled_content_extent{0.0f, 0.0f};

        // when the bar is drawn, which is a per instance policy (§8)
        ScrollbarVisibility m_visibility = ScrollbarVisibility::AsNeeded;

        // how far beyond the visible area content is kept built (§8.2)
        float m_overscan = 0.0f;

        // whether a drag is holding the thumb, which is what a pointer move steers (§8.3)
        bool m_dragging_thumb = false;

        // the offset the drag began at, so the pointer measures against where the thumb started (§8.3)
        float m_drag_origin_offset = 0.0f;

        // the pointer position the drag began at, which is the other half of that measurement (§8.3)
        Point m_drag_origin{0.0f, 0.0f};

        // the handler told about every change of the offset, which is what the signal reports (§8.3)
        std::function<void(float)> m_on_scroll;
    };
} // namespace albor
