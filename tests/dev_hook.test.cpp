#include <gtest/gtest.h>

#include "core/dev_hook.h"

namespace
{
    // the extension double, which records what the runtime handed it and what it released (§12.1, AD-02)
    struct BinderDouble
    {
        // the root the runtime offered, which is what a reload would patch (§12.1)
        void* bound_root = nullptr;

        // the engines the runtime offered, which a rebuilt node borrows (§8.1, §9.1)
        void* bound_engine = nullptr;
        void* bound_text = nullptr;

        // the capture handles the runtime offered, which an automation screenshot reads (§6.3, §10.3)
        void* bound_surface = nullptr;
        void* bound_executor = nullptr;

        // how many offers arrived, because every observer must receive the same tree (§12.1, AD-02)
        int binds = 0;

        // how many times a release arrived, because that is the count a test asserts on (§5.2)
        int releases = 0;

        // the root the last offer or release named, because each call carries the tree it owns (§5.2)
        void* named_root = nullptr;
    };

    // the first observer, which is the reload extension's place in the table (§12.1)
    BinderDouble g_first;

    // the second observer, which is the automation host's place in the same table (§12.1, AD-02)
    BinderDouble g_second;

    // the first offer, which records the tree and its engines (§12.1)
    void bind_first(const albor::dev::WindowBinding& binding) {
        // the offered tree and its engines are recorded whole, because that is the whole contract (§12.1)
        g_first.bound_root = binding.root;
        g_first.bound_engine = binding.engine;
        g_first.bound_text = binding.text;
        g_first.bound_surface = binding.surface;
        g_first.bound_executor = binding.executor;
        g_first.named_root = binding.root;
        g_first.binds++;
    }

    // the first release, which records the tree that went away (§5.2)
    void unbind_first(void* root) {
        // the named root and the count are recorded, because both are what a caller reads back (§5.2)
        g_first.named_root = root;
        g_first.releases++;
    }

    // the second offer, which proves the table reaches every observer rather than only the last (§12.1)
    void bind_second(const albor::dev::WindowBinding& binding) {
        // the offered tree and its engines are recorded whole, because that is the whole contract (§12.1)
        g_second.bound_root = binding.root;
        g_second.bound_engine = binding.engine;
        g_second.bound_text = binding.text;
        g_second.bound_surface = binding.surface;
        g_second.bound_executor = binding.executor;
        g_second.named_root = binding.root;
        g_second.binds++;
    }

    // the second release, which proves every observer hears about the same tree (§5.2)
    void unbind_second(void* root) {
        // the named root and the count are recorded, because both are what a caller reads back (§5.2)
        g_second.named_root = root;
        g_second.releases++;
    }
} // namespace

TEST(DevRootHookChecks, ABuildWithNoBinderReportsNone) {
    // arrange a build where nothing installed a binder, which is what a release link is (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    int root = 0;
    // act the runtime offers and releases a tree that nobody is listening for (§12.1, AD-02)
    albor::dev::bind_root(albor::dev::WindowBinding{.root = &root});
    albor::dev::unbind_root(&root);
    // assert nothing crashed, because the hook is null checked in every configuration (§12.1, AD-02)
    EXPECT_FALSE(albor::dev::binder_present());
}

TEST(DevRootHookChecks, AnInstalledBinderIsReported) {
    // arrange the double, which is what linking an extension does (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_first, &unbind_first));
    // act and assert the hook reports the arrival, because the runtime null checks on this (§12.1, AD-02)
    EXPECT_TRUE(albor::dev::binder_present());
}

TEST(DevRootHookChecks, ATreeIsOfferedToEveryInstalledBinder) {
    // arrange two observers and the five opaque pointers a stage hands over (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_first, &unbind_first));
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_second, &unbind_second));
    int root = 0;
    int engine = 0;
    int text = 0;
    int surface = 0;
    int executor = 0;
    g_first.binds = 0;
    g_second.binds = 0;
    // act hand a tree to the runtime, which forwards it to every observer (§12.1)
    albor::dev::bind_root(albor::dev::WindowBinding{.root = &root, .engine = &engine, .text = &text, .surface = &surface, .executor = &executor});
    // assert all five arrived at both observers, because a capture needs the surface as much as a rebuild needs the engines
    EXPECT_EQ(g_first.bound_root, &root);
    EXPECT_EQ(g_first.bound_engine, &engine);
    EXPECT_EQ(g_first.bound_text, &text);
    EXPECT_EQ(g_first.bound_surface, &surface);
    EXPECT_EQ(g_first.bound_executor, &executor);
    EXPECT_EQ(g_second.bound_root, &root);
    EXPECT_EQ(g_second.bound_engine, &engine);
    EXPECT_EQ(g_second.bound_text, &text);
    EXPECT_EQ(g_second.bound_surface, &surface);
    EXPECT_EQ(g_second.bound_executor, &executor);
    EXPECT_EQ(g_first.binds, 1);
    EXPECT_EQ(g_second.binds, 1);
}

