#pragma once

#include <cstdint>

namespace albor_proto_e
{

    // registration hook: exists in Release layout (never ifdef-gated); null unless devrt is linked
    // dev code installs its function pointer here at library load time (AD-02, §2.3)
    using DevExtensionHook = void (*)(void* /*node*/, uint32_t /*node_id*/);

    // base class for all UI nodes; virtual interface is identical in Release and Dev (AD-02, §2.3)
    struct UiNode
    {
        explicit UiNode(uint32_t id) noexcept : m_id(id) {}
        virtual ~UiNode() = default;
        virtual const char* type_name() const noexcept = 0;
        virtual float read_property(uint16_t local_offset) const noexcept = 0;
        virtual void write_property(uint16_t local_offset, float value) noexcept = 0;

        // return the node identity key (AD-11, §18.1)
        [[nodiscard]] uint32_t id() const noexcept { return m_id; }

        // hook slot: always present, null in Release builds (AD-02)
        static DevExtensionHook s_dev_hook;

        // identity field: public so offsetof is well-formed in the layout check binary
        uint32_t m_id;
    };

    // text node with four layout-relevant float properties
    struct TextNode final : UiNode
    {
        using UiNode::UiNode;
        const char* type_name() const noexcept override { return "TextNode"; }
        float read_property(uint16_t local_offset) const noexcept override;
        void write_property(uint16_t local_offset, float value) noexcept override;
        static constexpr uint16_t kPropFontSize = 0;
        static constexpr uint16_t kPropLineHeight = 1;
        static constexpr uint16_t kPropOpacity = 2;
        static constexpr uint16_t kPropVisible = 3;

    private:
        float m_font_size = 16.0f;
        float m_line_height = 1.2f;
        float m_opacity = 1.0f;
        float m_visible = 1.0f;
    };

    // container node with four box-model float properties
    struct ContainerNode final : UiNode
    {
        using UiNode::UiNode;
        const char* type_name() const noexcept override { return "ContainerNode"; }
        float read_property(uint16_t local_offset) const noexcept override;
        void write_property(uint16_t local_offset, float value) noexcept override;
        static constexpr uint16_t kPropWidth = 0;
        static constexpr uint16_t kPropHeight = 1;
        static constexpr uint16_t kPropPadding = 2;
        static constexpr uint16_t kPropGap = 3;

    private:
        float m_width = 100.0f;
        float m_height = 100.0f;
        float m_padding = 0.0f;
        float m_gap = 0.0f;
    };

} // namespace albor_proto_e
