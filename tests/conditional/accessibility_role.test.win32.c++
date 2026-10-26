// the win32 half of the accessibility bridge: a role is reachable once it hands its tree to ui automation (§10.2, AD-16)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <wrl/client.h>
// clang-format on

#include <cstddef>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "accessibility/accessibility_adapter.h"
#include "accessibility/accessibility_platform.win32.h"
#include "semantics/semantic_provider.h"

using namespace albor;

namespace
{
    // a viewport with one child, which is the smallest tree a reader can walk into (§10.1)
    class ViewportProvider final : public SemanticProvider
    {
    public:
        SemanticNode semantic_node() const override {
            // the node carries the role and name this provider publishes, which the bridge maps one for one (§10.1)
            SemanticNode node{m_role};
            // the name is what a reader speaks, so it belongs on the node (§10.1)
            node.set_name(m_name);
            return node;
        }

        std::size_t semantic_child_count() const override {
            // only the viewport holds a child, because a label is a leaf (§10.1)
            return m_role == SemanticRole::ScrollView ? 1U : 0U;
        }

        std::unique_ptr<SemanticProvider> semantic_child(std::size_t index) const override {
            // the single child is a label, so the viewport has a hierarchy to announce (§10.2)
            if (index != 0)
                return nullptr;
            auto label = std::make_unique<ViewportProvider>();
            // the child is static text, which is what a row inside a viewport is (§10.1)
            label->m_role = SemanticRole::Label;
            // the child carries a name, which a reader speaks (§10.1)
            label->m_name = "first row";
            return label;
        }

        // the role this provider publishes, which the child overrides to be something else (§10.1)
        SemanticRole m_role = SemanticRole::ScrollView;

        // the name this provider publishes, which a reader speaks (§10.1)
        std::string m_name = "Log";
    };

    // a window the bridge publishes into, which the suite owns and releases (§5.2)
    class WindowFixture
    {
    public:
        WindowFixture() {
            // the window class is registered once per process, while a second registration is refused harmlessly (§5.3)
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = L"AlborAccessibilityTestWindow";
            RegisterClassExW(&window_class);
            // the window is hidden, because the provider is reached through messages rather than the screen (§10.2)
            m_window = CreateWindowExW(0, L"AlborAccessibilityTestWindow", L"Accessibility", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr, window_class.hInstance, nullptr);
            // the bridge is built for this platform, which is the one this suite exercises (§10.2)
            m_adapter = accessibility::create_adapter();
            // a bridge that was not built has nothing to attach, which the tests assert first (§10.2)
            if (m_window != nullptr && m_adapter != nullptr)
                m_adapter->attach_window(NativeWindowHandle::from_object(reinterpret_cast<const void*>(m_window)));
        }

        ~WindowFixture() {
            // the bridge lets go before the window dies, so a reader never sees released elements (§10.2)
            if (m_adapter != nullptr)
                m_adapter->detach_window();
            // the window is destroyed last, because the bridge released the subclass above (§5.2)
            if (m_window != nullptr)
                DestroyWindow(m_window);
        }

        // the window under test, which the suite reads the announced tree from (§10.2)
        [[nodiscard]] HWND window() const { return m_window; }

        // the bridge under test, which is what the suite publishes into (§10.2)
        [[nodiscard]] accessibility::AccessibilityAdapter& adapter() const { return *m_adapter; }

        // the fragment root the bridge exposes, which is the element ui automation receives (§10.2)
        [[nodiscard]] Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_fragment() const {
            // the root is the fragment root the bridge owns, which is queried for the fragment interface (§10.2)
            Microsoft::WRL::ComPtr<IRawElementProviderFragmentRoot> root(accessibility::platform::fragment_root_for(*m_adapter));
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment;
            if (root != nullptr)
                root.As(&fragment);
            return fragment;
        }

        // the simple interface of a fragment, which is where properties and patterns are read (§10.2)
        [[nodiscard]] static Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple_of(IRawElementProviderFragment* fragment) {
            // the fragment is queried for its simple interface, because the two interfaces are independent (§10.2)
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple;
            if (fragment != nullptr)
                fragment->QueryInterface(IID_PPV_ARGS(simple.GetAddressOf()));
            return simple;
        }

    private:
        // the window the bridge publishes into, which the suite owns (§5.2)
        HWND m_window = nullptr;

        // the bridge under test, which is what the suite publishes into (§10.2)
        std::unique_ptr<accessibility::AccessibilityAdapter> m_adapter;
    };
} // namespace

