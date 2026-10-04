#include "Component237.h"

#include <cstdint>

struct Component237::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component236* m_child = nullptr;
};

void Component237::set_color(float v) { m_impl->m_color = v; }
void Component237::set_opacity(float v) { m_impl->m_opacity = v; }
void Component237::set_width(float v) { m_impl->m_width = v; }
void Component237::set_height(float v) { m_impl->m_height = v; }
void Component237::set_child(Component236* child) { m_impl->m_child = child; }
void Component237::on_click() {}
void Component237::on_hover() {}
void Component237::on_focus() {}
