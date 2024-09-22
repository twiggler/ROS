#pragma once

#include <cstdint>

struct Message {
    static constexpr auto MaxPayloadSize = 128;

    std::uint64_t senderId;
    std::uint64_t receiverId;
    std::uint64_t param1;
    std::uint64_t param2;
    std::uint64_t param3;
    std::uint64_t param4;
    std::uint64_t size;
    char          data[MaxPayloadSize];
};
