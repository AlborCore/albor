#include "Component145.h"

#include <cstdint>

struct Component145::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component144* m_child = nullptr;
};

void Component145::set_color(float v) { m_impl->m_color = v; }
void Component145::set_opacity(float v) { m_impl->m_opacity = v; }
void Component145::set_width(float v) { m_impl->m_width = v; }
void Component145::set_height(float v) { m_impl->m_height = v; }
void Component145::set_child(Component144* child) { m_impl->m_child = child; }
void Component145::on_click() {}
void Component145::on_hover() {}
void Component145::on_focus() {}
