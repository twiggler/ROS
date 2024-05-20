#pragma once
#include "allocator.hpp"

namespace rlib {
    
    template<class T, IsAllocator Alloc = Allocator>
    class OwningPointer  {
    public:
        using Element = std::remove_extent_t<T>;
        
        OwningPointer();

        OwningPointer(std::nullptr_t);
        
        OwningPointer(T* pointer, Alloc& alloc) requires (!std::is_array_v<T>);

        OwningPointer(T, Alloc& alloc, std::size_t extent) requires std::is_array_v<T>;

        OwningPointer(const OwningPointer&) = delete;

        OwningPointer(OwningPointer&& other);

        template<IsAllocator E>
        OwningPointer(OwningPointer<T, E>&& other) requires (std::convertible_to<E*, Alloc*>);

        OwningPointer& operator=(OwningPointer&& other);

        OwningPointer& operator=(const OwningPointer& ) = delete;

        typename std::add_lvalue_reference<T>::type operator*() const requires (!std::is_array_v<T>);

        T* operator->() const requires (!std::is_array_v<T>);

        Element& operator[](std::size_t index) requires std::is_array_v<T>;

        const Element& operator[](std::size_t index) const requires std::is_array_v<T>;

        void clear();

        Element* get() const;

        std::size_t size() const requires(std::is_array_v<T>);

        auto begin() const requires(std::is_array_v<T>);

        auto end() const requires(std::is_array_v<T>);
        
        ~OwningPointer();

    private:
        template<class E, IsAllocator U> friend class OwningPointer; 

        struct empty {};
        using extent_t = std::conditional_t<std::is_array_v<T>, std::size_t, empty>;

        Element* pointer;
        Alloc* alloc;
        [[no_unique_address]] extent_t extent;
    };

    template<class T, IsAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t);

    template<class U, IsAllocator Alloc = Allocator, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, Args&&... args) requires (!std::is_array_v<U>);

    template<class U, IsAllocator Alloc = Allocator, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, std::size_t size) requires std::is_array_v<U>;

/* IMPLEMENTATION */

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer() :
        pointer(nullptr), alloc(nullptr), extent{} {}

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(std::nullptr_t) :
        pointer(nullptr), alloc(nullptr), extent{} {}

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(T* pointer, Alloc& alloc) requires (!std::is_array_v<T>) :
        pointer(pointer), alloc(&alloc), extent{} {}

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(T pointer, Alloc& alloc, std::size_t extent) requires std::is_array_v<T> :
        pointer(pointer), alloc(&alloc), extent(extent) {}

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(OwningPointer&& other) :
        pointer(other.pointer),
        alloc(other.alloc),
        extent(other.extent)
    {   
        other.pointer = nullptr;
        other.alloc = nullptr;
        other.extent = {};
    }

    template<class T, IsAllocator Alloc>
    template<IsAllocator E>
    OwningPointer<T, Alloc>::OwningPointer(OwningPointer<T, E>&& other) requires (std::convertible_to<E*, Alloc*>) :
        pointer(other.pointer),
        alloc(other.alloc),
        extent(other.extent)
    {   
        other.pointer = nullptr;
        other.alloc = nullptr;
        other.extent = {};
    }

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>& OwningPointer<T, Alloc>::operator=(OwningPointer<T, Alloc>&& other) {
        clear();
        
        pointer = other.pointer;
        alloc = other.alloc;
        extent = other.extent;
        other.pointer = nullptr;
        other.alloc = nullptr;
        other.extent = {};

        return *this;
    }

    template<class T, IsAllocator Alloc>
    typename std::add_lvalue_reference<T>::type OwningPointer<T, Alloc>::operator*() const requires (!std::is_array_v<T>) {
        return *pointer;
    }

    template<class T, IsAllocator Alloc>
    T* OwningPointer<T, Alloc>::operator->() const requires (!std::is_array_v<T>) {
        return pointer;
    }

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::Element& OwningPointer<T, Alloc>::operator[](std::size_t index) requires std::is_array_v<T> {
        return pointer[index];
    }

    template<class T, IsAllocator Alloc>
    const OwningPointer<T, Alloc>::Element& OwningPointer<T, Alloc>::operator[](std::size_t index) const requires std::is_array_v<T> {
        return pointer[index];
    }

    template<class T, IsAllocator Alloc>
    auto OwningPointer<T, Alloc>::begin() const requires(std::is_array_v<T>) {
        return pointer;
    }

    template<class T, IsAllocator Alloc>
    auto OwningPointer<T, Alloc>::end() const requires(std::is_array_v<T>) {
        return pointer + extent;
    }

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::~OwningPointer() {
        clear();
    }

    template<class T, IsAllocator Alloc>
    void OwningPointer<T, Alloc>::clear() {
        if (pointer == nullptr) {
            return;
        }

        if constexpr (!std::is_array_v<T>) {
            pointer->~T();
            alloc->deallocate(pointer, sizeof(T), alignof(T));
        } else {
            using Element = std::remove_extent_t<T>;
            for (auto i = std::size_t(0); i < extent; i++) {
                pointer[i].~Element(); 
            }
            alloc->deallocate(pointer, sizeof(Element) * extent, alignof(Element));
        }
    }

    template<class T, IsAllocator Alloc>
    OwningPointer<T, Alloc>::Element* OwningPointer<T, Alloc>::get() const {
        return pointer; 
    }

    template<class T, IsAllocator Alloc>
    std::size_t OwningPointer<T, Alloc>::size() const requires(std::is_array_v<T>) {
        return extent;
    }

    template<class U, IsAllocator Alloc, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, Args&&... args) requires (!std::is_array_v<U>) {
        auto p = alloc.allocate(sizeof(U), alignof(U));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new(p) U(std::forward<Args>(args)...);
        return OwningPointer(naked, alloc);
    }

    template<class U, IsAllocator Alloc, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, std::size_t size) requires std::is_array_v<U> {
        using Element = std::remove_extent_t<U>;
        auto p = alloc.allocate(sizeof(Element) * size, alignof(Element));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new(p) Element[size]();
        return OwningPointer<U, Alloc>(naked, alloc, size);
    }

    template<class T, IsAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t) {
        return pointer.get() == nullptr;
    }
}
