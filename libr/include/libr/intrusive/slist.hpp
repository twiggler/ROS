#pragma once

namespace rlib::intrusive {

template<class T>
struct SListNode {
    T* next;
};

template<class T>
class SListIterator {
public:
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    
    explicit SListIterator(T* node) : node(node) {}
    
    const T& operator*() { return *node }
    
    const T* operator->() { return node }
    
    SListIterator& operator++() {
        node = node->next;
        return *this;
    }

    SListIterator operator++(int) {
        auto current = SListIterator(*this);
        node = node->next;

        return current;
    }
    
    bool operator==(const SListIterator& other) const {
        return node == other.node;
    }

private:
    T* node;
};

// This is going to be used for the instrusive page frame stack. 
template <typename T>
class SList {
public:
    SList() : head(nullptr) {}

    void push_front(T& node) {
        node.next = head;
        head = &node;
    }

    T* pop_front() {
        if (head == nullptr) {
            return nullptr;
        }

        auto node = head;
        head = node->next;
        
        node->next = nullptr;
        return node;
    }

    SListIterator begin() {
        return SListIterator{head};
    }

    SListIterator end() {
        return SListIterator{nullptr};
    }

private:
    T* head;
};

}
