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
    std::unique_ptr<uint8_t[]> frame_buffer;
    int frame_buffer_size;
    int frame_buffer_pitch;
    XXH64_hash_t last_update_hash;

    std::unique_ptr<uint8_t[]> cursor_buffer;
    int cursor_width;
    int cursor_height;
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
    bool is_initialization = impl->frame_buffer.get() == nullptr;
    impl->frame_buffer_pitch = width * 4;
    impl->frame_buffer_size = height * impl->frame_buffer_pitch;
    impl->frame_buffer = std::make_unique<uint8_t[]>(impl->frame_buffer_size);
    impl->last_update_hash = 0;
    EM_ASM_({ workerApi.didOpenVideo($0, $1); }, width, height);
    return is_initialization;
}

void Display::handle_events(const WindowEvent& wnd_event)
{
    // No-op in JS
}

void Display::blank()
{
    // Replace contents with opaque black.
    uint8_t *frame_buffer = impl->frame_buffer.get();
    int frame_buffer_size = impl->frame_buffer_size;
    for (int i = 0; i < frame_buffer_size; i += 4) {
        frame_buffer[i] = 0x00;
        frame_buffer[i + 1] = 0x00;
        frame_buffer[i + 2] = 0x00;
        frame_buffer[i + 3] = 0xff;
    }
    impl->last_update_hash = XXH3_64bits(frame_buffer, frame_buffer_size);
    EM_ASM_({ workerApi.blit($0, $1); }, frame_buffer, frame_buffer_size);
}

void Display::update(std::function<void(uint8_t *dst_buf, int dst_pitch)> convert_fb_cb, bool draw_hw_cursor, int cursor_x, int cursor_y)
{
    uint8_t *frame_buffer = impl->frame_buffer.get();
    size_t frame_buffer_size = impl->frame_buffer_size;
    convert_fb_cb(frame_buffer, impl->frame_buffer_pitch);
    if (draw_hw_cursor) {
        uint8_t *cursor_buffer = impl->cursor_buffer.get();
        int cursor_width = impl->cursor_width;
        int cursor_height = impl->cursor_height;
        int cursor_pitch = cursor_width * 4;
        int base_dst_offset = cursor_y * impl->frame_buffer_pitch + cursor_x * 4;
        for (int y = 0; y < cursor_height; y++) {
            for (int x = 0; x < cursor_width; x++) {
                int src_offset = y * cursor_pitch + x * 4;
                uint8_t alpha = cursor_buffer[src_offset + 3];
                if (alpha == 0) {
                    continue;
                }
                int dst_offset = base_dst_offset + y * impl->frame_buffer_pitch + x * 4;
                if (alpha == 0xff) {
                    frame_buffer[dst_offset] = cursor_buffer[src_offset];
                    frame_buffer[dst_offset + 1] = cursor_buffer[src_offset + 1];
                    frame_buffer[dst_offset + 2] = cursor_buffer[src_offset + 2];
                } else {
                    frame_buffer[dst_offset] = (frame_buffer[dst_offset] * (0xff - alpha) + cursor_buffer[src_offset] * alpha) / 0xff;
                    frame_buffer[dst_offset + 1] = (frame_buffer[dst_offset + 1] * (0xff - alpha) + cursor_buffer[src_offset + 1] * alpha) / 0xff;
                    frame_buffer[dst_offset + 2] = (frame_buffer[dst_offset + 2] * (0xff - alpha) + cursor_buffer[src_offset + 2] * alpha) / 0xff;
                }
            }
        }
    }

    // DingusPPC generates ARGB but in little endian order within a 32-bit word.
    // It ends up as BGRA in memory, so we need to swap red and blue channels
    // to generate the RGBA that the JS side expects.
    for (size_t i = 0; i < frame_buffer_size; i += 4) {
        std::swap(frame_buffer[i], frame_buffer[i + 2]);
    }

    XXH64_hash_t update_hash = XXH3_64bits(frame_buffer, frame_buffer_size);

    if (update_hash == impl->last_update_hash) {
        // Screen has not changed, but we still let the JS know so that it can
        // keep track of screen refreshes when deciding how long to idle for.
        EM_ASM({ workerApi.blit(0, 0); });
        return;
    }

    impl->last_update_hash = update_hash;

    EM_ASM_({ workerApi.blit($0, $1); }, frame_buffer, frame_buffer_size);
}

void Display::setup_hw_cursor(std::function<void(uint8_t *dst_buf, int dst_pitch)> draw_hw_cursor, int cursor_width, int cursor_height)
{
    int cursor_pitch = cursor_width * 4;
    if (cursor_width != impl->cursor_width || cursor_height != impl->cursor_height) {
        impl->cursor_width = cursor_width;
        impl->cursor_height = cursor_height;
        impl->cursor_buffer = std::make_unique<uint8_t[]>(cursor_pitch * cursor_height);
    }
    draw_hw_cursor(impl->cursor_buffer.get(), cursor_pitch);
}
