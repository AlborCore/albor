#include "Component061.h"

#include <cstdint>

struct Component061::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component060* m_child = nullptr;
};

void Component061::set_color(float v) { m_impl->m_color = v; }
void Component061::set_opacity(float v) { m_impl->m_opacity = v; }
void Component061::set_width(float v) { m_impl->m_width = v; }
void Component061::set_height(float v) { m_impl->m_height = v; }
void Component061::set_child(Component060* child) { m_impl->m_child = child; }
void Component061::on_click() {}
void Component061::on_hover() {}
void Component061::on_focus() {}
