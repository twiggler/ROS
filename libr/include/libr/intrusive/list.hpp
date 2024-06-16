#pragma once

namespace rlib::intrusive {

template<class T>
struct ListNode {
    T* next;
    T* prev;
};


template<class T>
struct NodeFromBase {
    static ListNode<T>& get(T* element) { return *element; }
};

template <class T, ListNode<T> T::*Node>
struct NodeFromMember {
    static ListNode<T>& get(T* element) {
        return element->*Node;
    }
};

template <typename T, class NodeGetter = NodeFromBase<T>>
class ListIterator {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;

    ListIterator() : element(nullptr) {}
    
    explicit ListIterator(T* node) : element(node) {}

    const T& operator*() const { return *element; }
    
    const T* operator->() const { return element; }

    ListIterator& operator++() {
        element = NodeGetter::get(element).next;
        return *this;
    }

    ListIterator& operator--() {
        element = NodeGetter::get(element).prev;
        return *this;
    }

    ListIterator operator++(int) {
        auto current = ListIterator(*this);
        element = NodeGetter::get(element).next;

        return current;
    }

    ListIterator operator--(int) {
        auto current = ListIterator(*this);
        element = NodeGetter::get(element).previous;

        return current;
    }

    bool operator==(const ListIterator& other) const {
        return element == other.element;
    }

private:
    T* element;
};

template <typename T, class NodeGetter = NodeFromBase<T>>
class List {
public:
    List() : head(nullptr), tail(nullptr) {}

    List(List&& other) : head(other.head), tail(other.tail) {
        other.head = other.tail = nullptr;
    }

    void push_front(T& element) {
        NodeGetter::get(&element).next = head;
        NodeGetter::get(&element).prev = nullptr;
        if (head != nullptr) {
            NodeGetter::get(head).prev = &element;
        } else {
            tail = &element;
        }
        head = &element;
    }

    T* pop_front() {
        if (head == nullptr) {
            return nullptr;
        }
        
        auto element = head;
        head = NodeGetter::get(element).next;
        
        if (head != nullptr) {
            NodeGetter::get(head).prev = nullptr;
        } else {
            tail = nullptr;
        }
        
        NodeGetter::get(element).next = NodeGetter::get(element).prev = nullptr;
        return element;
    }

    void remove(T& element) {
        if (head == nullptr) {
            return;
        }

        if (&element == head) {
            head = NodeGetter::get(head).next;
            if (head != nullptr) {
                NodeGetter::get(head).prev = nullptr;
            }
        } else if (&element == tail) {
            tail = NodeGetter::get(tail).prev;
            if (tail != nullptr) {
                NodeGetter::get(tail).next = nullptr;
            }
        } else {
            auto next = NodeGetter::get(&element).next;
            auto prev = NodeGetter::get(&element).prev;
            NodeGetter::get(prev).next = next;
            NodeGetter::get(next).prev = prev;
        }

        NodeGetter::get(&element).next = NodeGetter::get(&element).prev = nullptr;
    }

    ListIterator<T> begin() {
        return ListIterator<T>(head);
    }

    ListIterator<T> end() {
        return ListIterator<T>(nullptr);
    }

private:
    T* head;
    T* tail;
};

}
