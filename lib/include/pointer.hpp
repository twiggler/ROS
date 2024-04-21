#pragma once
#include "allocator.hpp"

namespace rlib {
    
    template<class T, DerivedFromAllocator Alloc = Allocator>
    class OwningPointer {
    public:
        OwningPointer();

        OwningPointer(std::nullptr_t);
        
        OwningPointer(T* pointer, Alloc& alloc);

        OwningPointer(const OwningPointer&) = delete;

        OwningPointer(OwningPointer&& other);

        OwningPointer& operator=(OwningPointer&& other);

        OwningPointer& operator=(const OwningPointer& ) = delete;

        typename std::add_lvalue_reference<T>::type operator*() const;

        T* operator->() const;

        void clear();

        T* get() const;
        
        ~OwningPointer();

    private:
        T* pointer;
        Alloc* alloc;
    };

    template<class T, DerivedFromAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t);

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer() :
        pointer(nullptr), alloc(nullptr) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(std::nullptr_t) :
        pointer(nullptr), alloc(nullptr) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(T* pointer, Alloc& alloc) :
        pointer(pointer), alloc(&alloc) {}

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::OwningPointer(OwningPointer&& other) :
        pointer(other.pointer),
        alloc(other.alloc)
    {   
        other.pointer = nullptr;
        other.alloc = nullptr;
    }

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>& OwningPointer<T, Alloc>::operator=(OwningPointer<T, Alloc>&& other) {
        clear();
        pointer = other.pointer;
        alloc = other.alloc;
        other.pointer = nullptr;
        other.alloc = nullptr;

        return *this;
    }

    template<class T, DerivedFromAllocator Alloc>
    typename std::add_lvalue_reference<T>::type OwningPointer<T, Alloc>::operator*() const {
        return *pointer;
    }

    template<class T, DerivedFromAllocator Alloc>
    T* OwningPointer<T, Alloc>::operator->() const {
        return pointer;
    }

    template<class T, DerivedFromAllocator Alloc>
    OwningPointer<T, Alloc>::~OwningPointer() {
        this->clear();
    }

    template<class T, DerivedFromAllocator Alloc>
    void OwningPointer<T, Alloc>::clear() {
        if (pointer == nullptr) {
            return;
        }

        pointer->~T();
        alloc->deallocate(pointer, sizeof(T), alignof(T));
    }

    template<class T, DerivedFromAllocator Alloc>
    T* OwningPointer<T, Alloc>::get() const {
        return pointer; 
    }

    template<class U, DerivedFromAllocator Alloc = Allocator, class... Args>
    OwningPointer<U, Alloc> construct(Alloc& alloc, Args&&... args) {
        auto p = alloc.allocate(sizeof(U), alignof(U));
        if (p == nullptr) {
            return nullptr;
        }

        auto naked = ::new(p) U(std::forward<Args>(args)...);
        return OwningPointer(naked, alloc);
    };

    template<class T, DerivedFromAllocator Alloc>
    bool operator==(const OwningPointer<T, Alloc>& pointer, std::nullptr_t) {
        return pointer.get() == nullptr;
    }
}
