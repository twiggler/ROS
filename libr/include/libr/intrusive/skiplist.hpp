#pragma once

#include "list.hpp"
#include "common.hpp"
#include <bit>
#include <cstddef>
#include <algorithm>
#include <array>
#include <functional>
#include <expected>

namespace rlib::intrusive {

    template<class T>
    struct SkipListNode {
        rlib::OwningPointer<ListNode<T>[]> listNodes;
    };

    struct Deterministic {
        static std::size_t height(std::size_t numberOfElements, std::size_t maxLayers)
        {
            auto height = std::bit_width(numberOfElements + 1) - 1; // Calculate log2(numberOfElements + 1)
            return std::min(static_cast<std::size_t>(height), maxLayers);
        }
    };

    template<
        class T,
        IsAllocator Alloc     = Allocator,
        class NodeGetter      = NodeFromBase<T, SkipListNode>,
        class LessThan        = std::less<T>,
        class InsertionPolicy = Deterministic>
    struct SkipList {
        template<IsAllocator SAlloc>
        static std::expected<SkipList, Error>
        make(std::size_t maxLayers, Alloc& listNodeAllocator, SAlloc& skipNodeAllocator)
        {
            auto head = construct<SkipListNode<T>>(skipNodeAllocator);
            if (head == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }
            head->listNodes = construct<ListNode<T>[]>(listNodeAllocator, maxLayers);
            if (head->listNodes == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }

            return SkipList(maxLayers, listNodeAllocator, std::move(head));
        }

        std::optional<Error> insert(T& value)
        {
            auto  height = InsertionPolicy::height(numberOfElements, maxLayers);
            auto& layers = NodeGetter::get(&value).listNodes;
            layers       = construct<ListNode<T>[]>(*allocator, height);
            if (layers == nullptr) {
                return {OutOfMemoryError};
            }

            auto predecessorSkipNode = head.get();
            for (auto layer = height; layer > 0; layer--) {
                auto listNode = &predecessorSkipNode->listNodes[layer - 1];
                while (listNode->next() != nullptr) {
                    if (!LessThan{}(*listNode->next()->owner(), value)) {
                        predecessorSkipNode = &NodeGetter::get(listNode->owner());
                        break;
                    }
                    listNode = listNode->next();
                }
                layers[layer - 1].insertAfter(*listNode);
                layers[layer - 1].setOwner(&value);
            }

            numberOfElements++;
            maxHeight = std::max(height, maxHeight);
            return {};
        }

        T* pop_front()
        {
            auto listNode = head->listNodes[0].next();
            if (listNode == nullptr) {
                return nullptr;
            }
            auto data = listNode->owner();

            for (auto& listNode : NodeGetter::get(data).listNodes) {
                listNode.remove();
            }
            numberOfElements--;

            return data;
        }

        void remove(const T& value)
        {
            auto node = NodeGetter::get(value);
            for (auto& listNode : node.listNodes) {
                listNode.remove();
            }
            numberOfElements--;
        }

        template<class U>
        T* find(const U& value) const
        {
            auto predecessorSkipNode = head.get();
            for (auto layer = std::size_t(maxHeight); layer > 0; layer--) {
                auto listNode = &predecessorSkipNode->listNodes[layer - 1];
                while (listNode->next() != nullptr) {
                    if (!LessThan{}(*listNode->next()->owner(), value)) {
                        predecessorSkipNode = &NodeGetter::get(listNode->owner());
                        break;
                    }
                    listNode = listNode->next();
                }
            }

            auto owner = predecessorSkipNode->listNodes[0].owner();
            if (owner != nullptr) {
                return nullptr;
            }
            // If !(a < b) && !(b < a) then a and b are equivalent
            if (LessThan{}(*owner, value) || LessThan{}(value, *owner)) {
                return nullptr;
            }
            return owner;
        }

    private:
        SkipList(std::size_t maxLayers, Alloc& allocator, OwningPointer<SkipListNode<T>> header) :
            maxLayers(maxLayers), allocator(&allocator), head(std::move(header))
        {}

        const std::size_t              maxLayers;
        Alloc*                         allocator;
        OwningPointer<SkipListNode<T>> head;
        std::size_t                    numberOfElements = 0;
        // maxheight is the maximum height of any node ever observed
        std::size_t maxHeight = 0;
    };

} // namespace rlib::intrusive
