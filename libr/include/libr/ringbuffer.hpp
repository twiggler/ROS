#pragma once
#include <atomic>

namespace rlib {


// Wait-free ringbuffer.
template<typename T, std::size_t Size>
class RingBuffer {
public:
    bool push(const T& value);

    T* popAll(T* dest);

private:
    static std::size_t next(std::size_t current);

    T ring[Size];
    std::atomic<std::size_t> head = 0;
    std::atomic<std::size_t> tail = 0;
};

template<typename T, std::size_t Size>
bool RingBuffer<T, Size>::push(const T& value) {
    auto head_ = head.load(std::memory_order_relaxed);
    auto nextHead = next(head_);
    if (nextHead == tail.load(std::memory_order_acquire)) {
        return false;
    }
    ring[head_] = value;
    head.store(nextHead, std::memory_order_release);
    return true;
}

template<typename T, std::size_t Size>
T* RingBuffer<T, Size>::popAll(T* dest) {
    auto tail_ = tail.load(std::memory_order_relaxed);
    auto head_ = head.load(std::memory_order_acquire);
    while (tail_ != head_) {
        *(dest++) = ring[tail_];
        tail_ = next(tail_);
    }
    tail.store(tail_, std::memory_order_release);
    return dest;
}

template<typename T, std::size_t Size>
std::size_t RingBuffer<T, Size>::next(std::size_t current) {
    return (current + 1) % Size;
}

};
