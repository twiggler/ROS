#pragma once

#include "common.hpp"
#include <libr/allocator.hpp>
#include <libr/pointer.hpp>
#include <expected>
#include <iterator>

namespace rlib::intrusive {


    template<class T>
    struct ListNode {
    public:
        ListNode() = default;

        ListNode(const ListNode&) = delete;

        ListNode& operator=(const ListNode&) = delete;

        // Note the asymmetry, which:
        //  * allows for a sentinel node to be used as a head, and
        //  * makes a pointer to the owner unnecessary.
        //
        // The list is circular when traversing backwards, and linear when traversing forwards.
        // Because the sentinel node has no owner, the next pointer of the last node is null.
        T*        next = nullptr;
        ListNode* prev = this;
    };

    // Free functions to link and unlink nodes.
    // Rationale: Implementing basic operations as free functions facilitates composition.
    template<class T, NodeGetter<T, ListNode> NG>
    void link(ListNode<T>& head, T& element, ListNode<T>& after, NG&& nodeGetter)
    {
        auto& nodeElement = nodeGetter(element);

        nodeElement.next = after.next;
        nodeElement.prev = &after;

        // This complexity is the cost of having an assymetrical link.
        if (after.next != nullptr) {
            nodeGetter(after.next).prev = &nodeElement;
        } else {
            // We are linking the last element, update the head.
            head.prev = &nodeElement;
        }
        after.next = &element;
    }

    template<class T, NodeGetter<T, ListNode> NG>
    void unlink(ListNode<T>& head, T& element, NG&& nodeGetter)
    {
        auto& nodeElement = nodeGetter(element);
        unlink(head, nodeElement, nodeGetter);
    }

    template<class T, NodeGetter<T, ListNode> NG>
    void unlink(ListNode<T>& head, ListNode<T>& nodeElement, NG&& nodeGetter)
    {
        // This complexity is the cost of having an assymetrical link.
        if (nodeElement.next != nullptr) {
            nodeGetter(nodeElement.next).prev = nodeElement.prev;
        } else {
            // We are unlinking the last element, update the head.
            head.prev = nodeElement.prev;
        }
        nodeElement.prev->next = nodeElement.next;

        nodeElement.prev = &nodeElement;
        nodeElement.next = nullptr;
    }

    template<typename T, NodeGetter<T, ListNode> NG>
    class ListIterator {
    public:
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;

        ListIterator() = default;

        explicit ListIterator(ListNode<T>& node)
        requires(std::default_initializable<NG>)
            : node(&node), nodeGetter{}
        {}

        explicit ListIterator(ListNode<T>* node)
        requires(std::default_initializable<NG>)
            : node(node), nodeGetter{}
        {}

        ListIterator(ListNode<T>& node, NG nodeGetter) : node(&node), nodeGetter(std::move(nodeGetter)) {}

        ListIterator(ListNode<T>* node, NG nodeGetter) : node(node), nodeGetter(std::move(nodeGetter)) {}

        T* operator->() const { return node->next; }

        T& operator*() const { return *this->operator->(); }

        ListIterator& operator++()
        {
            node = &nodeGetter(node->next);
            return *this;
        }

        ListIterator& operator--()
        {
            node = node->prev;
            return *this;
        }

        ListIterator operator++(int)
        {
            auto current = ListIterator(node, nodeGetter);
            node         = &nodeGetter(node->next);
            return current;
        }

        ListIterator operator--(int)
        {
            auto current = ListIterator(node, nodeGetter);
            node         = node->prev;
            return current;
        }

        bool operator==(const ListIterator& other) const { return node == other.node; }

    private:
        ListNode<T>*             node;
        [[no_unique_address]] NG nodeGetter;
    };

    template<typename T, NodeGetter<T, ListNode> NG = NodeFromBase<T, ListNode>>
    class List {
    public:
        static_assert(std::bidirectional_iterator<ListIterator<T, NG>>);

        template<IsAllocator Alloc>
        static std::expected<List, Error> make(Alloc& allocator)
        {
            auto head = construct<ListNode<T>>(allocator);
            if (head == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }

            return List(std::move(head));
        }

        void pushFront(T& element) { link(*head, element, *head, NG{}); }

        T* popFront()
        {
            auto element = head->next;
            if (element == nullptr) {
                return nullptr;
            }
            unlink(*head, *element, NG{});

            return element;
        }

        void remove(T& element) { unlink(*head, element, NG{}); }

        ListIterator<T, NG> begin() { return {*head}; }

        ListIterator<T, NG> end() { return {head->prev}; }

        bool empty() const { return head->next == nullptr; }

        T* front() const { return head->next; }

        T* back() const { return head->prev->prev->next; }

    private:
        explicit List(OwningPointer<ListNode<T>> head) : head(std::move(head)) {}

        OwningPointer<ListNode<T>> head;
    };

    // Helper template alias to facilitate the use of List with a member node.
    template<class T, ListNode<T> T::*Node>
    using ListWithNodeMember = List<T, NodeFromMember<T, ListNode, Node>>;

} // namespace rlib::intrusive
