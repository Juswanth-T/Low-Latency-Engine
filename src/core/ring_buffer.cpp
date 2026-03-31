// ==========================================
// Lock-Free Ring Buffer Implementation
// ==========================================
// This is the core data structure for the "Hot Path." It is a Single Producer 
// Single Consumer (SPSC) queue that allows two threads to exchange data 
// without ever using a Mutex or pausing the CPU.

#include "core/ring_buffer.hpp"
#include "core/internal_tick.hpp"
#include <string>

// ==========================================
// Constructor
// ==========================================

template <typename T>
RingBuffer<T>::RingBuffer(size_t capacity)
    : capacity_(capacity), buffer_(capacity) {}

// ==========================================
// Push (Producer)
// ==========================================

template <typename T>
bool RingBuffer<T>::push(const T& item) {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (current_head + 1) % capacity_;

    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false;
    }

    buffer_[current_head] = item;
    head_.store(next_head, std::memory_order_release);
    return true;
}

// ==========================================
// Pop (Consumer)
// ==========================================

template <typename T>
bool RingBuffer<T>::pop(T& item) {
    const size_t current_tail = tail_.load(std::memory_order_relaxed);

    if (current_tail == head_.load(std::memory_order_acquire)) {
        return false;
    }

    item = buffer_[current_tail];
    const size_t next_tail = (current_tail + 1) % capacity_;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

// ==========================================
// Size
// ==========================================

template <typename T>
size_t RingBuffer<T>::size() const {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_relaxed);
    return (h + capacity_ - t) % capacity_;
}

// ==========================================
// Empty
// ==========================================

template <typename T>
bool RingBuffer<T>::empty() const {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
}

// ==========================================
// Template Instantiations
// ==========================================

template class RingBuffer<InternalTick>;
template class RingBuffer<std::string>;
