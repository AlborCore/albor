#include <cstdio>

#include "devrt/devrt.h"

#include "core/dev_hook.h"
#include "runtime/binding_engine.h"
#include "text/text_engine.h"
#include "ui/ui_node.h"

namespace albor::devrt
{
    namespace
    {
        // the tree the runtime bound, which is what a reload patches in place (§12.1, AD-02)
        UiNode* g_root = nullptr;

        // the reactive engine the bound tree was built over, which a rebuilt node declares its properties over (§7.3)
        BindingEngine* g_engine = nullptr;

        // the text engine the bound tree measures with, which a rebuilt node borrows for its whole life (§9.1)
        TextEngine* g_text = nullptr;

        // the report of the last reload, which a developer tool reads while typing (§12.1)
        ReloadReport g_last;

        // the reload entry the runtime calls, which applies the definition the dev service delivered (§12.1, AD-12)
        void handle_reload(const std::string& ir) {
            // a reload before any window exists has no tree to patch, which is the quiet default (§12.1, AD-02)
            if (g_root == nullptr) {
                // the report is cleared, because a tool that reads it must see that this attempt applied nothing (§12.1)
                g_last = ReloadReport{};
                return;
            }
            // the definition is parsed first, because a malformed one changes nothing (§12.1)
            const std::optional<IrNode> parsed = parse_ir(ir);
            // a definition the reader refused is dropped, because patching half of it would be a lie (§12.1)
            if (!parsed) {
                // the report is cleared, because a tool that reads it must see that this attempt applied nothing (§12.1)
                g_last = ReloadReport{};
                return;
            }
            // the context carries the engines and the closed set, which is all a patch needs (§12.1, AD-01)
            const ReloadContext context{
                .engine = g_engine,
                .text = g_text,
                // the closed builtin set builds the nodes, which is the registry a Release never carries (AD-01)
                .factory = [](std::string_view element, BindingEngine& engine, TextEngine& text) { return create_node(element, engine, text); },
                // the live nodes are named by their dynamic type, which finds an element the author changed (§12.1)
                .element_of = [](const UiNode& node) { return element_of(node); },
            };
            // the tree is reconciled in place, which is what preserves the state a reload must keep (§12.1, §12.2)
            g_last = apply_reload(*g_root, *parsed, context);
            // the tree is invalidated, because a patched tree has to repaint to show what it now declares (§6.1)
            g_root->invalidate_layout();
        }

        // the installer that registers the extension's entry points at load, so linking it is the switch (§12.1, AD-02)
        struct DevAutoInstall
        {
            DevAutoInstall() {
                // the tree binder is appended, because the automation host may observe the same tree (§12.1, AD-02)
                const bool installed = dev::add_root_binding(&bind_root, &unbind_root);
                // a refusal is reported, because a silent refusal leaves the extension without a tree to reload (§12.1)
                if (!installed)
                    std::fprintf(stderr, "albor devrt: the extension could not register its tree observer\n");
                // the reload entry follows, so a definition that arrives before any window is simply dropped (§12.1)
                dev::install(&handle_reload);
            }
        };

        // the installer is never referenced, because the load runs it before any hook is read (§12.1, AD-02)
        const DevAutoInstall g_dev_auto_install;
    } // namespace

    void bind_root(const dev::WindowBinding& binding) {
        // a hand drawn window brings no tree a reload could patch, so it never displaces the one a developer edits (§6.1)
        if (!binding.has_tree)
            return;
        // the opaque pointers are the runtime's own types, because core names neither of them (§12.1, AD-02)
        g_root = static_cast<UiNode*>(binding.root);
        g_engine = static_cast<BindingEngine*>(binding.engine);
        g_text = static_cast<TextEngine*>(binding.text);
    }

    void unbind_root(void* root) {
        // only the tree this extension holds is released, because another window may have taken over (§5.2)
        if (g_root != static_cast<UiNode*>(root))
            return;
        // the engines go with the tree, because a rebuild borrows them for the whole run (§8.1, §9.1)
        g_engine = nullptr;
        g_text = nullptr;
        // the tree is cleared last, which is what stops a reload reaching a dead window (§12.1, §5.2)
        g_root = nullptr;
    }

    ReloadReport last_report() {
        // the report is handed back by value, because it is what a tool reads after a reload (§12.1)
        return g_last;
    }

    bool bound() {
        // a bound extension has a tree to patch, which is what makes a reload mean anything (§12.1)
        return g_root != nullptr;
    }
} // namespace albor::devrt

// the install mark the dev runtime defines, so two copies on one line refuse the binary (§12.1, AD-02)
extern "C" int albor_devrt_marker = 1;
