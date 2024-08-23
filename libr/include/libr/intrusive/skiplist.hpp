#pragma once

#include "list.hpp"
#include "common.hpp"
#include <bit>
#include <cstddef>
#include <algorithm>
#include <array>
#include <functional>
#include <expected>
#include "detail/skiplist.hpp"

namespace rlib::intrusive {

    template<class T>
    using SkipListNode = detail::SkipListNode<T>;

    struct Deterministic {
        static std::size_t height(std::size_t numberOfElements, std::size_t maxLayers)
        {
            auto rightMostClearedBit = ~numberOfElements & (numberOfElements + 1);
            auto index               = std::bit_width(rightMostClearedBit);
            return std::min(static_cast<std::size_t>(index), maxLayers);
        }
    };

    template<class T, class M, M T::*Member>
    struct ProjectMember {
        M operator()(const T& value) const { return value.*Member; }
    };

    template<class T, class M, M (T::*MemberFunc)() const>
    struct ProjectMemberFunc {
        M operator()(const T& value) const { return (value.*MemberFunc)(); }
    };

    template<
        class T,
        NodeGetter<T, SkipListNode> NG = NodeFromBase<T, SkipListNode>,
        class Project                  = std::identity,
        class LessThan                 = std::less<detail::Projected<T, Project>>,
        IsAllocator Alloc              = Allocator,
        class InsertionPolicy          = Deterministic>
    struct SkipList {
        template<IsAllocator SAlloc>
        static std::expected<SkipList, Error>
        make(std::size_t maxLayers, SAlloc& skipNodeAllocator, Alloc& listNodeAllocator)
        {
            auto head = construct<SkipListNode<T>>(skipNodeAllocator);
            if (head == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }
            head->links = construct<ListNode<T>[]>(listNodeAllocator, maxLayers);
            if (head->links == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }

            return SkipList(maxLayers, listNodeAllocator, std::move(head));
        }

        std::optional<Error> insert(T& value)
        {
            auto  height = InsertionPolicy::height(numberOfElements, maxLayers);
            auto& links  = NG{}(&value).links;
            links        = construct<ListNode<T>[]>(*allocator, height);
            if (links == nullptr) {
                return {OutOfMemoryError};
            }

            auto lessThan = detail::Comparator<T, LessThan, Project>{};
            auto cursor   = detail::ListCursor<T, NG>(head.get(), height);
            do {
                while (cursor.next() != nullptr && lessThan(*cursor.next(), value)) {
                    cursor.advance();
                }
                cursor.addTo(value);
            } while (cursor.descend());

            numberOfElements++;
            maxHeight = std::max(height, maxHeight);
            return {};
        }

        T* popFront()
        {
            auto element = head->links[0].next;
            if (element == nullptr) {
                return nullptr;
            }

            unlink(*head, *element, NG{});
            numberOfElements--;

            return element;
        }

        void remove(T& value)
        {
            unlink(*head, value, NG{});
            numberOfElements--;
        }

        template<class U>
        auto find(const U& value) const
        {
            auto lessThan = detail::Comparator<T, LessThan, Project>{};
            auto cursor   = detail::ListCursor<T, NG>(head.get(), maxHeight);
            do {
                while (cursor.next() != nullptr && lessThan(*cursor.next(), value)) {
                    cursor.advance();
                }
            } while (cursor.descend());

            // !(a < b) && !(b < a) <=> a == b
            return cursor.next() != nullptr && !lessThan(value, *cursor.next()) ? cursor.toIter() : end();
        }

        template<class U>
        auto findFirstGreaterOrEqual(const U& value) const
        {
            auto lessThan = detail::Comparator<T, LessThan, Project>{};
            auto cursor   = detail::ListCursor<T, NG>(head.get(), maxHeight);
            do {
                while (cursor.next() != nullptr && lessThan(*cursor.next(), value)) {
                    cursor.advance();
                }
            } while (cursor.descend());

            // !(a < b) <=> a >= b
            return cursor.toIter();
        }

        template<class U>
        auto findLastSmallerOrEqual(const U& value) const
        {
            auto lessThan = detail::Comparator<T, LessThan, Project>{};
            auto cursor   = detail::ListCursor<T, NG>(head.get(), maxHeight);
            do {
                // a <= b <--> !(b < a)
                while (cursor.next() != nullptr && !lessThan(value, *cursor.next())) {
                    cursor.advance();
                }
            } while (cursor.descend());

            auto iter = cursor.toIter();
            return iter == begin() ? end() : std::prev(iter);
        }

        auto begin() const { return ListIterator(head->links[0], map(NG{}, detail::NodeFromRootLayer{})); }

        auto end() const { return ListIterator(head->links[0].prev, map(NG{}, detail::NodeFromRootLayer{})); }

    private:
        SkipList(std::size_t maxLayers, Alloc& allocator, OwningPointer<SkipListNode<T>> header) :
            maxLayers(maxLayers), allocator(&allocator), head(std::move(header))
        {}

        const std::size_t              maxLayers;
        Alloc*                         allocator;
        OwningPointer<SkipListNode<T>> head;
        std::size_t                    numberOfElements = 0;
        // maxHeight is the maximum height of any node ever observed
        std::size_t maxHeight = 1;
    };

} // namespace rlib::intrusive
