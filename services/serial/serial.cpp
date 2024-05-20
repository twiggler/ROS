#include <cstdint>

void main(std::uint32_t* framebuffer) {
    *framebuffer = 0xff0000; // Set the top left pixel to red.

    while (true) { /* We do not have kernel call to terminate yet */ };
}
