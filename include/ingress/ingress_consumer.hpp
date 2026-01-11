#pragma once
#include <string>
#include <atomic>
#include "core/nic_buffer.hpp"
#include "core/ring_buffer.hpp"
#include "core/internal_tick.hpp"

class IngressConsumer {
public:
    IngressConsumer(NICBuffer& nic_buffer, RingBuffer<InternalTick>& ring_buffer);

    void start();
    void stop();

    uint64_t get_packets_consumed() const { return packets_consumed_.load(); }
    uint64_t get_ring_drops() const { return ring_drops_.load(); }

private:
    void run();

    NICBuffer& nic_buffer_;
    RingBuffer<InternalTick>& ring_buffer_;
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> packets_consumed_{0};
    std::atomic<uint64_t> ring_drops_{0};
};
