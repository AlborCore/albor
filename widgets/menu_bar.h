#pragma once

#include <string>

#include "widgets/tool_bar.h"

namespace albor
{
    // the strip a shell puts across the top, which hosts the menu items or menus that belong there (§8, §18.2)
    class MenuBar final : public ToolBar
    {
    public:
        // build an empty menu bar over the engine that settles its properties (§8)
        explicit MenuBar(BindingEngine* engine, std::string explicit_id = {});
    };
} // namespace albor
