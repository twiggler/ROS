#include <libr/allocator.hpp>
#include <cstdint>

namespace rlib {

    void *Allocator::allocate(std::size_t bytes, std::size_t alignment)
    {
        return do_allocate(bytes, alignment);
    }

    void Allocator::deallocate(void *p, std::size_t bytes, std::size_t alignment)
    {
        return do_deallocate(p, bytes, alignment);
    }

    bool Allocator::owns(void *p) const
    {
        return do_owns(p);
    }

    BumpAllocator::BumpAllocator(void *buffer, std::size_t size) :
        buffer(reinterpret_cast<std::byte *>(buffer)), available(size)
    {}

    BumpAllocator::BumpAllocator(BumpAllocator &&other) : buffer(other.buffer), available(other.available)
    {
        other.buffer    = nullptr;
        other.available = 0;
    }


    void *BumpAllocator::do_allocate(std::size_t bytes, std::size_t alignment)
    {
        if (available < bytes) {
            return nullptr;
        }

        auto base    = reinterpret_cast<std::uintptr_t>(buffer);
        auto aligned = (base + alignment - 1) & -alignment;
        auto diff    = aligned - base;
        if (diff + bytes > available) {
            return nullptr;
        }

        available -= bytes + diff;
        buffer += bytes + diff;
        return reinterpret_cast<void *>(aligned);
    }

    void BumpAllocator::do_deallocate(void *p, std::size_t bytes, std::size_t alignment)
    {
        // NOOP
    }

    bool BumpAllocator::do_owns(void *p) const
    {
        return p >= buffer && p < buffer + available;
    }

} // namespace rlib
