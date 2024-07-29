#pragma once

#include "allocator.hpp"

namespace rlib {

    class ScalarDeleter {
    public:
        ScalarDeleter() = default;

        ScalarDeleter(Allocator& alloc, std::size_t size, std::size_t alignment) :
            alloc(&alloc), size(size), alignment(alignment)
        {}

        void operator()(void* p) { alloc->deallocate(p, size, alignment); }

    private:
        Allocator*  alloc     = nullptr;
        std::size_t size      = 0;
        std::size_t alignment = 0;
    };

    template<class T>
    class ArrayDeleter {
    public:
        ArrayDeleter() = default;

        explicit ArrayDeleter(Allocator& alloc) : alloc(&alloc) {}

        void operator()(void* p, std::size_t extent) { alloc->deallocate(p, extent * sizeof(T), alignof(T)); }

    private:
        Allocator* alloc = nullptr;
    };

    template<class T>
    class OwningPointer {
    public:
        using Element = std::remove_extent_t<T>;

        constexpr OwningPointer();

        constexpr OwningPointer(std::nullptr_t);

        OwningPointer(T* pointer, Allocator& allocator)
        requires(!std::is_array_v<T>);

        OwningPointer(Element* pointer, Allocator& allocator, std::size_t extent)
        requires std::is_array_v<T>;

        OwningPointer(const OwningPointer&) = delete;

        OwningPointer(OwningPointer&& other);

        template<class U>
        OwningPointer(OwningPointer<U>&& other)
        requires(std::convertible_to<U*, T*>);

        OwningPointer& operator=(OwningPointer&& other);

        OwningPointer& operator=(const OwningPointer&) = delete;

        typename std::add_lvalue_reference<T>::type operator*() const
        requires(!std::is_array_v<T>);

        T* operator->() const
        requires(!std::is_array_v<T>);

        Element& operator[](std::size_t index)
        requires std::is_array_v<T>;

        const Element& operator[](std::size_t index) const
        requires std::is_array_v<T>;

        constexpr void clear();

        Element* get() const;

        std::size_t size() const
        requires(std::is_array_v<T>);

        auto begin() const
        requires(std::is_array_v<T>);

        auto end() const
        requires(std::is_array_v<T>);

        constexpr ~OwningPointer();

    private:
        template<class E>
        friend class OwningPointer;

        struct Empty {
            constexpr Empty(auto&&...) {};
        };
        using Deleter = std::conditional_t<std::is_array_v<T>, ArrayDeleter<Element>, ScalarDeleter>;
        using Extent  = std::conditional_t<std::is_array_v<T>, std::size_t, Empty>;

        Element*                     pointer;
        Deleter                      deleter;
        [[no_unique_address]] Extent extent;
    };

    template<class T>
    bool operator==(const OwningPointer<T>& pointer, std::nullptr_t);

    template<class U, IsAllocator Alloc = Allocator, class... Args>
    OwningPointer<U> construct(Alloc& alloc, Args&&... args)
    requires(!std::is_array_v<U>);

    template<class U, IsAllocator Alloc = Allocator, class... Args>
    OwningPointer<U> construct(Alloc& alloc, std::size_t size)
    requires std::is_array_v<U>;

    /* IMPLEMENTATION */

    template<class T>
    constexpr OwningPointer<T>::OwningPointer() : pointer(nullptr), deleter{}, extent{}
    {}

    template<class T>
    constexpr OwningPointer<T>::OwningPointer(std::nullptr_t) : pointer(nullptr), deleter{}, extent{}
    {}

    template<class T>
    OwningPointer<T>::OwningPointer(T* pointer, Allocator& allocator)
    requires(!std::is_array_v<T>)
        : pointer(pointer), deleter(allocator, sizeof(T), alignof(T)), extent{}
    {}

    template<class T>
    OwningPointer<T>::OwningPointer(Element* pointer, Allocator& allocator, std::size_t extent)
    requires std::is_array_v<T>
        : pointer(pointer), deleter(allocator), extent(extent)
    {}

    template<class T>
    OwningPointer<T>::OwningPointer(OwningPointer&& other) :
        pointer(other.pointer), deleter(other.deleter), extent(other.extent)
    {
        other.pointer = nullptr;
        other.deleter = {};
        other.extent  = {};
    }

    template<class T>
    template<class U>
    OwningPointer<T>::OwningPointer(OwningPointer<U>&& other)
    requires(std::convertible_to<U*, T*>)
        : pointer(other.pointer), deleter(other.deleter), extent(other.extent)
    {
        other.pointer = nullptr;
        other.deleter = {};
        other.extent  = {};
    }

    template<class T>
    OwningPointer<T>& OwningPointer<T>::operator=(OwningPointer<T>&& other)
    {
        clear();

        pointer       = other.pointer;
        deleter       = other.deleter;
        extent        = other.extent;
        other.pointer = nullptr;
        other.deleter = {};
        other.extent  = {};

        return *this;
    }

    template<class T>
    typename std::add_lvalue_reference<T>::type OwningPointer<T>::operator*() const
    requires(!std::is_array_v<T>)
    {
        return *pointer;
    }

    template<class T>
    T* OwningPointer<T>::operator->() const
    requires(!std::is_array_v<T>)
    {
        return pointer;
    }

    template<class T>
    OwningPointer<T>::Element& OwningPointer<T>::operator[](std::size_t index)
    requires std::is_array_v<T>
    {
        return pointer[index];
    }

    template<class T>
    const OwningPointer<T>::Element& OwningPointer<T>::operator[](std::size_t index) const
    requires std::is_array_v<T>
    {
        return pointer[index];
    }

    template<class T>
    auto OwningPointer<T>::begin() const
    requires(std::is_array_v<T>)
    {
        return pointer;
    }

    template<class T>
    auto OwningPointer<T>::end() const
    requires(std::is_array_v<T>)
    {
        return pointer + extent;
    }

    template<class T>
    constexpr OwningPointer<T>::~OwningPointer()
    {
        clear();
    }

    template<class T>
    constexpr void OwningPointer<T>::clear()
    {
        if (pointer == nullptr) {
            return;
        }

        if constexpr (!std::is_array_v<T>) {
            pointer->~T();
            deleter(pointer);
        } else {
            using Element = std::remove_extent_t<T>;
            for (auto i = std::size_t(0); i < extent; i++) {
                pointer[i].~Element();
            }
            deleter(pointer, extent);
        }
    }

    template<class T>
    OwningPointer<T>::Element* OwningPointer<T>::get() const
    {
        return pointer;
    }

    template<class T>
    std::size_t OwningPointer<T>::size() const
    requires(std::is_array_v<T>)
    {
        return extent;
    }

    template<class U, class... Args>
    OwningPointer<U> construct(Allocator& alloc, Args&&... args)
    requires(!std::is_array_v<U>)
    {
        auto p = alloc.allocate(sizeof(U), alignof(U));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new (p) U(std::forward<Args>(args)...);
        return OwningPointer(naked, alloc);
    }

    template<class U, class... Args>
    OwningPointer<U> construct(Allocator& alloc, std::size_t size)
    requires std::is_array_v<U>
    {
        using Element = std::remove_extent_t<U>;
        auto p        = alloc.allocate(sizeof(Element) * size, alignof(Element));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new (p) Element[size]();
        return OwningPointer<U>(naked, alloc, size);
    }

    template<class T>
    bool operator==(const OwningPointer<T>& pointer, std::nullptr_t)
    {
        return pointer.get() == nullptr;
    }
} // namespace rlib
