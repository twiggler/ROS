#pragma once

#include <slotmap/slotmap.hpp>
#include <libr/pointer.hpp>

namespace rlib {
    template<class Allocator>
    struct ArrayStorage {
        explicit ArrayStorage(Allocator& allocator) : allocator(&allocator) {}

        template<class T>
        OwningPointer<T> operator()(std::size_t capacity)
        {
            return construct<T>(*allocator, capacity);
        }

    private:
        Allocator* allocator;
    };

} // namespace rlib
