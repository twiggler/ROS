#include <cstdint>
#include <kernel/ipc.hpp>

void sendMessage(
    std::uint64_t      receiverId,
    std::size_t        size = 0,
    std::uint64_t      param1 = 0,
    std::uint64_t      param2 = 0,
    std::uint64_t      param3 = 0,
    std::uint64_t      param4 = 0
) {
    asm volatile(
        "syscall"
        :
        : 
        : "rcx", "r11"
    );

} 


void main()
{
    // syscall();
}
