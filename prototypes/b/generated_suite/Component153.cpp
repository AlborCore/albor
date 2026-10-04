#include "Component153.h"

#include <cstdint>

struct Component153::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component152* m_child = nullptr;
};

void Component153::set_color(float v) { m_impl->m_color = v; }
void Component153::set_opacity(float v) { m_impl->m_opacity = v; }
void Component153::set_width(float v) { m_impl->m_width = v; }
void Component153::set_height(float v) { m_impl->m_height = v; }
void Component153::set_child(Component152* child) { m_impl->m_child = child; }
void Component153::on_click() {}
void Component153::on_hover() {}
void Component153::on_focus() {}
