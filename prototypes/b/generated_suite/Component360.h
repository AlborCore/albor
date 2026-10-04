#pragma once

#include <cstdint>

struct Component359;

struct Component360
{
    Component360();
    ~Component360();
    Component360(const Component360 &) = delete;
    Component360 &operator=(const Component360 &) = delete;
    static constexpr uint16_t kPropColor = 0;
    static constexpr uint16_t kPropOpacity = 1;
    static constexpr uint16_t kPropWidth = 2;
    static constexpr uint16_t kPropHeight = 3;
    float color() const;
    float width() const;
    void set_color(float v);
    void set_opacity(float v);
    void set_width(float v);
    void set_height(float v);
    void set_child(Component359* child);
    void on_click();
    void on_hover();
    void on_focus();

private:
    struct Impl;
    Impl* m_impl;
};
