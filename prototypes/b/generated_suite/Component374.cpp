#include "Component374.h"

#include <cstdint>

struct Component374::Impl
{
    uint32_t m_base_offset = 0;
    float m_color = 0;
    float m_opacity = 1;
    float m_width = 0;
    float m_height = 0;
    Component373* m_child = nullptr;
};

Component374::Component374() :
    m_impl(new Impl) {}
Component374::~Component374() { delete m_impl; }

float Component374::color() const { return m_impl->m_color; }
float Component374::width() const { return m_impl->m_width; }
void Component374::set_color(float v) { m_impl->m_color = v; }
void Component374::set_opacity(float v) { m_impl->m_opacity = v; }
void Component374::set_width(float v) { m_impl->m_width = v; }
void Component374::set_height(float v) { m_impl->m_height = v; }
void Component374::set_child(Component373* child) { m_impl->m_child = child; }
void Component374::on_click() {}
void Component374::on_hover() {}
void Component374::on_focus() {}
