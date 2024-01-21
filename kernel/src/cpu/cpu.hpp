#pragma once
#include <cstdint>

namespace Register {

struct CR3 {
    static std::uint64_t read();

    static void flushTLBS(); 
};

}

class Cpu {
public:
    Cpu();

private:
    uint64_t gdt[5];
};
