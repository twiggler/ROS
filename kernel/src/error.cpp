#include <kernel/error.hpp>

static FrameBufferInfo fb;

extern char _binary_font_font_psf_start;

struct psf2_t {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t headersize;
    std::uint32_t flags;
    std::uint32_t numglyph;
    std::uint32_t bytesperglyph;
    std::uint32_t height;
    std::uint32_t width;
} __attribute__((packed)); 


void panic(const char* message) {
    psf2_t *font = reinterpret_cast<psf2_t*>(&_binary_font_font_psf_start);
    uint32_t kx = 0;
    auto bpl = (font->width + 7) / 8;
    
    while (*message) {
        auto mc = *message > 0 && static_cast<unsigned char>(*message) < font->numglyph ? *message : 0;
        auto glyph = &_binary_font_font_psf_start + font->headersize + font->bytesperglyph * mc;
           
        auto offs = kx * (font->width + 1);
        for(std::uint32_t y = 0; y < font->height; y++) {
            auto line = offs;
            for(std::uint32_t x = 0; x < font->width; x++) {
                auto glyphBit = static_cast<unsigned char>(*(glyph + x / 8)) & (0x80 >> (x % 8));
                *(fb.base + line) = glyphBit ? 0xffffff : 0;
                line++;
            }
            *(fb.base + line) = 0; 
          
            glyph += bpl;
            offs += fb.scanline / 4;
        }
        message++;
        kx++;
    }

    asm (
        "cli;"
        "hlt;"
    );
}

void initializePanicHandler(FrameBufferInfo frameBufferInfo) {
    fb = frameBufferInfo;
}
