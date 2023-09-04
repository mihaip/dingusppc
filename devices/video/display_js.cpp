#include <devices/video/display.h>
#include <loguru.hpp>
#include <emscripten.h>

class Display::Impl
{
public:
    std::unique_ptr<uint8_t[]> browser_framebuffer;
    int browser_framebuffer_size;
    int browser_framebuffer_pitch;
};

Display::Display(): impl(std::make_unique<Impl>())
{
    // No-op in JS
}

Display::~Display() {
    // No-op in JS
}

bool Display::configure(int width, int height)
{
    impl->browser_framebuffer_pitch = width * 4;
    impl->browser_framebuffer_size = height * impl->browser_framebuffer_pitch;
    impl->browser_framebuffer = std::make_unique<uint8_t[]>(impl->browser_framebuffer_size);
    EM_ASM_({ workerApi.didOpenVideo($0, $1); }, width, height);
    return true;
}

void Display::handle_events(const WindowEvent& wnd_event)
{
    // No-op in JS
}

void Display::blank()
{
    // Replace contents with opaque black.
    uint8_t *browser_framebuffer = impl->browser_framebuffer.get();
    int browser_framebuffer_size = impl->browser_framebuffer_size;
    for (int i = 0; i < browser_framebuffer_size; i += 4) {
        browser_framebuffer[i] = 0x00;
        browser_framebuffer[i + 1] = 0x00;
        browser_framebuffer[i + 2] = 0x00;
        browser_framebuffer[i + 3] = 0xff;
    }
    EM_ASM_({ workerApi.blit($0, $1); }, browser_framebuffer, browser_framebuffer_size);
}

void Display::update(std::function<void(uint8_t *dst_buf, int dst_pitch)> convert_fb_cb, bool draw_hw_cursor, int cursor_x, int cursor_y)
{
    convert_fb_cb(impl->browser_framebuffer.get(), impl->browser_framebuffer_pitch);
    // TODO: has contents and avoid sending framebuffer if unchanged
    EM_ASM_({ workerApi.blit($0, $1); }, impl->browser_framebuffer.get(), impl->browser_framebuffer_size);
}

void Display::setup_hw_cursor(std::function<void(uint8_t *dst_buf, int dst_pitch)> draw_hw_cursor, int cursor_width, int cursor_height)
{
    LOG_F(INFO, "Display::setup_hw_cursor()");
}