TEST(Win32AccessibilityRole, TheAnnouncedRootCarriesTheSemanticRole) {
    // arrange a tree whose root is a viewport, which is the node whose role is at issue (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    // act
    fixture.adapter().publish(tree);
    // assert the window answers the ui automation request, which is what makes the tree reachable (§10.2)
    EXPECT_NE(SendMessageW(fixture.window(), WM_GETOBJECT, 0, UiaRootObjectId), 0);
    // assert the window announces exactly the published root, because that is what makes it reachable (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced.GetAddressOf()));
    ASSERT_NE(announced.Get(), nullptr);
    // assert a viewport announces as the pane assistive technology scrolls, which is the win32 control type for it (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced.Get());
    ASSERT_NE(simple.Get(), nullptr);
    VARIANT control_type{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_PaneControlTypeId);
    VariantClear(&control_type);
}

TEST(Win32AccessibilityRole, TheAnnouncedTreeCarriesEveryChild) {
    // arrange
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    // act
    fixture.adapter().publish(tree);
    // assert the root's children are announced, which is the hierarchy a reader walks (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> viewport;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, viewport.GetAddressOf()));
    ASSERT_NE(viewport.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
    ASSERT_HRESULT_SUCCEEDED(viewport->Navigate(NavigateDirection_FirstChild, child.GetAddressOf()));
    ASSERT_NE(child.Get(), nullptr);
    // assert the child announces as static text and under its own name, which is what the node carries (§10.1)
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(child.Get());
    ASSERT_NE(simple.Get(), nullptr);
    VARIANT control_type{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_TextControlTypeId);
    VariantClear(&control_type);
    VARIANT name{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name));
    ASSERT_EQ(name.vt, VT_BSTR);
    EXPECT_STREQ(name.bstrVal, L"first row");
    VariantClear(&name);
}

TEST(Win32AccessibilityRole, TheWindowCarriesNothingBeforeAnythingIsPublished) {
    // arrange a bridge attached to a window that has published nothing (§10.2)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    // act
    fixture.adapter().publish(SemanticTree{});
    // assert the window answers the request but announces no child, because there is no tree to reach (§10.2)
    EXPECT_NE(SendMessageW(fixture.window(), WM_GETOBJECT, 0, UiaRootObjectId), 0);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
    EXPECT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, child.GetAddressOf()));
    EXPECT_EQ(child.Get(), nullptr);
}

TEST(Win32AccessibilityRole, ADetachedBridgeTakesItsTreeOutOfTheWindow) {
    // arrange
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    fixture.adapter().publish(tree);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    // act
    fixture.adapter().detach_window();
    // assert the window announces nothing, because a reader must not see a dead tree (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
    EXPECT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, child.GetAddressOf()));
    EXPECT_EQ(child.Get(), nullptr);
}

TEST(Win32AccessibilityRole, ATextFieldAnnouncesItsValue) {
    // arrange a tree whose root is an editable field carrying text (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::TextField});
    // the node needs the identity the tree walk keeps, and the text a reader would edit (§10.1)
    SemanticNode* field = tree.mutable_at(0);
    field->set_value("Albor");
    // act
    fixture.adapter().publish(tree);
    // assert the window announces the field, whose value is what assistive technology edits (§10.1, §10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced.GetAddressOf()));
    ASSERT_NE(announced.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced.Get());
    ASSERT_NE(simple.Get(), nullptr);
    VARIANT control_type{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_EditControlTypeId);
    VariantClear(&control_type);
    // assert the value reaches the element rather than only the tree it was published from (§10.2)
    VARIANT value{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ValueValuePropertyId, &value));
    ASSERT_EQ(value.vt, VT_BSTR);
    EXPECT_STREQ(value.bstrVal, L"Albor");
    VariantClear(&value);
}

TEST(Win32AccessibilityRole, ACheckboxStillAnnouncesItsCheckStateAsItsToggleState) {
    // arrange a tree whose root is a checkable node, which already had a value (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Checkbox});
    tree.mutable_at(0)->set_check(CheckState::Checked);
    // act
    fixture.adapter().publish(tree);
    // assert the check state is still announced, which the toggle pattern carries on win32 (§10.1, §10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced.GetAddressOf()));
    ASSERT_NE(announced.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced.Get());
    ASSERT_NE(simple.Get(), nullptr);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPatternProvider(UIA_TogglePatternId, pattern.GetAddressOf()));
    ASSERT_NE(pattern.Get(), nullptr);
    Microsoft::WRL::ComPtr<IToggleProvider> toggle;
    ASSERT_HRESULT_SUCCEEDED(pattern.As(&toggle));
    ToggleState state = ToggleState_Off;
    ASSERT_HRESULT_SUCCEEDED(toggle->get_ToggleState(&state));
    EXPECT_EQ(state, ToggleState_On);
}

