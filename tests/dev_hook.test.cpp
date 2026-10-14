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

        // how many times a release arrived, because that is the count a test asserts on (§5.2)
        int releases = 0;

        // the root the last release named, because a release carries the tree it owns (§5.2)
        void* released_root = nullptr;
    };

    // the state the double records into, which the installed entry points write (§12.1)
    BinderDouble g_double;

    // the binder the double installs, which the runtime calls when a main window is built (§12.1)
    void bind(void* root, void* engine, void* text) {
        // the offered tree and its engines are recorded whole, because that is the whole contract (§12.1)
        g_double.bound_root = root;
        g_double.bound_engine = engine;
        g_double.bound_text = text;
    }

    // the release the double installs, which the runtime calls when that window closes (§5.2)
    void unbind(void* root) {
        // the named root and the count are recorded, because both are what a caller reads back (§5.2)
        g_double.released_root = root;
        g_double.releases++;
    }

    // the inert entry points, which keep the hook occupied once a test has exercised the contract (§12.1)
    void bind_nothing(void*, void*, void*) {}
    void unbind_nothing(void*) {}

    // the scope that installs the double, leaving a binder behind rather than an empty hook (§12.1)
    class InstalledBinder
    {
    public:
        InstalledBinder() { albor::dev::install_root_binder(&bind, &unbind); }

        ~InstalledBinder() { albor::dev::install_root_binder(&bind_nothing, &unbind_nothing); }
    };
} // namespace

TEST(DevRootHookChecks, ABuildWithNoBinderReportsNone) {
    // arrange a build where nothing installed a binder, which is what a release link is (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    // assert the hook reports the absence, because every call site checks it before calling (§12.1, AD-02)
    EXPECT_FALSE(albor::dev::binder_present());
}

TEST(DevRootHookChecks, AnInstalledBinderIsReported) {
    // arrange the double, which is what linking an extension does (§12.1, AD-02)
    InstalledBinder scope;
    // act and assert the hook reports the arrival, because the runtime null checks on this (§12.1, AD-02)
    EXPECT_TRUE(albor::dev::binder_present());
}

TEST(DevRootHookChecks, ATreeIsOfferedToTheInstalledBinder) {
    // arrange the double, and the three opaque pointers a stage hands over (§12.1, AD-02)
    InstalledBinder scope;
    int root = 0;
    int engine = 0;
    int text = 0;
    // act hand a tree to the runtime, which forwards it to whoever installed (§12.1)
    albor::dev::bind_root(&root, &engine, &text);
    // assert all three arrived, because a rebuild needs the engines as much as the tree (§8.1, §9.1)
    EXPECT_EQ(g_double.bound_root, &root);
    EXPECT_EQ(g_double.bound_engine, &engine);
    EXPECT_EQ(g_double.bound_text, &text);
}

TEST(DevRootHookChecks, ATreeIsReleasedToTheBinderThatOwnsIt) {
    // arrange the double and a bound tree, which is what a main window leaves behind (§12.1, §5.2)
    InstalledBinder scope;
    int root = 0;
    albor::dev::bind_root(&root, nullptr, nullptr);
    // act the window closes, which is what the runtime announces (§5.2)
    albor::dev::unbind_root(&root);
    // assert the release arrived and named that tree, because a rebuild must not outlive its window (§5.2)
    EXPECT_EQ(g_double.releases, 1);
    EXPECT_EQ(g_double.released_root, &root);
}

TEST(DevRootHookChecks, AReleaseWithNoBinderDoesNothing) {
    // arrange a build where nothing installed a binder, which is what a release link is (§12.1, AD-02)
    albor::dev::install_root_binder(nullptr, nullptr);
    int root = 0;
    // act the runtime offers and releases a tree that nobody is listening for (§12.1, AD-02)
    albor::dev::bind_root(&root, nullptr, nullptr);
    albor::dev::unbind_root(&root);
    // assert nothing crashed, because the hook is null checked in every configuration (§12.1, AD-02)
    SUCCEED();
}
