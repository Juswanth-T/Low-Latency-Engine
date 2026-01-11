// ==========================================
// Market Simulator Implementation
// ==========================================
// This component simulates a high-frequency market feed. It generates
// synthetic trade ticks for Bitcoin and Ethereum across multiple exchanges
// (Binance, Kraken, Coinbase) and pushes them into the NIC buffer.
// It includes logic to simulate "micro-bursts" of traffic to test
// the system's resilience under load.

#include "simulator/market_simulator.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

// ==========================================
// Global Signals
// ==========================================
extern std::atomic<bool> keep_running; 

// ==========================================
// Constructor & Lifecycle Management
// ==========================================

MarketSimulator::MarketSimulator(NICBuffer& nic_buffer)
    : nic_buffer_(nic_buffer)
    , rng_(std::random_device{}()) 
{
}

void MarketSimulator::start() {
    running_ = true;
    run(); 
}

void MarketSimulator::stop() {
    running_ = false;
}

// ==========================================
// Main Simulation Loop
// ==========================================

void MarketSimulator::run() {

    std::uniform_real_distribution<> burst_chance(0.0, 1.0);
    std::uniform_int_distribution<> burst_size(200, 500); 
    std::uniform_int_distribution<> exchange_selector(0, 2);  // 0=BINANCE, 1=KRAKEN, 2=COINBASE

    while (running_ && keep_running) {
        
        // ------------------------------------------
        // Scenario A: Traffic Burst (5% Probability)
        // ------------------------------------------

        if (burst_chance(rng_) < 0.05) {
            int count = burst_size(rng_);
            std::cout << "Simulator: Burst of " << count << " packets" << std::endl;

            for (int i = 0; i < count && running_ && keep_running; ++i) {

                std::string tick_json = generate_tick(i % 2 == 0 ? "BITCOIN" : "ETHEREUM");
                if (!nic_buffer_.push(tick_json)) {
                    nic_drops_++; 
                }
                packets_generated_++;

            }
        } 
        // ------------------------------------------
        // Scenario B: Normal Operation
        // ------------------------------------------
        else {
            for (int i = 0; i < 2; ++i) {
                std::string tick_json = generate_tick(i == 0 ? "BITCOIN" : "ETHEREUM");

                if (!nic_buffer_.push(tick_json)) {
                    nic_drops_++;
                }
                packets_generated_++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ==========================================
// Tick Generation Logic
// ==========================================

std::string MarketSimulator::generate_tick(const std::string& symbol_key) {
    // ------------------------------------------
    // 1. Exchange & Symbol Resolution
    // ------------------------------------------
    static const std::map<std::string, std::map<std::string, std::string>> mapping = {
        {"BITCOIN",  {{"BINANCE", "BTCUSDT"}, {"KRAKEN", "XBT/USD"}, {"COINBASE", "BTC-USD"}}},
        {"ETHEREUM", {{"BINANCE", "ETHUSDT"}, {"KRAKEN", "ETH/USD"}, {"COINBASE", "ETH-USD"}}}
    };

    std::uniform_int_distribution<> exchange_selector(0, 2);
    const char* exchanges[] = {"BINANCE", "KRAKEN", "COINBASE"};
    std::string exchange = exchanges[exchange_selector(rng_)];

    std::string ticker = mapping.at(symbol_key).at(exchange);
    
    // ------------------------------------------
    // 2. Price & Volume Simulation
    // ------------------------------------------
    double base_price = (symbol_key == "BITCOIN") ? 95000.0 : 2800.0;

    std::uniform_real_distribution<> price_jitter(-1.0, 1.0);
    std::uniform_real_distribution<> spread_range(0.1, 3);
    std::uniform_real_distribution<> volume_range(0.001, 2.5);

    double mid = base_price + price_jitter(rng_);
    double spread = spread_range(rng_);
    double bid_p = mid - (spread / 2.0);
    double ask_p = mid + (spread / 2.0);
    double bid_sz = volume_range(rng_);
    double ask_sz = volume_range(rng_);

    // ------------------------------------------
    // 3. Payload Formatting (Exchange Specific)
    // ------------------------------------------
    json payload;

    if (exchange == "BINANCE") {

        std::ostringstream bid_str, ask_str, bid_vol_str, ask_vol_str;
        bid_str << std::fixed << std::setprecision(2) << bid_p;
        ask_str << std::fixed << std::setprecision(2) << ask_p;
        bid_vol_str << std::fixed << std::setprecision(4) << bid_sz;
        ask_vol_str << std::fixed << std::setprecision(4) << ask_sz;

        payload = {
            {"e", "bookTicker"},
            {"s", ticker},
            {"b", bid_str.str()},
            {"B", bid_vol_str.str()},
            {"a", ask_str.str()},
            {"A", ask_vol_str.str()},
            {"T", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };

    } else if (exchange == "KRAKEN") {

        std::ostringstream bid_str, ask_str, bid_vol_str, ask_vol_str;
        bid_str << std::fixed << std::setprecision(2) << bid_p;
        ask_str << std::fixed << std::setprecision(2) << ask_p;
        bid_vol_str << std::fixed << std::setprecision(4) << bid_sz;
        ask_vol_str << std::fixed << std::setprecision(4) << ask_sz;

        payload = {
            {"pair", ticker},
            {"b", {bid_str.str(), static_cast<int>(bid_sz * 100), bid_vol_str.str()}}, // Mixed types in array
            {"a", {ask_str.str(), static_cast<int>(ask_sz * 100), ask_vol_str.str()}},
            {"ts", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() +
                std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() % 1000 / 1000.0}
        };

    } else {  // COINBASE
   
        payload = {
            {"type", "l1_update"},
            {"product_id", ticker},
            {"bid", bid_p},
            {"bid_size", bid_sz},
            {"ask", ask_p},
            {"ask_size", ask_sz},
            {"time",  std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
    }

    return payload.dump();
}