#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <UIAutomationClient.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "accessibility/accessibility_platform.h"
#include "accessibility/accessibility_platform.win32.h"

namespace albor::accessibility::platform
{
    namespace
    {
        // the subclass identifier is unique per bridge, so two bridges on one window do not displace each other (§10.2)
        constexpr UINT_PTR SUBCLASS_ID_OFFSET = 1;

        // one immutable publication, so a provider handed to ui automation keeps its tree after a rebuild (§10.2)
        struct Publication
        {
            // the tree as published, which every provider reads and none writes (§10.2)
            SemanticTree tree;
        };

        // the root index of a publication, which is the node assistive technology enters by (§10.1)
        std::size_t root_index(const Publication& publication) {
            // the flat tree carries the shape in its parent links, so the one node without a parent is the root (§10.1)
            for (std::size_t index = 0; index < publication.tree.size(); index++)
                if (publication.tree.parent_of(index) == SemanticTree::NO_PARENT)
                    return index;
            // a tree with no root has nothing to announce, which the quiet default states (§10.2)
            return SemanticTree::NO_PARENT;
        }

        // the ui automation control type a semantic role announces, which is a one to one mapping (§10.1, §10.2)
        LONG control_type_for(SemanticRole role) {
            // a switch covers every role, because the two enumerations correspond (§10.1, §10.2)
            switch (role) {
            case SemanticRole::None:
                return UIA_CustomControlTypeId;
            case SemanticRole::Button:
                return UIA_ButtonControlTypeId;
            case SemanticRole::Checkbox:
                return UIA_CheckBoxControlTypeId;
            case SemanticRole::Heading:
            case SemanticRole::Label:
                return UIA_TextControlTypeId;
            case SemanticRole::Slider:
                return UIA_SliderControlTypeId;
            case SemanticRole::ProgressBar:
                return UIA_ProgressBarControlTypeId;
            case SemanticRole::TabView:
                return UIA_TabControlTypeId;
            case SemanticRole::TabItem:
                return UIA_TabItemControlTypeId;
            case SemanticRole::RadioGroup:
                return UIA_GroupControlTypeId;
            case SemanticRole::RadioButton:
                return UIA_RadioButtonControlTypeId;
            case SemanticRole::NumericStepper:
                return UIA_SpinnerControlTypeId;
            case SemanticRole::Canvas:
                return UIA_PaneControlTypeId;
            case SemanticRole::TextField:
                return UIA_EditControlTypeId;
            case SemanticRole::Image:
                return UIA_ImageControlTypeId;
            case SemanticRole::ListItem:
                return UIA_ListItemControlTypeId;
            case SemanticRole::List:
                return UIA_ListControlTypeId;
            case SemanticRole::ComboBox:
                return UIA_ComboBoxControlTypeId;
            case SemanticRole::Group:
                return UIA_GroupControlTypeId;
            case SemanticRole::ScrollView:
                return UIA_PaneControlTypeId;
            case SemanticRole::Window:
                return UIA_WindowControlTypeId;
            }
            // a role outside the enumeration announces the custom type, which is the quiet default (§10.2)
            return UIA_CustomControlTypeId;
        }

        // report whether assistive technology may focus a role, which the focus pattern asks (§10.2)
        bool focusable_role(SemanticRole role) {
            // the roles that take keyboard input are the controls a user operates, which the default set names (§10.1)
            switch (role) {
            case SemanticRole::Button:
            case SemanticRole::Checkbox:
            case SemanticRole::Slider:
            case SemanticRole::TextField:
            case SemanticRole::ListItem:
            case SemanticRole::TabItem:
            case SemanticRole::RadioButton:
            case SemanticRole::NumericStepper:
            case SemanticRole::ComboBox:
                return true;
            default:
                return false;
            }
        }

        // the toggle state a semantic check state announces, which the toggle pattern reads (§10.1, §10.2)
        ToggleState toggle_state_for(CheckState check) {
            // the three checkable states map one to one, because ui automation names the same three (§10.1)
            switch (check) {
            case CheckState::Checked:
                return ToggleState_On;
            case CheckState::Unchecked:
                return ToggleState_Off;
            case CheckState::Indeterminate:
                return ToggleState_Indeterminate;
            case CheckState::NotCheckable:
                return ToggleState_Off;
            }
            // a state outside the enumeration reports off, which the quiet default states (§10.2)
            return ToggleState_Off;
        }

        // build a utf16 string from the utf8 a semantic node carries, which ui automation hands over as bstr (§10.2)
        BSTR bstr_from_utf8(std::string_view text) {
            // an empty text allocates no conversion buffer, because the api refuses a zero length source (§10.2)
            if (text.empty())
                return SysAllocStringLen(nullptr, 0);
            // the length is measured once in units, because the api answers the measure before the copy (§10.2)
            const int units = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            // a text that does not convert allocates an empty string rather than a dangling one (§10.2)
            if (units <= 0)
                return SysAllocStringLen(nullptr, 0);
            // the string is allocated before the units are written into it, because a bstr owns its buffer (§10.2)
            BSTR result = SysAllocStringLen(nullptr, static_cast<UINT>(units));
            if (result != nullptr)
                MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result, units);
            return result;
        }

        // build a bstr from the utf16 a window title carries, which ui automation hands over untyped (§10.2)
        BSTR bstr_from_wide(std::wstring_view text) {
            // an empty title allocates an empty string, because a null bstr is not a name a reader can speak (§10.2)
            if (text.empty())
                return SysAllocStringLen(nullptr, 0);
            return SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        }

        // set a variant to a boolean, which ui automation reads as a property value (§10.2)
        void set_variant_bool(VARIANT* value, bool flag) {
            // the variant is cleared first, because the caller owns whatever it held before (§10.2)
            VariantInit(value);
            value->vt = VT_BOOL;
            value->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
        }

