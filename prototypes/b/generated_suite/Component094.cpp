#include "Component094.h"

#include <cstdint>

struct Component094::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component093* m_child = nullptr;
};

void Component094::set_color(float v) { m_impl->m_color = v; }
void Component094::set_opacity(float v) { m_impl->m_opacity = v; }
void Component094::set_width(float v) { m_impl->m_width = v; }
void Component094::set_height(float v) { m_impl->m_height = v; }
void Component094::set_child(Component093* child) { m_impl->m_child = child; }
void Component094::on_click() {}
void Component094::on_hover() {}
void Component094::on_focus() {}
