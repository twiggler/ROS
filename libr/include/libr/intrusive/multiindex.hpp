#pragma once

#include <tuple>
#include <expected>
#include <optional>
#include <libr/pointer.hpp>
#include <libr/error.hpp>
#include "concepts.hpp"
#include <functional>

namespace rlib::intrusive {

    std::optional<Error> insert(auto& element)
    {
        return {};
    }

    std::optional<Error> insert(auto& element, auto& first, auto&... rest)
    {
        auto error = first.insert(element).or_else([&]() { return insert(element, rest...); });
        if (error) {
            first.remove(element);
        }
        return error;
    }


    template<class... Containers>
    std::optional<Error> insert(auto& element, std::tuple<Containers...>& containers)
    {
        return std::apply([&](auto&... cont) { return insert(element, cont...); }, containers);
    }

    template<class T, class Alloc, class... Containers, class... Args>
    std::expected<T*, Error> emplace(Alloc& allocator, std::tuple<Containers...>& containers, Args&&... args)
    {
        auto element = rlib::construct<T>(allocator, std::forward<Args>(args)...);
        if (element == nullptr) {
            return std::unexpected(OutOfMemoryError);
        }

        auto error = insert(*element, containers);
        if (error) {
            return std::unexpected(*error);
        }

        return element.release();
    }

    template<class... Containers>
    void remove(auto& element, std::tuple<Containers...>& containers)
    {
        std::apply([&](auto&... cont) { (cont.remove(element), ...); }, containers);
    }

    template<class... Containers, IsAllocator Alloc>
    void removeAndDestruct(auto& element, Alloc& alloc, std::tuple<Containers...>& containers) {
        remove(element, containers);
        rlib::destruct(&element, alloc);
    }


    std::optional<Error> reinsert(auto& element)
    {
        return {};
    }

    template<class T, class First>
    std::optional<Error> reinsert(T& element, First& first, auto&... rest)
    {
        if constexpr (HasFrontInsertion<T, First>) {
            // If a container has front insertion, it is unordered and we do not have to re-insert the element
            return reinsert(element, rest...);
        }

        first.remove(element);
        return first.insert(element).or_else([&] { return reinsert(element, rest...); });
    }

    template<class UpdateFunc, class... Containers>
    std::optional<Error> update(auto& element, UpdateFunc&& update, std::tuple<Containers...>& containers)
    {
        std::invoke(update, element);

        auto error = std::apply([&](auto&... cont) { return reinsert(element, cont...); }, containers);

        if (error) {
            remove(element, containers);
            return error;
        }

        return {};
    }

    template<class... Containers, class Alloc>
    std::optional<Error> clear(std::tuple<Containers...>& containers, Alloc& alloc) {
        auto& first = std::get<0>(containers);

        auto iter = first.begin();
        while (iter != first.end()) {
            auto current = iter++;
            rlib::destruct(&(*current), alloc);
        }

        return {};
    }

} // namespace rlib::intrusive
