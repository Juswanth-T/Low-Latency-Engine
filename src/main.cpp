// ==========================================
// RapidFeed Aggregator - Main Entry Point
// ==========================================
// This file orchestrates the price aggregator by spawning four concurrent threads:
// 
// 1. Simulator (Exchange): Generates synthetic market data (BTC/ETH) with random bursts.
// 2. Ingress :Moves raw packets from the NIC Buffer to the Lock-Free Ring Buffer.
// 3. Processor : Updates the Price Book, calculates latency, and pushes metrics.
// 4. Logger: Prints real-time statistics to the console

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>

#include "core/ring_buffer.hpp"
#include "core/price_book.hpp"
#include "core/nic_buffer.hpp"
#include "simulator/market_simulator.hpp"
#include "ingress/ingress_consumer.hpp"
#include "metrics/prometheus_handler.hpp"

std::atomic<bool> keep_running{true};
auto metrics_handler = std::make_shared<PrometheusHandler>("0.0.0.0:8080");

void signal_handler(int signal) {
    std::cout << "\n[System] Shutdown signal received (" << signal << ")..." << std::endl;
    keep_running = false;
}

// ==========================================
// Main Entry Point
// ==========================================

int main() {
    std::signal(SIGINT, signal_handler);

    std::cout << "Starting RapidFeed Aggregator..." << std::endl;

    auto nic_buffer = std::make_shared<NICBuffer>(50);
    auto ring_buffer = std::make_shared<RingBuffer<InternalTick>>(1024);
    auto price_book = std::make_shared<PriceBook>();

    // ==========================================
    // Thread 1: Market Simulator
    // ==========================================

    MarketSimulator simulator(*nic_buffer);
    std::thread simulator_thread([&]() {
        try {
            std::cout << "[Thread: Exchange] Market simulator started." << std::endl;
            simulator.start();
        } catch (const std::exception& e) {
            std::cerr << "[Thread: Exchange] Error: " << e.what() << std::endl;
            keep_running = false;
        }
    });

    // ==========================================
    // Thread 2: Ingress Consumer
    // ==========================================

    IngressConsumer consumer(*nic_buffer, *ring_buffer);
    std::thread ingress_thread([&]() {
        try {
            std::cout << "[Thread: Ear] Ingress consumer started." << std::endl;
            consumer.start();
        } catch (const std::exception& e) {
            std::cerr << "[Thread: Ear] Error: " << e.what() << std::endl;
            keep_running = false;
        }
    });

    // ==========================================
    // Thread 3: Processor
    // ==========================================

    std::thread processor_thread([ring_buffer, price_book, nic_buffer, &simulator, &consumer]() {
        std::cout << "[Thread: Brain] Processing started." << std::endl;
        InternalTick tick;

        while (keep_running) {
            if (ring_buffer->pop(tick)) {
                uint64_t t3_now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                uint64_t internal_lat = t3_now - tick.receive_ts_ns;

                price_book->handle_tick(tick);

                metrics_handler->update(
                    nic_buffer->size(),
                    ring_buffer->size(),
                    simulator.get_nic_drops(),
                    consumer.get_ring_drops(),
                    consumer.get_packets_consumed(),
                    internal_lat
                );

                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            } else {
                std::this_thread::yield();
            }
        }
    });

    // ==========================================
    // Thread 4: Logger
    // ==========================================

    std::thread logger_thread([nic_buffer, ring_buffer, price_book, &simulator, &consumer]() {
        while (keep_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // std::cout << "\n=== C++ Engine Status ===" << std::endl;

            // std::cout << "[Buffers]" << std::endl;
            // std::cout << "  NIC Buffer:  " << (nic_buffer->size() * 100 / 50) << "%" << std::endl;
            // std::cout << "  Ring Buffer: " << (ring_buffer->size() * 100 / 1024) << "%" << std::endl;

            // std::cout << "[Packet Flow]" << std::endl;
            // std::cout << "  Network In:  " << simulator.get_packets_generated() << " packets" << std::endl;
            // std::cout << "  Ingress Out: " << consumer.get_packets_consumed() << " packets" << std::endl;
            // uint64_t lost = simulator.get_packets_generated() - consumer.get_packets_consumed();
            // std::cout << "  Lost:        " << lost << " packets" << std::endl;

            // std::cout << "[Buffer Overflows]" << std::endl;
            // std::cout << "  NIC Drops:   " << simulator.get_nic_drops() << std::endl;
            // std::cout << "  Ring Drops:  " << consumer.get_ring_drops() << std::endl;

            std::cout << "[Price Book]" << std::endl;
            for (uint32_t id = 1; id <= 2; ++id) {
                auto state = price_book->get_symbol_state(id);
                const char* symbol = (id == 1) ? "BTC/USD" : "ETH/USD";
                const char* exchanges[] = {"", "Binance", "Kraken", "Coinbase"};
                std::cout << "  " << symbol << ": "
                          << "Bid=$" << (state.best_bid_price / 1000000000.0) << " (" << exchanges[state.best_bid_exchange] << ") "
                          << "Ask=$" << (state.best_ask_price / 1000000000.0) << " (" << exchanges[state.best_ask_exchange] << ") "
                          << "Mid=$" << (state.last_mid_price / 1000000000.0)
                          << " Spread=$" << (state.spread / 1000000000.0) << std::endl;
            }
        }
    });

    // ==========================================
    // Shutdown
    // ==========================================

    if (simulator_thread.joinable()) simulator_thread.join();
    if (ingress_thread.joinable()) ingress_thread.join();
    if (processor_thread.joinable()) processor_thread.join();
    if (logger_thread.joinable()) logger_thread.join();

    std::cout << "RapidFeed shutdown complete." << std::endl;
    return 0;
}
