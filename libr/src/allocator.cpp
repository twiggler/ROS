#include <libr/allocator.hpp>
#include <memory>

namespace rlib { 

void *Allocator::allocate(std::size_t bytes, std::size_t alignment) {
    return do_allocate(bytes, alignment);
}

void Allocator::deallocate(void *p, std::size_t bytes, std::size_t alignment) {
    return do_deallocate(p, bytes, alignment);
}

BumpAllocator::BumpAllocator(void* buffer, std::size_t size) :
    buffer(buffer), available(size) { }

BumpAllocator::BumpAllocator(BumpAllocator&& other) :
    buffer(other.buffer),
    available(other.available)
{
    other.buffer = nullptr;
    other.available = 0;
}


void *BumpAllocator::do_allocate(std::size_t bytes, std::size_t alignment) {
    auto p = std::align(alignment, bytes, buffer, available);
    if (p == nullptr) {
        return nullptr;
    }

    buffer = reinterpret_cast<char*>(buffer) + bytes;
    available -= bytes;
    return p;
}

void BumpAllocator::do_deallocate(void *p, std::size_t bytes, std::size_t alignment) {
    // NOOP
}

}
