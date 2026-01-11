// ==========================================
// Ingress Consumer Implementation
// ==========================================
// This component consumes raw string packets
// from the Network Interface Card (NIC) buffer, determines which exchange sent them,
// and normalizes them into a standard format (InternalTick).

#include "ingress/ingress_consumer.hpp"
#include "ingress/decoder_factory.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <iostream>

using json = nlohmann::json;

extern std::atomic<bool> keep_running;

// ==========================================
// Constructor
// ==========================================

IngressConsumer::IngressConsumer(NICBuffer& nic_buffer, RingBuffer<InternalTick>& ring_buffer)
    : nic_buffer_(nic_buffer)
    , ring_buffer_(ring_buffer)
{
}

// ==========================================
// Lifecycle
// ==========================================

void IngressConsumer::start() {
    running_ = true;
    run();
}

void IngressConsumer::stop() {
    running_ = false;
}

// ==========================================
// Main Loop
// ==========================================

void IngressConsumer::run() {
    std::string raw_json;

    while (running_ && keep_running) {
        // ------------------------------------------
        // 1. Fetch Raw Packet
        // ------------------------------------------
        if (nic_buffer_.pop(raw_json)) {
            packets_consumed_++;

            uint64_t receive_ts = std::chrono::high_resolution_clock::now()
                                    .time_since_epoch().count();

            try {
                auto j = json::parse(raw_json);

                // ------------------------------------------
                // 2. Exchange Detection (Sniffing)
                // ------------------------------------------
                std::string exchange_name;
                if (j.contains("s")) {
                    exchange_name = "BINANCE";
                } else if (j.contains("pair")) {
                    exchange_name = "KRAKEN";
                } else if (j.contains("product_id")) {
                    exchange_name = "COINBASE";
                } else {
                    continue;
                }
                
                // ------------------------------------------
                // 3. Normalization (Decoding)
                // ------------------------------------------
                auto decoder = DecoderFactory::get_decoder(exchange_name);
                if (decoder) {
                    InternalTick tick = decoder->decode(raw_json, receive_ts);
                    
                    // ------------------------------------------
                    // 4. Push to Hot Path
                    // ------------------------------------------
                    if (!ring_buffer_.push(tick)) {
                        ring_drops_++;
                    }

                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            } catch (const std::exception& e) {
            }
        } else {
            std::this_thread::yield();
        }
    }
}
