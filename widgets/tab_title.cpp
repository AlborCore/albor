#include "widgets/tab_title.h"

#include <utility>

namespace albor
{
    TabTitle::TabTitle(BindingEngine* engine, std::string explicit_id) :
        Widget(engine, std::move(explicit_id)) {}

    bool TabTitle::hit_test(Point logical_point) const {
        // a title is drawn by the strip, so a pointer belongs to the tab that owns it (§8.3)
        (void)logical_point;
        return false;
    }

    SemanticNode TabTitle::semantic_node() const {
        // a title is decorative, so it announces nothing beyond the tab item that names it (§10.1)
        return SemanticNode{SemanticRole::None};
    }

    bool TabTitle::accepts_focus() const {
        // a title is decorative, so it never holds the caret (§8.3)
        return false;
    }

    bool TabTitle::focusable_children() const {
        // the title's content is decorative, so the focus walk does not enter it (§8.3)
        return false;
    }

    void TabTitle::dispatch_event(Event& event) {
        // a title takes no input, because the tab around it is the target (§8.3)
        (void)event;
    }
} // namespace albor
