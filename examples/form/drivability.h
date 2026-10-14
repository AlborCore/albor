#pragma once

#include <memory>
#include <string>

#include "semantics/semantic_provider.h"
#include "ui/ui_node.h"

namespace albor::example
{
    // the implementation, which names the platform and the automation types the header must not (§3.5, AD-05)
    class DrivableImpl;

    // the drivability an example gains where the automation core is built (§2.3, AD-04, AD-10)
    class Drivable
    {
    public:
        // build the drivability over a tree and the window it publishes into, which the caller owns (§10.1, §10.2)
        Drivable(UiNode& root, const void* native_window);

        ~Drivable();

        Drivable(const Drivable&) = delete;
        Drivable& operator=(const Drivable&) = delete;

        // refresh the semantic tree, publish it and serve one waiting client, which is what a frame calls (§10.2, §10.3)
        void refresh();

        // hand out the endpoint a client connects to, which a host prints so a person can find it (§10.3, AD-10)
        [[nodiscard]] const std::string& endpoint() const;

    private:
        // the live semantic tree, which is what assistive technology and automation both read (§10.1)
        SemanticTree m_semantics;

        // the implementation, which is where the linked translation unit keeps the tree root (§2.3, AD-04)
        std::unique_ptr<DrivableImpl> m_impl;
    };
} // namespace albor::example
