#pragma once

#include <cstdint>

struct Component219;

struct Component220
{
    Component220();
    ~Component220();
    Component220(const Component220 &) = delete;
    Component220 &operator=(const Component220 &) = delete;
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
    void set_child(Component219* child);
    void on_click();
    void on_hover();
    void on_focus();

private:
    struct Impl;
    Impl* m_impl;
};
