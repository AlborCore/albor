#include "prototypes/e/src/node.h"

namespace albor_proto_e
{

    // initialise the hook slot to null; non-null only when devrt is linked (AD-02)
    DevExtensionHook UiNode::s_dev_hook = nullptr;

    float TextNode::read_property(uint16_t local_offset) const noexcept {
        switch (local_offset) {
        case kPropFontSize:
            return m_font_size;
        case kPropLineHeight:
            return m_line_height;
        case kPropOpacity:
            return m_opacity;
        case kPropVisible:
            return m_visible;
        default:
            return 0.0f;
        }
    }

    void TextNode::write_property(uint16_t local_offset, float value) noexcept {
        switch (local_offset) {
        case kPropFontSize:
            m_font_size = value;
            break;
        case kPropLineHeight:
            m_line_height = value;
            break;
        case kPropOpacity:
            m_opacity = value;
            break;
        case kPropVisible:
            m_visible = value;
            break;
        default:
            break;
        }
    }

    float ContainerNode::read_property(uint16_t local_offset) const noexcept {
        switch (local_offset) {
        case kPropWidth:
            return m_width;
        case kPropHeight:
            return m_height;
        case kPropPadding:
            return m_padding;
        case kPropGap:
            return m_gap;
        default:
            return 0.0f;
        }
    }

    void ContainerNode::write_property(uint16_t local_offset, float value) noexcept {
        switch (local_offset) {
        case kPropWidth:
            m_width = value;
            break;
        case kPropHeight:
            m_height = value;
            break;
        case kPropPadding:
            m_padding = value;
            break;
        case kPropGap:
            m_gap = value;
            break;
        default:
            break;
        }
    }

} // namespace albor_proto_e
