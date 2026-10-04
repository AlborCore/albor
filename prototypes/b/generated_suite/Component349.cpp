#include "Component349.h"

#include <cstdint>

struct Component349::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component348* m_child = nullptr;
};

void Component349::set_color(float v) { m_impl->m_color = v; }
void Component349::set_opacity(float v) { m_impl->m_opacity = v; }
void Component349::set_width(float v) { m_impl->m_width = v; }
void Component349::set_height(float v) { m_impl->m_height = v; }
void Component349::set_child(Component348* child) { m_impl->m_child = child; }
void Component349::on_click() {}
void Component349::on_hover() {}
void Component349::on_focus() {}
