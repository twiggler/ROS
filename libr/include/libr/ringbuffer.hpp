#pragma once

#include <atomic>
#include <optional>
#include "pointer.hpp"

namespace rlib {


// Single producer single consumer ringbuffer.
template<typename T, std::size_t Size>
class spscBoundedQueue {
public:
    bool enqueue(const T& value);

    T* dequeueAll(T* dest);

private:
    static std::size_t next(std::size_t current);

    T ring[Size];
    std::atomic<std::size_t> head = 0;
    std::atomic<std::size_t> tail = 0;
};

// Adapted from Dmitry Vyukov https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
template<typename T>
class mpmcBoundedQueue {
public:
    static std::expected<OwningPointer<mpmcBoundedQueue>, rlib::Error> make(std::size_t buffer_size, rlib::Allocator& allocator);
   
    bool enqueue(T&& data);

    std::optional<T> dequeue();

private:
    struct Cell {
        std::atomic<std::size_t>        sequence;
        T                               data;
    };

    explicit mpmcBoundedQueue(OwningPointer<Cell[]> buffer);

    OwningPointer<Cell[]>               buffer;
    std::size_t                         bufferMask;
    std::atomic<std::size_t>            enqueuePos = 0;
    std::atomic<std::size_t>            dequeuePos = 0;
};

template<typename T, std::size_t Size>
bool spscBoundedQueue<T, Size>::enqueue(const T& value) {
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
T* spscBoundedQueue<T, Size>::dequeueAll(T* dest) {
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
std::size_t spscBoundedQueue<T, Size>::next(std::size_t current) {
    return (current + 1) % Size;
}

template<class T>
std::expected<OwningPointer<mpmcBoundedQueue<T>>, rlib::Error> mpmcBoundedQueue<T>::make(std::size_t bufferSize, rlib::Allocator& allocator) {
    if (bufferSize < 2 || (bufferSize & (bufferSize - 1)) != 0) {
        return std::unexpected(rlib::InvalidArgument);
    }
    
    auto buffer = construct<Cell[]>(allocator, bufferSize);
    if (buffer == nullptr) {
        return std::unexpected(rlib::OutOfMemoryError);
    }

    struct EnableMaker : public mpmcBoundedQueue<T> { 
        EnableMaker(OwningPointer<Cell[]> buffer) : mpmcBoundedQueue<T>(std::move(buffer)) { }
    };
    auto queue = construct<EnableMaker>(allocator, std::move(buffer));
    if (queue == nullptr) {
        return std::unexpected(rlib::OutOfMemoryError);
    }

    return queue;
}

template<class T>
mpmcBoundedQueue<T>::mpmcBoundedQueue(OwningPointer<Cell[]> buffer) :
    buffer(std::move(buffer)),
    bufferMask(this->buffer.size() - 1)
{
    for (auto i = std::size_t(0); i < this->buffer.size(); ++i) {
        this->buffer[i].sequence.store(i, std::memory_order_relaxed);
    }
}

template<class T>
bool mpmcBoundedQueue<T>::enqueue(T&& data) {
    Cell* cell;
    auto pos = enqueuePos.load(std::memory_order_relaxed);
    while (true) {
        cell = &buffer[pos & bufferMask];
        auto seq = cell->sequence.load(std::memory_order_acquire);
        if (pos == seq) {
            auto success = enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed);
            if (success) {
                break;
            }
        }
        else if (pos > seq)
            return false;
        else
            pos = enqueuePos.load(std::memory_order_relaxed);
    }

    cell->data = std::forward<T>(data);
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
}

template<class T>
std::optional<T> mpmcBoundedQueue<T>::dequeue() {
    Cell* cell;
    auto pos = dequeuePos.load(std::memory_order_relaxed);
    while (true) {
        cell = &buffer[pos & bufferMask];
        auto seq = cell->sequence.load(std::memory_order_acquire);
        if (pos + 1 == seq) {
            auto success = dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed);
            if (success) {
                break;
            }
        }
        else if (pos + 1 > seq)
            return {};
        else
            pos = dequeuePos.load(std::memory_order_relaxed);
    }

    auto data = cell->data;
    cell->sequence.store(pos + bufferMask + 1, std::memory_order_release);

    return data;
}

};
