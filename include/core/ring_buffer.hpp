#pragma once

#include <vector>
#include <atomic>
#include <optional>

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);

    bool push(const T& item);
    bool pop(T& item);
    size_t size() const;
    bool empty() const;

    alignas(64) std::atomic<size_t> head_{0};

private:
    const size_t capacity_;
    std::vector<T> buffer_;
    alignas(64) std::atomic<size_t> tail_{0};
};
