#include <devices/video/display.h>
#include <loguru.hpp>

class Display::Impl {
public:
};

Display::Display(): impl(std::make_unique<Impl>()) {
    LOG_F(INFO, "Display::Display()");
}

Display::~Display() {
    LOG_F(INFO, "Display::!Display()");
}

bool Display::configure(int width, int height) {
    LOG_F(INFO, "Display::configure(%d, %d)", width, height);
    return true;
}

void Display::handle_events(const WindowEvent& wnd_event) {
    LOG_F(INFO, "Display::handle_events()");
}

void Display::blank() {
    LOG_F(INFO, "Display::blank()");
}

void Display::update(std::function<void(uint8_t *dst_buf, int dst_pitch)> convert_fb_cb, bool draw_hw_cursor, int cursor_x, int cursor_y) {
    LOG_F(INFO, "Display::update()");
}

void Display::setup_hw_cursor(std::function<void(uint8_t *dst_buf, int dst_pitch)> draw_hw_cursor, int cursor_width, int cursor_height) {
    LOG_F(INFO, "Display::setup_hw_cursor()");
}