TEST(Win32AccessibilityRole, AListAnnouncesAsAList) {
    // arrange a tree whose root is a list, which is the role at issue (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::List});
    // act
    fixture.adapter().publish(tree);
    // assert the list announces as the list role, which is the container assistive technology navigates rows in (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced.GetAddressOf()));
    ASSERT_NE(announced.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced.Get());
    ASSERT_NE(simple.Get(), nullptr);
    VARIANT control_type{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_ListControlTypeId);
    VariantClear(&control_type);
}

TEST(Win32AccessibilityRole, ATabViewAnnouncesItsTabsAndTheirSelection) {
    // arrange a tab group with two pages, the second selected (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    std::size_t tabs = tree.add(SemanticNode{SemanticRole::TabView});
    std::size_t first = tree.add(SemanticNode{SemanticRole::TabItem}, tabs);
    std::size_t second = tree.add(SemanticNode{SemanticRole::TabItem}, tabs);
    tree.mutable_at(first)->set_name("One");
    tree.mutable_at(second)->set_name("Two");
    tree.mutable_at(second)->set_selected(true);
    // the first page records the activate action, which the selection pattern presses (§10.1)
    bool activated = false;
    (void)tree.mutable_at(first)->add_action("activate", [&activated] { activated = true; });
    // act
    fixture.adapter().publish(tree);
    // assert the group announces as a tab control, which is the container a reader navigates pages in (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> group = WindowFixture::simple_of(root.Get());
    ASSERT_NE(group.Get(), nullptr);
    VARIANT control_type{};
    ASSERT_HRESULT_SUCCEEDED(group->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_TabControlTypeId);
    VariantClear(&control_type);
    // assert the first page announces as a tab item and exposes the selection item pattern (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_first;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced_first.GetAddressOf()));
    ASSERT_NE(announced_first.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> first_simple = WindowFixture::simple_of(announced_first.Get());
    ASSERT_NE(first_simple.Get(), nullptr);
    ASSERT_HRESULT_SUCCEEDED(first_simple->GetPropertyValue(UIA_ControlTypePropertyId, &control_type));
    ASSERT_EQ(control_type.vt, VT_I4);
    EXPECT_EQ(control_type.lVal, UIA_TabItemControlTypeId);
    VariantClear(&control_type);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(first_simple->GetPatternProvider(UIA_SelectionItemPatternId, pattern.GetAddressOf()));
    ASSERT_NE(pattern.Get(), nullptr);
    Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
    ASSERT_HRESULT_SUCCEEDED(pattern.As(&selection));
    BOOL selected = TRUE;
    ASSERT_HRESULT_SUCCEEDED(selection->get_IsSelected(&selected));
    EXPECT_FALSE(selected);
    // act the first page is selected through the pattern (§10.2)
    ASSERT_HRESULT_SUCCEEDED(selection->Select());
    // assert the page's activate action ran, because a tab selection is an activation (§10.1)
    EXPECT_TRUE(activated);
    // assert a tab item cannot be removed from the single selection, which the pattern reports success for (§10.2)
    EXPECT_HRESULT_SUCCEEDED(selection->RemoveFromSelection());
}

TEST(Win32AccessibilityRole, ASelectedRowAnnouncesItsSelectedState) {
    // arrange a list whose single row is the chosen one (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    std::size_t list = tree.add(SemanticNode{SemanticRole::List});
    std::size_t row = tree.add(SemanticNode{SemanticRole::ListItem}, list);
    tree.mutable_at(row)->set_selected(true);
    // act
    fixture.adapter().publish(tree);
    // assert the chosen row announces itself selected, which is the state a reader speaks (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_list;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced_list.GetAddressOf()));
    ASSERT_NE(announced_list.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_row;
    ASSERT_HRESULT_SUCCEEDED(announced_list->Navigate(NavigateDirection_FirstChild, announced_row.GetAddressOf()));
    ASSERT_NE(announced_row.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced_row.Get());
    ASSERT_NE(simple.Get(), nullptr);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPatternProvider(UIA_SelectionItemPatternId, pattern.GetAddressOf()));
    ASSERT_NE(pattern.Get(), nullptr);
    Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
    ASSERT_HRESULT_SUCCEEDED(pattern.As(&selection));
    BOOL selected = FALSE;
    ASSERT_HRESULT_SUCCEEDED(selection->get_IsSelected(&selected));
    EXPECT_TRUE(selected);
}

TEST(Win32AccessibilityRole, AnUnchosenRowDoesNotAnnounceSelection) {
    // arrange a list whose single row is not the chosen one (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    std::size_t list = tree.add(SemanticNode{SemanticRole::List});
    tree.add(SemanticNode{SemanticRole::ListItem}, list);
    // act
    fixture.adapter().publish(tree);
    // assert an unselected row announces no selection, because the state defaults quiet (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_list;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced_list.GetAddressOf()));
    ASSERT_NE(announced_list.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced_row;
    ASSERT_HRESULT_SUCCEEDED(announced_list->Navigate(NavigateDirection_FirstChild, announced_row.GetAddressOf()));
    ASSERT_NE(announced_row.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced_row.Get());
    ASSERT_NE(simple.Get(), nullptr);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPatternProvider(UIA_SelectionItemPatternId, pattern.GetAddressOf()));
    ASSERT_NE(pattern.Get(), nullptr);
    Microsoft::WRL::ComPtr<ISelectionItemProvider> selection;
    ASSERT_HRESULT_SUCCEEDED(pattern.As(&selection));
    BOOL selected = TRUE;
    ASSERT_HRESULT_SUCCEEDED(selection->get_IsSelected(&selected));
    EXPECT_FALSE(selected);
}

TEST(Win32AccessibilityRole, APlainLabelAnnouncesNoValue) {
    // arrange a tree whose root carries no value of any kind (§10.1)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.add(SemanticNode{SemanticRole::Label});
    // act
    fixture.adapter().publish(tree);
    // assert a role with nothing to report announces nothing rather than an empty string (§10.2)
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> root = fixture.announced_fragment();
    ASSERT_NE(root.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> announced;
    ASSERT_HRESULT_SUCCEEDED(root->Navigate(NavigateDirection_FirstChild, announced.GetAddressOf()));
    ASSERT_NE(announced.Get(), nullptr);
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> simple = WindowFixture::simple_of(announced.Get());
    ASSERT_NE(simple.Get(), nullptr);
    VARIANT value{};
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_ValueValuePropertyId, &value));
    EXPECT_EQ(value.vt, VT_EMPTY);
    VariantClear(&value);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPatternProvider(UIA_ValuePatternId, pattern.GetAddressOf()));
    EXPECT_EQ(pattern.Get(), nullptr);
}

