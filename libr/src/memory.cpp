#include <memory>

void* memcpy(void* dest, const void* src, std::size_t count ) {
    auto destBuffer = reinterpret_cast<std::byte*>(dest);
    auto sourceBuffer = reinterpret_cast<const std::byte*>(src);
    
    for (auto i = std::size_t(0); i < count; i++) {
        destBuffer[i] = sourceBuffer[i];
    }

    return dest;
}
