#pragma once
#include "allocator.hpp"

namespace rlib {
    
    template<class T, DerivedFromAllocator Alloc = Allocator>
    class OwningPointer {
    public:
        using Pointer = std::conditional_t<std::is_array_v<T>, std::remove_extent_t<T>*, T*>;
        
        OwningPointer();

        OwningPointer(std::nullptr_t);
        
        OwningPointer(T* pointer, Alloc& alloc) requires (!std::is_array_v<T>);

        OwningPointer(T, Alloc& alloc, std::size_t extent) requires std::is_array_v<T>;

        OwningPointer(const OwningPointer&) = delete;

        OwningPointer(OwningPointer&& other);

        OwningPointer& operator=(OwningPointer&& other);

        OwningPointer& operator=(const OwningPointer& ) = delete;

        typename std::add_lvalue_reference<T>::type operator*() const requires (!std::is_array_v<T>);

        T* operator->() const requires (!std::is_array_v<T>);

        void clear();

        Pointer get() const;
        
        ~OwningPointer();

    private:
        struct empty {};
        using extent_t = std::conditional_t<std::is_array_v<T>, std::size_t, empty>;

        Pointer pointer;
        Alloc* alloc;
        [[no_unique_address]] extent_t extent;
    };

    template<class T, DerivedFromAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t);

    template<class U, DerivedFromAllocator Alloc = Allocator, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, Args&&... args) requires (!std::is_array_v<U>);

    template<class U, DerivedFromAllocator Alloc = Allocator, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, std::size_t size) requires std::is_array_v<U>;

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer() :
        pointer(nullptr), alloc(nullptr) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(std::nullptr_t) :
        pointer(nullptr), alloc(nullptr) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(T* pointer, Alloc& alloc) requires (!std::is_array_v<T>) :
        pointer(pointer), alloc(&alloc) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(T pointer, Alloc& alloc, std::size_t extent) requires std::is_array_v<T> :
        pointer(pointer), alloc(&alloc), extent(extent) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(OwningPointer&& other) :
        pointer(other.pointer),
        alloc(other.alloc),
        extent(other.extent)
    {   
        other.pointer = nullptr;
        other.alloc = nullptr;
        other.extent = {};
    }

    template<class T, DerivedFromAllocator Alloc>
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

    template<class T, DerivedFromAllocator Alloc>
    typename std::add_lvalue_reference<T>::type OwningPointer<T, Alloc>::operator*() const requires (!std::is_array_v<T>) {
        return *pointer;
    }

    template<class T, DerivedFromAllocator Alloc>
    T* OwningPointer<T, Alloc>::operator->() const requires (!std::is_array_v<T>) {
        return pointer;
    }

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::~OwningPointer() {
        clear();
    }

    template<class T, DerivedFromAllocator Alloc>
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

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::Pointer OwningPointer<T, Alloc>::get() const {
        return pointer; 
    }

    template<class U, DerivedFromAllocator Alloc, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, Args&&... args) requires (!std::is_array_v<U>) {
        auto p = alloc.allocate(sizeof(U), alignof(U));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new(p) U(std::forward<Args>(args)...);
        return OwningPointer(naked, alloc);
    }

    template<class U, DerivedFromAllocator Alloc, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, std::size_t size) requires std::is_array_v<U> {
        using Element = std::remove_extent_t<U>;
        auto p = alloc.allocate(sizeof(Element) * size, alignof(Element));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new(p) Element[size]();
        return OwningPointer<U, Alloc>(naked, alloc, size);
    }

    template<class T, DerivedFromAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t) {
        return pointer.get() == nullptr;
    }
}
