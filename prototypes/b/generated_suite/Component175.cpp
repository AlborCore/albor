#include "Component175.h"

#include <cstdint>

struct Component175::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component174* m_child = nullptr;
};

void Component175::set_color(float v) { m_impl->m_color = v; }
void Component175::set_opacity(float v) { m_impl->m_opacity = v; }
void Component175::set_width(float v) { m_impl->m_width = v; }
void Component175::set_height(float v) { m_impl->m_height = v; }
void Component175::set_child(Component174* child) { m_impl->m_child = child; }
void Component175::on_click() {}
void Component175::on_hover() {}
void Component175::on_focus() {}
