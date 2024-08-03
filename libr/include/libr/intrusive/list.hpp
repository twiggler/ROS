#pragma once

#include "common.hpp"
#include <libr/allocator.hpp>
#include <libr/pointer.hpp>
#include <expected>

namespace rlib::intrusive {

    // Node of a circular doubly linked list.
    // Rationale: By implementing basic operations in the node itself, it can be composed with other data structures.
    template<class T>
    class ListNode {
    public:
        ListNode() : _next(this), _prev(this) {}

        void remove()
        {
            _prev->_next = _next;
            _next->_prev = _prev;
            _next = _prev = this;
            _owner         = nullptr;
        }

        void insertAfter(ListNode& after, T& value)
        {
            remove();
            _next        = after._next;
            _prev        = &after;
            _next->_prev = this;
            after._next  = this;
            _owner       = &value;
        }

        bool isHead() const { return _owner == nullptr; }

        ListNode* next() const { return _next->isHead() ? nullptr : _next; }

        ListNode* prev() const { return _next->isHead() ? nullptr : _prev; }

        T* owner() { return _owner; }

    private:
        ListNode* _next;
        ListNode* _prev;

        // Rationale:
        // Storing a pointer to the parent element is expensive.
        // Technically, it is possible to  obtain an element pointer from a node pointer instead.
        // However, this would require a downcast to the element type in the case of inheriting from the ListNode.
        // In the case of the ListNode being a member of the element, ABI specific offset calculations are necessary.
        // Using of the offsetof macro leads to undefined behavior if the parent element is not a standard layout type.
        //
        // Consider optimizing memory usage by removing owner if the parent element is a standard layout type.
        T* _owner = nullptr;
    };

    template<typename T>
    class ListIterator {
    public:
        using value_type      = T;
        using difference_type = std::ptrdiff_t;

        ListIterator() : node(nullptr) {}

        explicit ListIterator(ListNode<T>* node) : node(node) {}

        const T& operator*() const { return *node->_owner(); }

        const T* operator->() const { return node->_owner(); }

        ListIterator& operator++()
        {
            node = node->next();
            return *this;
        }

        ListIterator& operator--()
        {
            node = node->prev();
            return *this;
        }

        ListIterator operator++(int)
        {
            auto current = ListIterator(node);
            node         = node->next();

            return current;
        }

        ListIterator operator--(int)
        {
            auto current = ListIterator(node);
            node         = node->prev();

            return current;
        }

        bool operator==(const ListIterator& other) const { return node == other.node; }

    private:
        ListNode<T>* node;
    };

    template<typename T, class NodeGetter = NodeFromBase<T, ListNode>>
    class List {
    public:
        template<IsAllocator Alloc>
    static std::expected<List, Error> make(Alloc& allocator)
        {
            auto head = construct<ListNode<T>>(allocator);
            if (head == nullptr) {
                return std::unexpected(OutOfMemoryError);
            }

            return List(std::move(head));
        }

        void push_front(T& element)
        {
            auto& node = NodeGetter::get(&element);
            node.insertAfter(*head, element);
        }

        T* pop_front()
        {
            auto node = head->next();
            if (node == nullptr) {
                return nullptr;
            }
            auto element = node->owner();
            node->remove();

            return element;
        }

        void remove(T& element) { NodeGetter::get(&element).remove(); }

        ListIterator<T> begin() { return ListIterator<T>(head->next()); }

        ListIterator<T> end() { return ListIterator<T>(nullptr); }

        bool empty() const { return head->next() == nullptr; }

        T* front() const { return head->next()->owner(); }

        T* back() const { return head->prev()->owner(); }

    private:
        explicit List(OwningPointer<ListNode<T>> head) : head(std::move(head)) {}

        OwningPointer<ListNode<T>> head;
    };

    // Helper template alias to facilitate the use of List with a member node.
    template<class T, ListNode<T> T::*Node>
    using ListWithNodeMember = List<T, NodeFromMember<T, ListNode, Node>>;

} // namespace rlib::intrusive
