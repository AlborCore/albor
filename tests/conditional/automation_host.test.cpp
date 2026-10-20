#include <cstdint>
#include <gtest/gtest.h>

#include "application/application.h"
#include "automation/host/automation_host.h"

TEST(AutomationHost, AHostStartsWhenBound) {
    // arrange a dummy root (the host only checks non‑null to start the server)
    int dummy_root = 42;

    // act bind the dummy root to the host
    albor::automation::AutomationHost::instance().bind(&dummy_root, nullptr, nullptr);

    // assert the host reports running and published paths
    albor::automation::AutomationHost& host = albor::automation::AutomationHost::instance();
    EXPECT_TRUE(host.running());
    EXPECT_FALSE(host.paths().socket.empty());
    EXPECT_FALSE(host.paths().token.empty());
}

TEST(AutomationHost, AHostStopsWhenUnbound) {
    // arrange a dummy root
    int dummy_root = 99;

    // act bind then unbind the dummy root
    albor::automation::AutomationHost::instance().bind(&dummy_root, nullptr, nullptr);
    albor::automation::AutomationHost& host = albor::automation::AutomationHost::instance();
    EXPECT_TRUE(host.running());

    albor::automation::AutomationHost::instance().unbind(&dummy_root);
    EXPECT_FALSE(host.running());
}
