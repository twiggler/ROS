#pragma once

namespace rlib::intrusive {

template<class T>
struct ListNode {
    T* next;
    T* prev;
};

template <typename T>
class ListIterator {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;

    ListIterator() : node(nullptr) {}
    
    explicit ListIterator(T* node) : node(node) {}

    const T& operator*() const { return *node; }
    
    const T* operator->() const { return node; }

    ListIterator& operator++() {
        node = node->next;
        return *this;
    }

    ListIterator& operator--() {
        node = node->prev;
        return *this;
    }

    ListIterator operator++(int) {
        auto current = ListIterator(*this);
        node = node->next;

        return current;
    }

    ListIterator operator--(int) {
        auto current = ListIterator(*this);
        node = node->previous;

        return current;
    }

    bool operator==(const ListIterator& other) const {
        return node == other.node;
    }

private:
    T* node;
};

template <typename T>
class List {
public:
    List() : head(nullptr), tail(nullptr) {}

    void push_front(T& node) {
        node.next = head;
        node.prev = nullptr;
        if (head != nullptr) {
            head->prev = &node;
        } else {
            tail = &node;
        }
        head = &node;
    }

    T* pop_front() {
        if (head == nullptr) {
            return nullptr;
        }
        
        auto node = head;
        head = node->next;
        
        if (head != nullptr) {
            head->prev = nullptr;
        } else {
            tail = nullptr;
        }
        
        node->next = node->prev = nullptr;
        return node;
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
