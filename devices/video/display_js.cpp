#include <devices/video/display.h>
#include <loguru.hpp>
#include <emscripten.h>

// Use xxHash in inline mode to avoid having to include a separate .c file.
#define XXH_STATIC_LINKING_ONLY
#define XXH_INLINE_ALL
#include <thirdparty/xxHash/xxhash.h>

class Display::Impl
{
public:
    std::unique_ptr<uint8_t[]> framebuffer;
    int framebuffer_size;
    int framebuffer_pitch;
    XXH64_hash_t last_update_hash;
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
    impl->framebuffer_pitch = width * 4;
    impl->framebuffer_size = height * impl->framebuffer_pitch;
    impl->framebuffer = std::make_unique<uint8_t[]>(impl->framebuffer_size);
    impl->last_update_hash = 0;
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
    uint8_t *framebuffer = impl->framebuffer.get();
    int framebuffer_size = impl->framebuffer_size;
    for (int i = 0; i < framebuffer_size; i += 4) {
        framebuffer[i] = 0x00;
        framebuffer[i + 1] = 0x00;
        framebuffer[i + 2] = 0x00;
        framebuffer[i + 3] = 0xff;
    }
    impl->last_update_hash = XXH3_64bits(framebuffer, framebuffer_size);
    EM_ASM_({ workerApi.blit($0, $1); }, framebuffer, framebuffer_size);
}

void Display::update(std::function<void(uint8_t *dst_buf, int dst_pitch)> convert_fb_cb, bool draw_hw_cursor, int cursor_x, int cursor_y)
{
    uint8_t *framebuffer = impl->framebuffer.get();
    size_t framebuffer_size = impl->framebuffer_size;
    convert_fb_cb(framebuffer, impl->framebuffer_pitch);
    // DingusPPC generates ARGB but in little endian order within a 32-bit word. s
    // It ends up as BGRA in memory, so we need to swap red and blue channels
    // to generate the RGBA that the JS side expects.
    for (size_t i = 0; i < framebuffer_size; i += 4) {
        std::swap(framebuffer[i], framebuffer[i + 2]);
    }

    XXH64_hash_t update_hash = XXH3_64bits(framebuffer, framebuffer_size);

    if (update_hash == impl->last_update_hash) {
        // Screen has not changed, but we still let the JS know so that it can
        // keep track of screen refreshes when deciding how long to idle for.
        EM_ASM({ workerApi.blit(0, 0); });
        return;
    }

    impl->last_update_hash = update_hash;

    EM_ASM_({ workerApi.blit($0, $1); }, framebuffer, framebuffer_size);
}

void Display::setup_hw_cursor(std::function<void(uint8_t *dst_buf, int dst_pitch)> draw_hw_cursor, int cursor_width, int cursor_height)
{
    LOG_F(INFO, "Display::setup_hw_cursor()");
}
