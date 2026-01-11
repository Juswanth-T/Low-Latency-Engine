#pragma once
#include <string>
#include <atomic>
#include <random>
#include "core/nic_buffer.hpp"

class MarketSimulator {
public:
    MarketSimulator(NICBuffer& nic_buffer);

    void start();
    void stop();

    uint64_t get_packets_generated() const { return packets_generated_.load(); }
    uint64_t get_nic_drops() const { return nic_drops_.load(); }

private:
    void run();
    std::string generate_tick(const std::string& symbol_key);

    NICBuffer& nic_buffer_;
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> packets_generated_{0};
    std::atomic<uint64_t> nic_drops_{0};
    std::mt19937 rng_;
};
