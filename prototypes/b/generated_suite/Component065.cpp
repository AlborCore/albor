#include "Component065.h"

#include <cstdint>

struct Component065::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component064* m_child = nullptr;
};

void Component065::set_color(float v) { m_impl->m_color = v; }
void Component065::set_opacity(float v) { m_impl->m_opacity = v; }
void Component065::set_width(float v) { m_impl->m_width = v; }
void Component065::set_height(float v) { m_impl->m_height = v; }
void Component065::set_child(Component064* child) { m_impl->m_child = child; }
void Component065::on_click() {}
void Component065::on_hover() {}
void Component065::on_focus() {}