        // set a variant to an integer, which ui automation reads as a property value (§10.2)
        void set_variant_int(VARIANT* value, LONG number) {
            // the variant is cleared first, because the caller owns whatever it held before (§10.2)
            VariantInit(value);
            value->vt = VT_I4;
            value->lVal = number;
        }

        // set a variant to a bstr, which the caller releases through the variant (§10.2)
        void set_variant_bstr(VARIANT* value, BSTR text) {
            // the variant is cleared first, because the caller owns whatever it held before (§10.2)
            VariantInit(value);
            value->vt = VT_BSTR;
            value->bstrVal = text;
        }

        // build the runtime id array a fragment announces, which is how ui automation keeps element identity (§10.2)
        SAFEARRAY* runtime_id_array(LONG id) {
            // the array holds the provider marker and the one identity the bridge assigns (§10.2)
            SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 2);
            // an array the allocator refused leaves the property empty, which the caller reports as unsupported (§10.2)
            if (array == nullptr)
                return nullptr;
            // the values are written through the array's own lock, because a safe array owns its storage (§10.2)
            LONG* values = nullptr;
            if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void**>(&values)))) {
                SafeArrayDestroy(array);
                return nullptr;
            }
            values[0] = UiaAppendRuntimeId;
            values[1] = id;
            SafeArrayUnaccessData(array);
            return array;
        }

        // build the four doubles a bounding rectangle carries, which ui automation reads in screen pixels (§10.2)
        SAFEARRAY* rectangle_array(const UiaRect& rectangle) {
            // the array holds left, top, width and height in that order, which the property contract fixes (§10.2)
            SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, 4);
            // an array the allocator refused leaves the property empty, which the caller reports as unsupported (§10.2)
            if (array == nullptr)
                return nullptr;
            // the values are written through the array's own lock, because a safe array owns its storage (§10.2)
            double* values = nullptr;
            if (FAILED(SafeArrayAccessData(array, reinterpret_cast<void**>(&values)))) {
                SafeArrayDestroy(array);
                return nullptr;
            }
            values[0] = rectangle.left;
            values[1] = rectangle.top;
            values[2] = rectangle.width;
            values[3] = rectangle.height;
            SafeArrayUnaccessData(array);
            return array;
        }

        // the scale factor the window reports, which separates logical pixels from physical ones (AD-27)
        double scale_of(HWND window) {
            // a window that is gone has no dpi to read, which the identity scale answers (§10.2)
            if (window == nullptr)
                return 1.0;
            // the dpi the window sits under is what the per monitor awareness hands out (AD-27)
            const UINT dpi = GetDpiForWindow(window);
            // a window that reports no dpi falls back to the identity scale, which the quiet default states (AD-27)
            return dpi == 0 ? 1.0 : static_cast<double>(dpi) / 96.0;
        }

        // the screen rectangle a node's logical bounds occupy, which is what ui automation reports (§10.2, AD-27)
        UiaRect screen_rect(HWND window, const Rect& bounds) {
            // a node with no window or no area has no rectangle to report, which the empty result states (§10.2)
            if (window == nullptr || bounds.empty())
                return UiaRect{};
            // the logical origin is grown by the scale before it is opened to screen pixels (AD-27)
            const double scale = scale_of(window);
            POINT origin{static_cast<LONG>(std::lround(bounds.x * scale)), static_cast<LONG>(std::lround(bounds.y * scale))};
            // a client point that cannot be converted has no screen rectangle, which the empty result states (§10.2)
            if (!ClientToScreen(window, &origin))
                return UiaRect{};
            return UiaRect{static_cast<double>(origin.x), static_cast<double>(origin.y), bounds.width * scale, bounds.height * scale};
        }

        // the client rectangle the window occupies, which the fragment root announces (§10.2)
        UiaRect window_rect(HWND window) {
            // a window that is gone has no rectangle to report, which the empty result states (§10.2)
            if (window == nullptr)
                return UiaRect{};
            // the client rect is the surface the framework draws into, which the root represents (§5.3)
            RECT client{};
            if (!GetClientRect(window, &client))
                return UiaRect{};
            // the client origin is opened to screen pixels, because ui automation reports screen coordinates (§10.2)
            POINT origin{0, 0};
            if (!ClientToScreen(window, &origin))
                return UiaRect{};
            return UiaRect{static_cast<double>(origin.x), static_cast<double>(origin.y), static_cast<double>(client.right - client.left), static_cast<double>(client.bottom - client.top)};
        }

        // report whether a point falls inside a node, which the point query answers (§10.2)
        bool contains(const Rect& bounds, float x, float y) {
            // a node with no area contains nothing, which keeps an empty element out of the hit test (§10.2)
            if (bounds.empty())
                return false;
            return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height;
        }

        // the depth of a node in the tree, which the point query uses to prefer the deepest hit (§10.2)
        std::size_t depth_of(const Publication& publication, std::size_t index) {
            // the walk climbs the parent links, which is the only shape a flat tree carries (§10.1)
            std::size_t depth = 0;
            for (std::size_t parent = publication.tree.parent_of(index); parent != SemanticTree::NO_PARENT; parent = publication.tree.parent_of(parent))
                depth++;
            return depth;
        }

        // the client point a screen point names in logical pixels, which the hit test uses (§10.2, AD-27)
        POINT logical_client_point(HWND window, double x, double y) {
            // the screen point is narrowed to the integer form the conversion takes (§10.2)
            POINT point{static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y))};
            // a point that cannot be converted names no client position, which the sentinel reports (§10.2)
            if (window == nullptr || !ScreenToClient(window, &point))
                return POINT{LONG_MIN, LONG_MIN};
            return point;
        }

        // forward declare the node provider, because the fragment root builds one before its body is declared (§10.2)
        class NodeProvider;

        // the fragment root of one window, which is the element assistive technology enters by (§10.2)
        class RootProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot
        {
        public:
            // the root starts attached to no window and publishes nothing, which a fresh bridge is (§10.2)
            RootProvider() = default;

            // the root releases the publication it held, which is the shared snapshot no element outlives unsafely (§10.2)
            ~RootProvider() = default;

            // the root counts the references ui automation and the node providers hold (§10.2)
            ULONG STDMETHODCALLTYPE AddRef() override {
                // the count is atomic, because a provider may be reached from a ui automation thread (§10.2)
                return m_references.fetch_add(1) + 1;
            }

            // the root is released by com, so the last reference destroys it (§10.2)
            ULONG STDMETHODCALLTYPE Release() override {
                // the decrement decides the destruction, because com hands no instruction beyond the count (§10.2)
                const ULONG remaining = m_references.fetch_sub(1) - 1;
                if (remaining == 0)
                    delete this;
                return remaining;
            }

            // com asks the root which interfaces it serves, which is the simple and the fragment set (§10.2)
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interface_id, void** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                if (interface_id == __uuidof(IUnknown) || interface_id == __uuidof(IRawElementProviderSimple))
                    *result = static_cast<IRawElementProviderSimple*>(this);
                else if (interface_id == __uuidof(IRawElementProviderFragment))
                    *result = static_cast<IRawElementProviderFragment*>(this);
                else if (interface_id == __uuidof(IRawElementProviderFragmentRoot))
                    *result = static_cast<IRawElementProviderFragmentRoot*>(this);
                else
                    return E_NOINTERFACE;
                // the handed out interface owns one reference, which com expects the caller to release (§10.2)
                AddRef();
                return S_OK;
            }

            // report the options the root provides under, a server side provider reachable from any thread (§10.2)
            HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (options == nullptr)
                    return E_POINTER;
                *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
                return S_OK;
            }

            // the root serves no pattern of its own, because the tree below it carries the controls (§10.2)
            HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                return S_OK;
            }

            // read one property of the window, which is the fragment root ui automation sees (§10.2)
            HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property_id, VARIANT* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                VariantInit(result);
                // the window is read once, because the title and the bounds both describe it (§10.2)
                const HWND window = m_window.load();
                switch (property_id) {
                case UIA_NamePropertyId: {
                    // a window that is gone has no title to announce, which the empty name states (§10.2)
                    if (window == nullptr) {
                        set_variant_bstr(result, SysAllocStringLen(nullptr, 0));
                        return S_OK;
                    }
                    // the title is read through the native window, which is the authority for it (§5.3)
                    const int length = GetWindowTextLengthW(window);
                    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
                    const int copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
                    title.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
                    set_variant_bstr(result, bstr_from_wide(title));
                    return S_OK;
                }
                case UIA_ControlTypePropertyId:
                    set_variant_int(result, UIA_WindowControlTypeId);
                    return S_OK;
                case UIA_AutomationIdPropertyId:
                    set_variant_bstr(result, SysAllocStringLen(nullptr, 0));
                    return S_OK;
                case UIA_IsEnabledPropertyId:
                    set_variant_bool(result, true);
                    return S_OK;
                case UIA_IsControlElementPropertyId:
                case UIA_IsContentElementPropertyId:
                    set_variant_bool(result, true);
                    return S_OK;
                case UIA_IsKeyboardFocusablePropertyId:
                    set_variant_bool(result, true);
                    return S_OK;
                case UIA_HasKeyboardFocusPropertyId:
                    set_variant_bool(result, window != nullptr && ::GetFocus() == window);
                    return S_OK;
                case UIA_IsOffscreenPropertyId:
                case UIA_IsPasswordPropertyId:
                    set_variant_bool(result, false);
                    return S_OK;
                case UIA_ProcessIdPropertyId:
                    set_variant_int(result, static_cast<LONG>(GetCurrentProcessId()));
                    return S_OK;
                case UIA_FrameworkIdPropertyId:
                    set_variant_bstr(result, SysAllocString(L"Albor"));
                    return S_OK;
                case UIA_BoundingRectanglePropertyId: {
                    // the property carries the four rectangle doubles, which the array helper builds (§10.2)
                    SAFEARRAY* rectangle = rectangle_array(window_rect(window));
                    if (rectangle == nullptr)
                        return S_OK;
                    result->vt = VT_ARRAY | VT_R8;
                    result->parray = rectangle;
                    return S_OK;
                }
                default:
                    return S_OK;
                }
            }

            // the root is the native window, so its host is the provider the window itself hosts (§10.2)
            HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // a window that is gone has no host provider, which the empty result states (§10.2)
                const HWND window = m_window.load();
                if (window == nullptr)
                    return S_OK;
                return UiaHostProviderFromHwnd(window, result);
            }

            // walk one step of the hierarchy from the window root, which reaches the published tree (§10.2)
            HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // the publication is a snapshot, because a rebuild may replace it while ui automation reads (§10.2)
                const std::shared_ptr<const Publication> publication = snapshot();
                if (publication == nullptr)
                    return S_OK;
                // the root of the semantic tree is the single child the window announces (§10.2)
                const std::size_t root = root_index(*publication);
                if (root == SemanticTree::NO_PARENT)
                    return S_OK;
                // only the two child directions name the root, because the window has no parent or sibling here (§10.2)
                if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
                    *result = make_node(publication, root);
                return S_OK;
            }

            // report the runtime id of the window root, which is unique within the provider (§10.2)
            HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = runtime_id_array(1);
                return *result == nullptr ? E_OUTOFMEMORY : S_OK;
            }

            // report the rectangle the window covers, which is the client area in screen pixels (§10.2)
            HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = window_rect(m_window.load());
                return S_OK;
            }

            // the window embeds no other fragment roots, because the published tree lives in this one (§10.2)
            HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                return S_OK;
            }

            // move keyboard focus to the window, which is the surface the root represents (§10.2)
            HRESULT STDMETHODCALLTYPE SetFocus() override {
                // a window that is gone cannot take focus, which the missing element reports (§10.2)
                const HWND window = m_window.load();
                if (window == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                ::SetFocus(window);
                return S_OK;
            }

            // the root is its own fragment root, which is how a fragment finds the top of its tree (§10.2)
            HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = this;
                AddRef();
                return S_OK;
            }

            // find the element under a screen point, which is the deepest node whose bounds contain it (§10.2)
            HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // the publication is a snapshot, because a rebuild may replace it while ui automation reads (§10.2)
                const std::shared_ptr<const Publication> publication = snapshot();
                // a bridge that published nothing has no element under the point, which the empty result states (§10.2)
                if (publication == nullptr)
                    return S_OK;
                // the screen point is opened to the client space the node bounds are measured in (§10.2, AD-27)
                const HWND window = m_window.load();
                POINT point = logical_client_point(window, x, y);
                // a point that cannot be converted names no element, which the empty result states (§10.2)
                if (point.x == LONG_MIN && point.y == LONG_MIN)
                    return S_OK;
                // the scale converts the converted client pixels back to logical ones, which the bounds carry (AD-27)
                const double scale = scale_of(window);
                const float logical_x = static_cast<float>(point.x / scale);
                const float logical_y = static_cast<float>(point.y / scale);
                // the deepest containing node is the one a hit test names, because a child sits over its parent (§10.2)
                std::size_t hit = SemanticTree::NO_PARENT;
                std::size_t hit_depth = 0;
                for (std::size_t index = 0; index < publication->tree.size(); index++) {
                    // a node the point misses is not the hit, which leaves the earlier candidate standing (§10.2)
                    if (!contains(publication->tree.at(index)->bounds(), logical_x, logical_y))
                        continue;
                    // the depth decides between overlapping nodes, because the deeper one is drawn above (§10.2)
                    const std::size_t depth = depth_of(*publication, index);
                    if (hit == SemanticTree::NO_PARENT || depth >= hit_depth) {
                        hit = index;
                        hit_depth = depth;
                    }
                }
                // a point inside no node names no element, which the empty result states (§10.2)
                if (hit == SemanticTree::NO_PARENT)
                    return S_OK;
                *result = make_node(publication, hit);
                return S_OK;
            }

            // hand out the node that holds focus, which is the element a reader follows (§10.2)
            HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // the publication is a snapshot, because a rebuild may replace it while ui automation reads (§10.2)
                const std::shared_ptr<const Publication> publication = snapshot();
                // a bridge that published nothing has no focused element, which the empty result states (§10.2)
                if (publication == nullptr)
                    return S_OK;
                for (std::size_t index = 0; index < publication->tree.size(); index++) {
                    // only a node that states it holds focus is announced, which the semantic state carries (§10.1)
                    if (!publication->tree.at(index)->focused())
                        continue;
                    *result = make_node(publication, index);
                    return S_OK;
                }
                return S_OK;
            }

            // attach the root to the window it publishes into, which a fresh bridge does at attach time (§10.2)
            void set_window(HWND window) {
                // the window is one atomic word, because a ui automation thread may read it during a rebuild (§10.2)
                m_window.store(window);
            }

            // replace the publication the root reads, which a rebuild and a detach both call (§10.2)
            void set_publication(std::shared_ptr<const Publication> publication) {
                // the lock protects the snapshot pointer, because a ui automation thread reads it concurrently (§10.2)
                std::lock_guard<std::mutex> lock(m_mutex);
                m_publication = std::move(publication);
            }

            // hand out the published snapshot, which every navigation call reads exactly once (§10.2)
            [[nodiscard]] std::shared_ptr<const Publication> snapshot() const {
                // the lock protects the snapshot pointer, because the ui thread replaces it concurrently (§10.2)
                std::lock_guard<std::mutex> lock(m_mutex);
                return m_publication;
            }

        private:
            // build a fragment for one published node, which transfers the node's first reference to the caller (§10.2)
            IRawElementProviderFragment* make_node(const std::shared_ptr<const Publication>& publication, std::size_t index) const;

            // the publication the root reads, which a rebuild replaces as one snapshot (§10.2)
            mutable std::mutex m_mutex;
            std::shared_ptr<const Publication> m_publication;

            // the window the root represents, which the subclass installs and the detach clears (§10.2)
            std::atomic<HWND> m_window{nullptr};

            // the com reference count, which ui automation and the node providers both hold (§10.2)
            std::atomic<ULONG> m_references{1};
        };

        // one published semantic node, which is the unit assistive technology reads and acts on (§10.2)
        class NodeProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider, public IToggleProvider, public IValueProvider, public ISelectionItemProvider
        {
        public:
            // the node reads one index of one immutable publication, which the bridge owns for as long as it lives (§10.2)
            NodeProvider(std::shared_ptr<const Publication> publication, std::size_t index, IRawElementProviderFragmentRoot* root, HWND window) :
                m_publication(std::move(publication)), m_index(index), m_root(root), m_window(window) {
                // the root outlives every element it handed out, which the extra reference guarantees (§10.2)
                if (m_root != nullptr)
                    m_root->AddRef();
            }

            // the node releases the root reference it holds, which is the only resource the provider owns (§10.2)
            ~NodeProvider() {
                // the root reference is released last, because no fragment may outlive the root it names (§10.2)
                if (m_root != nullptr)
                    m_root->Release();
            }

            // the node is shared through com, so it counts the references ui automation holds (§10.2)
            ULONG STDMETHODCALLTYPE AddRef() override {
                // the count is atomic, because a provider may be reached from a ui automation thread (§10.2)
                return m_references.fetch_add(1) + 1;
            }

            // the node is released by com, so the last reference destroys it (§10.2)
            ULONG STDMETHODCALLTYPE Release() override {
                // the decrement decides the destruction, because com hands no instruction beyond the count (§10.2)
                const ULONG remaining = m_references.fetch_sub(1) - 1;
                if (remaining == 0)
                    delete this;
                return remaining;
            }

            // com asks the node which interfaces it serves, which is the simple, fragment and pattern set (§10.2)
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interface_id, void** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                if (interface_id == __uuidof(IUnknown) || interface_id == __uuidof(IRawElementProviderSimple))
                    *result = static_cast<IRawElementProviderSimple*>(this);
                else if (interface_id == __uuidof(IRawElementProviderFragment))
                    *result = static_cast<IRawElementProviderFragment*>(this);
                else if (interface_id == __uuidof(IInvokeProvider) && node() != nullptr && node()->role() == SemanticRole::Button)
                    *result = static_cast<IInvokeProvider*>(this);
                else if (interface_id == __uuidof(IToggleProvider) && node() != nullptr && node()->role() == SemanticRole::Checkbox)
                    *result = static_cast<IToggleProvider*>(this);
                else if (interface_id == __uuidof(IValueProvider) && node() != nullptr && (node()->role() == SemanticRole::TextField || node()->role() == SemanticRole::NumericStepper || node()->role() == SemanticRole::ComboBox))
                    *result = static_cast<IValueProvider*>(this);
                else if (interface_id == __uuidof(ISelectionItemProvider) && node() != nullptr && (node()->role() == SemanticRole::ListItem || node()->role() == SemanticRole::TabItem || node()->role() == SemanticRole::RadioButton))
                    *result = static_cast<ISelectionItemProvider*>(this);
                else
                    return E_NOINTERFACE;
                // the handed out interface owns one reference, which com expects the caller to release (§10.2)
                AddRef();
                return S_OK;
            }

            // report the options the node provides under, a server side provider reachable from any thread (§10.2)
            HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (options == nullptr)
                    return E_POINTER;
                *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
                return S_OK;
            }

            // hand out the pattern a role supports, which is how ui automation knows what it may ask (§10.2)
            HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern_id, IUnknown** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // a node that was never published supports no pattern, which the empty result states (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return S_OK;
                switch (pattern_id) {
                case UIA_InvokePatternId:
                    // a push control is the only role that invokes, which the pattern mapping fixes (§10.1)
                    if (published->role() == SemanticRole::Button)
                        *result = static_cast<IUnknown*>(static_cast<IInvokeProvider*>(this));
                    break;
                case UIA_TogglePatternId:
                    // a checkable control is the only role that toggles, which the pattern mapping fixes (§10.1)
                    if (published->role() == SemanticRole::Checkbox)
                        *result = static_cast<IUnknown*>(static_cast<IToggleProvider*>(this));
                    break;
                case UIA_ValuePatternId:
                    // an editable field and a stepper carry a value, which the role checks agree on (§10.1)
                    if (published->role() == SemanticRole::TextField || published->role() == SemanticRole::NumericStepper || published->role() == SemanticRole::ComboBox)
                        *result = static_cast<IUnknown*>(static_cast<IValueProvider*>(this));
                    break;
                case UIA_SelectionItemPatternId:
                    // a list row, a tab item and a radio button carry a selection, which the pattern mapping fixes (§10.1)
                    if (published->role() == SemanticRole::ListItem || published->role() == SemanticRole::TabItem || published->role() == SemanticRole::RadioButton)
                        *result = static_cast<IUnknown*>(static_cast<ISelectionItemProvider*>(this));
                    break;
                default:
                    break;
                }
                // a pattern the role serves is handed over with the reference the caller releases (§10.2)
                if (*result != nullptr)
                    AddRef();
                return S_OK;
            }

            // read one property of the node, which is everything a reader announces about it (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property_id, VARIANT* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                VariantInit(result);
                // a node that was never published carries no property, which the empty variant states (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return S_OK;
                switch (property_id) {
                case UIA_NamePropertyId:
                    set_variant_bstr(result, bstr_from_utf8(published->name()));
                    return S_OK;
                case UIA_AutomationIdPropertyId:
                    set_variant_bstr(result, bstr_from_utf8(published->explicit_id()));
                    return S_OK;
                case UIA_ControlTypePropertyId:
                    set_variant_int(result, control_type_for(published->role()));
                    return S_OK;
                case UIA_IsEnabledPropertyId:
                    set_variant_bool(result, !published->disabled());
                    return S_OK;
                case UIA_HasKeyboardFocusPropertyId:
                    set_variant_bool(result, published->focused());
                    return S_OK;
                case UIA_IsKeyboardFocusablePropertyId:
                    set_variant_bool(result, focusable_role(published->role()));
                    return S_OK;
                case UIA_IsControlElementPropertyId:
                case UIA_IsContentElementPropertyId:
                    set_variant_bool(result, true);
                    return S_OK;
                case UIA_IsOffscreenPropertyId:
                case UIA_IsPasswordPropertyId:
                    set_variant_bool(result, false);
                    return S_OK;
                case UIA_ProcessIdPropertyId:
                    set_variant_int(result, static_cast<LONG>(GetCurrentProcessId()));
                    return S_OK;
                case UIA_FrameworkIdPropertyId:
                    set_variant_bstr(result, SysAllocString(L"Albor"));
                    return S_OK;
                case UIA_ValueValuePropertyId:
                    // a text field and a stepper carry a value, which the role branch enforces (§10.1)
                    if (published->role() == SemanticRole::TextField || published->role() == SemanticRole::NumericStepper || published->role() == SemanticRole::ComboBox)
                        set_variant_bstr(result, bstr_from_utf8(published->value()));
                    return S_OK;
                case UIA_ValueIsReadOnlyPropertyId:
                    // the bridge cannot carry an edit, so the value is read only for now (§10.2)
                    if (published->role() == SemanticRole::TextField || published->role() == SemanticRole::NumericStepper || published->role() == SemanticRole::ComboBox)
                        set_variant_bool(result, true);
                    return S_OK;
                case UIA_ToggleToggleStatePropertyId:
                    // the check state belongs to a checkable control, which the role scoped branch agrees with (§10.1)
                    if (published->role() == SemanticRole::Checkbox)
                        set_variant_int(result, static_cast<LONG>(toggle_state_for(published->check())));
                    return S_OK;
                case UIA_SelectionItemIsSelectedPropertyId:
                    // the selected state belongs to a list row, tab item, radio or combo box (§10.1)
                    if (published->role() == SemanticRole::ListItem || published->role() == SemanticRole::TabItem || published->role() == SemanticRole::RadioButton || published->role() == SemanticRole::ComboBox)
                        set_variant_bool(result, published->selected());
                    return S_OK;
                case UIA_ExpandCollapseExpandCollapseStatePropertyId:
                    // a combo box announces whether its popup is open, which the portable state carries (§10.1)
                    if (published->role() == SemanticRole::ComboBox)
                        set_variant_int(result, published->expanded() ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
                    return S_OK;
                case UIA_IsInvokePatternAvailablePropertyId:
                    set_variant_bool(result, published->role() == SemanticRole::Button);
                    return S_OK;
                case UIA_IsTogglePatternAvailablePropertyId:
                    set_variant_bool(result, published->role() == SemanticRole::Checkbox);
                    return S_OK;
                case UIA_IsValuePatternAvailablePropertyId:
                    set_variant_bool(result, published->role() == SemanticRole::TextField || published->role() == SemanticRole::NumericStepper);
                    return S_OK;
                case UIA_IsSelectionItemPatternAvailablePropertyId:
                    set_variant_bool(result, published->role() == SemanticRole::ListItem || published->role() == SemanticRole::TabItem || published->role() == SemanticRole::RadioButton);
                    return S_OK;
                case UIA_BoundingRectanglePropertyId: {
                    // the property carries the four rectangle doubles, which the array helper builds (§10.2)
                    SAFEARRAY* rectangle = rectangle_array(screen_rect(m_window, published->bounds()));
                    if (rectangle == nullptr)
                        return S_OK;
                    result->vt = VT_ARRAY | VT_R8;
                    result->parray = rectangle;
                    return S_OK;
                }
                default:
                    return S_OK;
                }
            }

            // a fragment is served by its window host rather than owning a second host provider (§10.2)
            HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                return S_OK;
            }

            // walk one step of the hierarchy from the node, which is what a reader does (§10.2)
            HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // a node that was never published has no neighbours, which the empty result states (§10.2)
                if (m_publication == nullptr)
                    return S_OK;
                const SemanticTree& tree = m_publication->tree;
                switch (direction) {
                case NavigateDirection_Parent: {
                    // the tree parent is the fragment above a child, which is the hierarchy a reader walks (§10.2)
                    const std::size_t parent = tree.parent_of(m_index);
                    if (parent != SemanticTree::NO_PARENT) {
                        *result = make_node(parent);
                        return S_OK;
                    }
                    // a top level node sits directly under the window root, which is its fragment parent (§10.2)
                    if (m_root != nullptr)
                        m_root->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(result));
                    return S_OK;
                }
                case NavigateDirection_FirstChild:
                case NavigateDirection_LastChild: {
                    // the children are carried in tree order, so the first and last are the list ends (§10.2)
                    const std::vector<std::size_t> children = tree.children_of(m_index);
                    if (children.empty())
                        return S_OK;
                    *result = make_node(direction == NavigateDirection_FirstChild ? children.front() : children.back());
                    return S_OK;
                }
                case NavigateDirection_NextSibling:
                case NavigateDirection_PreviousSibling: {
                    // a sibling is found through the parent's child list, which the flat tree carries (§10.1)
                    const std::size_t parent = tree.parent_of(m_index);
                    if (parent == SemanticTree::NO_PARENT)
                        return S_OK;
                    const std::vector<std::size_t> siblings = tree.children_of(parent);
                    const auto position = std::find(siblings.begin(), siblings.end(), m_index);
                    // a node that is not in its parent's list has no sibling, which the empty result states (§10.2)
                    if (position == siblings.end())
                        return S_OK;
                    // the next sibling is the following entry, while the previous is the one before it (§10.2)
                    if (direction == NavigateDirection_NextSibling) {
                        const auto sibling = std::next(position);
                        // a node at the end of the list has no next sibling, which the empty result states (§10.2)
                        if (sibling == siblings.end())
                            return S_OK;
                        *result = make_node(*sibling);
                        return S_OK;
                    }
                    // a node at the start of the list has no previous sibling, which the empty result states (§10.2)
                    if (position == siblings.begin())
                        return S_OK;
                    *result = make_node(*std::prev(position));
                    return S_OK;
                }
                default:
                    return S_OK;
                }
            }

            // report the runtime id of the node, which is unique within the provider (§10.2)
            HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // the index is offset past the window root's id, so no two fragments share one (§10.2)
                *result = runtime_id_array(static_cast<LONG>(m_index) + 2);
                return *result == nullptr ? E_OUTOFMEMORY : S_OK;
            }

            // report the rectangle the node covers, which is its logical bounds opened to screen pixels (§10.2, AD-27)
            HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // a node that was never published has no rectangle, which the empty result states (§10.2)
                const SemanticNode* published = node();
                *result = published == nullptr ? UiaRect{} : screen_rect(m_window, published->bounds());
                return S_OK;
            }

            // the node embeds no other fragment roots, because the published tree lives in the window root (§10.2)
            HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                return S_OK;
            }

            // move keyboard focus to the node, which is what a reader asks of an interactive control (§10.2)
            HRESULT STDMETHODCALLTYPE SetFocus() override {
                // a node that was never published cannot take focus, which the missing element reports (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                // a role that takes no keyboard input refuses the focus, which the role check states (§10.1)
                if (!focusable_role(published->role()) || m_window == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                ::SetFocus(m_window);
                return S_OK;
            }

            // the node names the window root as the top of its fragment, which is how a reader climbs out (§10.2)
            HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = m_root;
                // a node with no root has nothing to hand out, which the empty result states (§10.2)
                if (m_root != nullptr)
                    m_root->AddRef();
                return S_OK;
            }

            // run the action a push control exposes, which is the invoke pattern contract (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE Invoke() override {
                // a node that was never published has no action to run, which the missing element reports (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                return published->invoke("activate") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
            }

            // run the action a checkable control exposes, which is the toggle pattern contract (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE Toggle() override {
                // a node that was never published has no action to run, which the missing element reports (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                // the application owns the state, so the toggle is the action it registers (§10.2)
                if (published->invoke("toggle"))
                    return S_OK;
                return published->invoke("activate") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
            }

            // read the check state a checkable control announces, which is the toggle pattern contract (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // a node that was never published reports off, which the quiet default states (§10.2)
                const SemanticNode* published = node();
                *result = published == nullptr ? ToggleState_Off : toggle_state_for(published->check());
                return S_OK;
            }

            // hand a new text value to the application, which the value pattern contract asks (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override {
                // the bridge holds a read only copy, so it cannot edit the application's text (§10.2)
                return UIA_E_ELEMENTNOTAVAILABLE;
            }

            // read the text value an editable field announces, which is the value pattern contract (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE get_Value(BSTR* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // a node that was never published carries no text, which the empty value states (§10.2)
                const SemanticNode* published = node();
                *result = published == nullptr ? SysAllocStringLen(nullptr, 0) : bstr_from_utf8(published->value());
                return S_OK;
            }

            // report whether the field refuses edits, which the value pattern contract asks (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // the bridge cannot carry an edit, so every field is read only for now (§10.2)
                *result = TRUE;
                return S_OK;
            }

            // make the row the selection of its list, which the selection item pattern asks (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE Select() override {
                // a row that was never published cannot be selected, which the missing element reports (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                // an already selected row needs no work, which the idempotent operation states (§10.2)
                if (published->selected())
                    return S_OK;
                // a tab item selects by activating the page it names, while a row invokes its own select (§10.1)
                const bool activated = published->role() == SemanticRole::TabItem || published->role() == SemanticRole::RadioButton ? published->invoke("activate") : published->invoke("select");
                return activated ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
            }

            // add the row to its list's selection, which the selection item pattern asks (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }

            // take the row out of its list's selection, which the selection item pattern asks (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
                // a row that was never published cannot be deselected, which the missing element reports (§10.2)
                const SemanticNode* published = node();
                if (published == nullptr)
                    return UIA_E_ELEMENTNOTAVAILABLE;
                // a tab item and a radio button are always selected as one member of their group (§10)
                if (published->role() == SemanticRole::TabItem || published->role() == SemanticRole::RadioButton)
                    return S_OK;
                // an unselected row needs no work, which the idempotent operation states (§10.2)
                if (!published->selected())
                    return S_OK;
                return published->invoke("deselect") ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
            }

            // report whether the row is the selection of its list, which the selection item pattern reads (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                // a row that was never published is not selected, which the quiet default states (§10.2)
                const SemanticNode* published = node();
                *result = published != nullptr && published->selected() ? TRUE : FALSE;
                return S_OK;
            }

            // hand out the list that owns the row's selection, which the selection item pattern reads (§10.1, §10.2)
            HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) override {
                // a missing result cannot be written, which the pointer contract refuses (§10.2)
                if (result == nullptr)
                    return E_POINTER;
                *result = nullptr;
                // a row that was never published has no container, which the empty result states (§10.2)
                if (m_publication == nullptr)
                    return S_OK;
                const std::size_t parent = m_publication->tree.parent_of(m_index);
                // a root row has no container, which the empty result states (§10.2)
                if (parent == SemanticTree::NO_PARENT)
                    return S_OK;
                // the parent fragment is asked for its simple interface, which the two interfaces split (§10.2)
                IRawElementProviderFragment* container = make_node(parent);
                if (container != nullptr) {
                    container->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(result));
                    container->Release();
                }
                return S_OK;
            }

        private:
            // read the node this provider was published for, which is the single source of truth (§10.1)
            [[nodiscard]] const SemanticNode* node() const {
                // a provider with no publication carries no node, which the empty result states (§10.2)
                if (m_publication == nullptr)
                    return nullptr;
                return m_publication->tree.at(m_index);
            }

            // build a fragment for one published node, which transfers the node's first reference to the caller (§10.2)
            [[nodiscard]] IRawElementProviderFragment* make_node(std::size_t index) const {
                // an index past the published tree names no element, which the empty result states (§10.2)
                if (m_publication == nullptr || index >= m_publication->tree.size())
                    return nullptr;
                return new NodeProvider(m_publication, index, m_root, m_window);
            }

            // the publication the node reads, which is an immutable snapshot of one tree (§10.2)
            std::shared_ptr<const Publication> m_publication;

            // the index the node took in the publication, which is how its neighbours are found (§10.2)
            std::size_t m_index = 0;

            // the window root the node climbs to, which outlives the node through its own reference (§10.2)
            IRawElementProviderFragmentRoot* m_root = nullptr;

            // the window the node reports its screen rectangle in, which the bridge owns (§10.2)
            HWND m_window = nullptr;

            // the com reference count, which ui automation holds through every interface it takes (§10.2)
            std::atomic<ULONG> m_references{1};
        };

        // build a fragment for one published node, which transfers the node's first reference to the caller (§10.2)
        IRawElementProviderFragment* RootProvider::make_node(const std::shared_ptr<const Publication>& publication, std::size_t index) const {
            // an index past the published tree names no element, which the empty result states (§10.2)
            if (publication == nullptr || index >= publication->tree.size())
                return nullptr;
            return new NodeProvider(publication, index, const_cast<RootProvider*>(this), m_window.load());
        }

        // the subclass procedure that answers the window's accessibility request (§10.2)
        LRESULT CALLBACK accessibility_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference);

        // the win32 bridge, which owns what it published and the window it publishes into (§10.2)
        class Win32AccessibilityAdapter final : public AccessibilityAdapter
        {
        public:
            // a fresh bridge holds a root provider and publishes nothing, which the portable side expects (§10.2)
            Win32AccessibilityAdapter() :
                m_root(new RootProvider()) {}

            // the bridge lets go of the window and the root, which a closed application needs (§10.2)
            ~Win32AccessibilityAdapter() override {
                // the publication and the subclass are removed before the root is released (§10.2)
                detach_window();
                m_root->Release();
            }

            // attach the bridge to the native window, which is where the subclass answers ui automation (§10.2)
            void attach_window(NativeWindowHandle window) override {
                // the previous window is detached first, because a bridge publishes into one window at a time (§10.2)
                detach_window();
                m_window = reinterpret_cast<HWND>(const_cast<void*>(window.object()));
                m_root->set_window(m_window);
                // a window handle installs the subclass, while a headless publication keeps no hook (§10.2)
                if (m_window != nullptr)
                    SetWindowSubclass(m_window, &accessibility_subclass_proc, subclass_id(), reinterpret_cast<DWORD_PTR>(this));
            }

            // publish a rebuilt semantic tree, which ui automation reads as one immutable snapshot (§10.2)
            void publish(const SemanticTree& tree) override {
                // the tree is copied into a publication, because the caller's tree may go away after this call (§10.2)
                auto publication = std::make_shared<Publication>();
                publication->tree = tree;
                m_root->set_publication(publication);
                // the count is the whole tree, which the portable coverage suite reads (§10.2)
                m_count.store(tree.size());
                // a window that is attached is told its children changed (§10.2)
                if (m_window != nullptr)
                    (void)UiaRaiseStructureChangedEvent(m_root, StructureChangeType_ChildrenInvalidated, nullptr, 0);
            }

            // detach from the window, which takes the hook and the publication away (§10.2)
            void detach_window() override {
                // the subclass is removed before the window is forgotten (§10.2)
                if (m_window != nullptr) {
                    const HWND window = m_window;
                    m_window = nullptr;
                    RemoveWindowSubclass(window, &accessibility_subclass_proc, subclass_id());
                    // the provider map is told the window is gone (§10.2)
                    UiaReturnRawElementProvider(window, 0, 0, nullptr);
                }
                m_root->set_window(nullptr);
                m_root->set_publication(nullptr);
                m_count.store(0);
            }

            // report how many nodes the last publication held, which a coverage test asserts on (§10.2)
            [[nodiscard]] std::size_t published_node_count() const override { return m_count.load(); }

            // hand out the fragment root the native hook answers with, which the bridge owns (§10.2)
            [[nodiscard]] IRawElementProviderFragmentRoot* root_provider() const { return m_root; }

        private:
            // the subclass identifier is the bridge address, which keeps two bridges on one window apart (§10.2)
            [[nodiscard]] UINT_PTR subclass_id() const { return reinterpret_cast<UINT_PTR>(this) + SUBCLASS_ID_OFFSET; }

            // the window the bridge publishes into, which the caller owns (§5.2, §10.2)
            HWND m_window = nullptr;

            // the fragment root every window request reaches, which the bridge owns for its whole life (§10.2)
            RootProvider* m_root = nullptr;

            // how many nodes the last publication held, which a coverage test reads (§10.2)
            std::atomic<std::size_t> m_count{0};
        };

        // the subclass procedure that answers the window's accessibility request, which is the one native hook (§10.2)
        LRESULT CALLBACK accessibility_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
            // the bridge reference is the instance the subclass was installed for, which the install carries (§10.2)
            auto* adapter = reinterpret_cast<Win32AccessibilityAdapter*>(reference);
            if (message == WM_GETOBJECT && static_cast<DWORD>(lparam) == static_cast<DWORD>(UiaRootObjectId)) {
                // the provider is the fragment root the bridge published, which ui automation walks from (§10.2)
                IRawElementProviderFragmentRoot* root = adapter != nullptr ? adapter->root_provider() : nullptr;
                // the simple interface is the shape the window message returns, which the root also implements (§10.2)
                IRawElementProviderSimple* simple = nullptr;
                if (root != nullptr)
                    root->QueryInterface(__uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(&simple));
                if (simple != nullptr) {
                    const LRESULT key = UiaReturnRawElementProvider(window, wparam, lparam, simple);
                    // the extra reference the query interface took is released after the call owns its own (§10.2)
                    simple->Release();
                    return key;
                }
            }
            // the provider map is told the window is going, so ui automation releases what it held for it (§10.2)
            if (message == WM_DESTROY)
                UiaReturnRawElementProvider(window, 0, 0, nullptr);
            // the subclass removes itself at the end of the window, because a dead window keeps no callback chain (§10.2)
            if (message == WM_NCDESTROY)
                RemoveWindowSubclass(window, &accessibility_subclass_proc, id);
            return DefSubclassProc(window, message, wparam, lparam);
        }
    } // namespace

    // hand out the fragment root the win32 bridge publishes, which the platform suite drives directly (§10.2)
    IRawElementProviderFragmentRoot* fragment_root_for(AccessibilityAdapter& adapter) {
        // the bridge is the only adapter this platform builds, so the cast is the one a suite needs (§10.2)
        auto* win32 = dynamic_cast<Win32AccessibilityAdapter*>(&adapter);
        // an adapter of another kind has no win32 fragment root, which the empty result states (§10.2)
        if (win32 == nullptr)
            return nullptr;
        IRawElementProviderFragmentRoot* root = win32->root_provider();
        // the caller owns the reference it is handed, which com expects it to release (§10.2)
        if (root != nullptr)
            root->AddRef();
        return root;
    }

    bool available() {
        // the ui automation provider is part of every windows build this project supports (§10.2)
        return true;
    }

    std::unique_ptr<AccessibilityAdapter> build() {
        // the bridge is handed back by value, which the caller owns for the life of the application (§10.2)
        return std::make_unique<Win32AccessibilityAdapter>();
    }
} // namespace albor::accessibility::platform
