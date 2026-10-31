#include "widgets/menu_bar.h"

#include <utility>

namespace albor
{
    MenuBar::MenuBar(BindingEngine* engine, std::string explicit_id) :
        ToolBar(engine, std::move(explicit_id)) {}
} // namespace albor