TEST(DevRootHookChecks, ATreeIsReleasedToEveryInstalledBinder) {
    // arrange two observers and a bound tree, which is what a main window leaves behind (§12.1, §5.2)
    albor::dev::install_root_binder(nullptr, nullptr);
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_first, &unbind_first));
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_second, &unbind_second));
    int root = 0;
    g_first.releases = 0;
    g_second.releases = 0;
    albor::dev::bind_root(albor::dev::WindowBinding{.root = &root});
    // act the window closes, which is what the runtime announces (§5.2)
    albor::dev::unbind_root(&root);
    // assert every observer heard the release and named that tree, because a rebuild must not outlive its window (§5.2)
    EXPECT_EQ(g_first.releases, 1);
    EXPECT_EQ(g_first.named_root, &root);
    EXPECT_EQ(g_second.releases, 1);
    EXPECT_EQ(g_second.named_root, &root);
}

TEST(DevRootHookChecks, InstallingOneBinderReplacesThePreviousSet) {
    // arrange a first observer, then the replace operation a host or a test uses (§12.1)
    albor::dev::install_root_binder(nullptr, nullptr);
    ASSERT_TRUE(albor::dev::add_root_binding(&bind_first, &unbind_first));
    albor::dev::install_root_binder(&bind_second, &unbind_second);
    int root = 0;
    g_first.binds = 0;
    g_second.binds = 0;
    // act the runtime offers a tree after the replacement (§12.1)
    albor::dev::bind_root(albor::dev::WindowBinding{.root = &root});
    // assert only the replacement heard it, because replace means replace rather than append (§12.1)
    EXPECT_EQ(g_first.binds, 0);
    EXPECT_EQ(g_second.binds, 1);
}

TEST(DevRootHookChecks, AnIncompleteBinderIsRefused) {
    // arrange an empty hook and two incomplete pairs, which cannot observe and release a tree together (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    // act every incomplete form is offered to the table (§12.1)
    const bool missing_bind = albor::dev::add_root_binding(nullptr, &unbind_first);
    const bool missing_unbind = albor::dev::add_root_binding(&bind_first, nullptr);
    albor::dev::install_root_binder(nullptr, &unbind_first);
    // assert neither arrived, because a half installed observer is worse than none (§12.1, AD-02)
    EXPECT_FALSE(missing_bind);
    EXPECT_FALSE(missing_unbind);
    EXPECT_FALSE(albor::dev::binder_present());
    albor::dev::install_root_binder(&bind_first, nullptr);
    // assert the other incomplete replace form is also empty (§12.1, AD-02)
    EXPECT_FALSE(albor::dev::binder_present());
}

TEST(DevRootHookChecks, AFullBinderTableRefusesAnotherObserver) {
    // arrange an empty hook and as many observers as the table holds (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    g_first.binds = 0;
    g_first.releases = 0;
    g_second.binds = 0;
    g_second.releases = 0;
    for (int index = 0; index < 8; ++index)
        ASSERT_TRUE(albor::dev::add_root_binding(&bind_first, &unbind_first));
    // act one more observer is offered, then the tree is bound and released (§12.1)
    const bool overflow = albor::dev::add_root_binding(&bind_second, &unbind_second);
    int root = 0;
    albor::dev::bind_root(albor::dev::WindowBinding{.root = &root});
    albor::dev::unbind_root(&root);
    // assert the table refused the ninth, kept the eight, and called every one of them (§12.1, AD-02)
    EXPECT_FALSE(overflow);
    EXPECT_TRUE(albor::dev::binder_present());
    EXPECT_EQ(g_first.binds, 8);
    EXPECT_EQ(g_first.releases, 8);
    EXPECT_EQ(g_second.binds, 0);
    EXPECT_EQ(g_second.releases, 0);
    albor::dev::install_root_binder(nullptr, nullptr);
}
