#include "Component259.h"

#include <cstdint>

struct Component259::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component258* m_child = nullptr;
};

void Component259::set_color(float v) { m_impl->m_color = v; }
void Component259::set_opacity(float v) { m_impl->m_opacity = v; }
void Component259::set_width(float v) { m_impl->m_width = v; }
void Component259::set_height(float v) { m_impl->m_height = v; }
void Component259::set_child(Component258* child) { m_impl->m_child = child; }
void Component259::on_click() {}
void Component259::on_hover() {}
void Component259::on_focus() {}
