#pragma once

#include <string_view>

#include "core/dev_hook.h"
#include "devrt/reload_adapter.h"

namespace albor::devrt
{
    // take the tree of a main window and the engines behind it, ignoring the capture handles a reload never reads (§12.1)
    void bind_root(const dev::WindowBinding& binding);

    // set the asset root the dev service supplies, which is process wide and stays until the next call (§6.4, §12.1)
    void set_asset_root(std::string_view utf8_root);

    // take back a tree that went away, which the runtime calls when the window closes (§12.1, §5.2)
    void unbind_root(void* root);

    // hand out what the last reload changed, which a developer tool reads while typing (§12.1)
    [[nodiscard]] ReloadReport last_report();

    // report whether a tree is bound, which is what decides whether a reload has anything to patch (§12.1)
    [[nodiscard]] bool bound();
} // namespace albor::devrt
