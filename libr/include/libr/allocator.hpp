#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include "error.hpp"


namespace rlib {
    struct AllocatorErrorCategory : ErrorCategory {};
    inline constexpr auto allocatorErrorCategory = AllocatorErrorCategory{};

    inline constexpr auto OutOfMemoryError = Error{-1, &allocatorErrorCategory};

    // No-throw interface for allocators.
    class Allocator {
    public:
        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));

        void deallocate(void *p, std::size_t bytes, std::size_t = alignof(std::max_align_t));

        bool owns(void *p) const;

    private:
        virtual void* do_allocate(std::size_t bytes, std::size_t alignment) = 0;

        virtual void do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) = 0;

        virtual bool do_owns(void *p) const = 0;
    };

    class BumpAllocator : public Allocator {
    public:
        BumpAllocator(void* buffer, std::size_t size);
        
        BumpAllocator(BumpAllocator&& other);

        BumpAllocator(const BumpAllocator&) = delete;
        BumpAllocator& operator= (const BumpAllocator&) = delete;
        
    private:
        std::byte*  buffer;
        std::size_t available;

        virtual void* do_allocate(std::size_t bytes, std::size_t alignment) final;

        virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment ) final;

        virtual bool do_owns(void *p) const final;
    };

    template <typename T>
    concept IsAllocator = std::is_base_of_v<Allocator, T>;

    template<IsAllocator BaseAllocator>
    class RefAllocator : public Allocator {
    public:
        RefAllocator(BaseAllocator& ref)
         : ref(&ref) { }
    private:
        BaseAllocator* ref;

        virtual void* do_allocate(std::size_t bytes, std::size_t alignment) final {
            return ref->allocate(bytes, alignment);
        }

        virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) final {
            ref->deallocate(p, bytes, alignment);
        }

        virtual bool do_owns(void *p) const final {
            return ref->owns(p);
        }
    };  

    template<IsAllocator PrimaryAllocator, IsAllocator SecondaryAllocator>
    class FallbackAllocator : public Allocator {
    public:
        FallbackAllocator(PrimaryAllocator primary, SecondaryAllocator secondary) :
            primary(std::move(primary)),
            secondary(std::move(secondary)) { }

    private:
        PrimaryAllocator primary;
        SecondaryAllocator secondary;

        virtual void* do_allocate(std::size_t bytes, std::size_t alignment) final {
            auto p = primary.allocate(bytes, alignment);
            if (p == nullptr) {
                return secondary.allocate(bytes, alignment);
            }
            return p;
        }

        virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) final {
            if (primary.owns(p)) {
                primary.deallocate(p, bytes, alignment);
            } else {
                secondary.deallocate(p, bytes, alignment);
            }
        }

        virtual bool do_owns(void *p) const final {
            return primary.owns(p) || secondary.owns(p);
        }    
    };

    

    template <typename T, IsAllocator Alloc, class... Args>
    T* constructRaw(Alloc& allocator, Args&&... args) {
        void *storage = allocator.allocate(sizeof(T), alignof(T));
        return ::new (storage) T(std::forward<Args>(args)...);
    }

    template<typename T, IsAllocator Alloc>
    void destruct(T* ptr, Alloc& allocator) {
        ptr->~T();
        allocator.deallocate(ptr, sizeof(T), alignof(T));
    }
}
