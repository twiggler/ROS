#pragma once

#include <cstdint>

struct FrameBufferInfo {
    std::uint32_t*  base;     
    std::uint32_t   size;
    std::uint32_t   width;
    std::uint32_t   height;
    std::uint32_t   scanline;
};

void initializePanicHandler(FrameBufferInfo frameBufferInfo);

void panic(const char* message);
