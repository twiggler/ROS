#pragma once
#include <cstddef>
#include <new>
#include <utility>


namespace Memory {
    
    // No-throw interface for allocators.
    class Allocator {
    public:
        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));

        void deallocate(void *p, std::size_t bytes, std::size_t = alignof(std::max_align_t));

        template<class U, class... Args> U* construct(Args&&... args);

    private:
        virtual void* do_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) = 0;

        virtual void do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) = 0;
    };

    class BumpAllocator : public Allocator {
    public:
        BumpAllocator(void* buffer, std::size_t size);

        BumpAllocator(const BumpAllocator&) = delete;
        BumpAllocator& operator= (const BumpAllocator&) = delete;
        
    private:
        void*       buffer;
        std::size_t available;

        virtual void* do_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) final;

        virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment ) final;
    };

    template<class U, class... Args> U* Allocator::construct(Args&&... args) {
        auto p = allocate(sizeof(U), alignof(U));
        if (p == nullptr) {
            return nullptr;
        }

        return ::new(p) U(std::forward<Args>(args)...);
    }
}