TEST(Win32AccessibilityRole, AUiaClientSeesThePublishedRoot) {
    // arrange a published viewport, which a ui automation client should reach through the window handle (§10.2)
    WindowFixture fixture;
    ASSERT_NE(fixture.window(), nullptr);
    SemanticTree tree;
    tree.build_from(ViewportProvider());
    fixture.adapter().publish(tree);
    // the window is shown off screen, because the client walks a window the desktop has composed (§10.2)
    ShowWindow(fixture.window(), SW_SHOWNOACTIVATE);
    // the client is initialized on the test thread, because the ui automation client object is a com object (§10.2)
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE);
    // the client lives in a scope, so its com objects release before the apartment closes (§10.2)
    {
        Microsoft::WRL::ComPtr<IUIAutomation> automation;
        ASSERT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
        Microsoft::WRL::ComPtr<IUIAutomationElement> window;
        ASSERT_HRESULT_SUCCEEDED(automation->ElementFromHandle(fixture.window(), &window));
        // the client searches for the pane the root announces, which the window frame sits beside (§10.2)
        VARIANT pane{};
        VariantInit(&pane);
        pane.vt = VT_I4;
        pane.lVal = UIA_PaneControlTypeId;
        Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
        ASSERT_HRESULT_SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, pane, &condition));
        VariantClear(&pane);
        // act the client walks into the window, which is where the published root is announced (§10.2)
        Microsoft::WRL::ComPtr<IUIAutomationElement> child;
        ASSERT_HRESULT_SUCCEEDED(window->FindFirst(TreeScope_Descendants, condition.Get(), &child));
        ASSERT_NE(child.Get(), nullptr);
        CONTROLTYPEID control_type = 0;
        ASSERT_HRESULT_SUCCEEDED(child->get_CurrentControlType(&control_type));
        EXPECT_EQ(control_type, UIA_PaneControlTypeId);
    }
    // the published tree is taken out before the apartment closes, because the provider map is com backed (§10.2)
    fixture.adapter().detach_window();
    if (SUCCEEDED(initialized))
        CoUninitialize();
}
