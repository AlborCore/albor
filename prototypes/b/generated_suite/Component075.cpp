#include "Component075.h"

#include <cstdint>

struct Component075::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component074* m_child = nullptr;
};

void Component075::set_color(float v) { m_impl->m_color = v; }
void Component075::set_opacity(float v) { m_impl->m_opacity = v; }
void Component075::set_width(float v) { m_impl->m_width = v; }
void Component075::set_height(float v) { m_impl->m_height = v; }
void Component075::set_child(Component074* child) { m_impl->m_child = child; }
void Component075::on_click() {}
void Component075::on_hover() {}
void Component075::on_focus() {}
