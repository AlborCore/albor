#pragma once

#include <cstdint>

struct Component142;

struct Component143
{
    Component143();
    ~Component143();
    Component143(const Component143 &) = delete;
    Component143 &operator=(const Component143 &) = delete;
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
    void set_child(Component142* child);
    void on_click();
    void on_hover();
    void on_focus();

private:
    struct Impl;
    Impl* m_impl;
};
