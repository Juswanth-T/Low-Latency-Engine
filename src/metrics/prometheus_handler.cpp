// ==========================================
// Prometheus Handler Implementation
// ==========================================
// This component acts as the bridge between the C++ Engine and the 
// Prometheus monitoring system. It creates an embedded HTTP server (Exposer)
// that Prometheus scrapes to retrieve metrics.

#include "metrics/prometheus_handler.hpp"

// ==========================================
// Constructor
// ==========================================

PrometheusHandler::PrometheusHandler(const std::string& bind_address)
    : exposer_(bind_address),
      registry_(std::make_shared<prometheus::Registry>()),
      buffer_peak_family_(prometheus::BuildGauge()
          .Name("rapidfeed_buffer_peak")
          .Help("Peak buffer utilization per 1s window")
          .Register(*registry_)),
      drops_family_(prometheus::BuildCounter()
          .Name("rapidfeed_drops_total")
          .Help("Buffer overflow drops")
          .Register(*registry_)),
      packets_family_(prometheus::BuildCounter()
          .Name("rapidfeed_packets_total")
          .Help("Packets processed")
          .Register(*registry_)),
      latency_family_(prometheus::BuildGauge()
          .Name("rapidfeed_latency_us")
          .Help("Processing latency in microseconds")
          .Register(*registry_)),
      last_reset_(std::chrono::steady_clock::now()) {
    exposer_.RegisterCollectable(registry_);
}

// ==========================================
// Metrics Update
// ==========================================

void PrometheusHandler::update(
    size_t nic_buffer_size,
    size_t ring_buffer_size,
    uint64_t nic_drops,
    uint64_t ring_drops,
    uint64_t ingress_packets,
    uint64_t internal_latency_ns) {

    // ------------------------------------------
    // 1. Calculate Real-Time Utilization
    // ------------------------------------------

    double nic_size = static_cast<double>(nic_buffer_size);
    double ring_size = static_cast<double>(ring_buffer_size);
    double nic_util = (nic_size / 50.0) * 100.0;
    double ring_util = (ring_size / 1024.0) * 100.0;

    if (nic_size > nic_size_peak_) nic_size_peak_ = nic_size;
    if (nic_util > nic_util_peak_) nic_util_peak_ = nic_util;
    if (ring_size > ring_size_peak_) ring_size_peak_ = ring_size;
    if (ring_util > ring_util_peak_) ring_util_peak_ = ring_util;

    // ------------------------------------------
    // 2. Publish Peaks (1 Second Interval)
    // ------------------------------------------
    auto now = std::chrono::steady_clock::now();
    if (now - last_reset_ >= std::chrono::seconds(1)) {
        buffer_peak_family_.Add({{"buffer", "nic"}, {"metric", "size"}}).Set(nic_size_peak_);
        buffer_peak_family_.Add({{"buffer", "nic"}, {"metric", "util"}}).Set(nic_util_peak_);
        buffer_peak_family_.Add({{"buffer", "ring"}, {"metric", "size"}}).Set(ring_size_peak_);
        buffer_peak_family_.Add({{"buffer", "ring"}, {"metric", "util"}}).Set(ring_util_peak_);

        nic_size_peak_ = nic_util_peak_ = ring_size_peak_ = ring_util_peak_ = 0.0;
        last_reset_ = now;
    }

    // ------------------------------------------
    // 3. Update Cumulative Counters
    // ------------------------------------------
    static uint64_t last_nic_drops = 0, last_ring_drops = 0, last_packets = 0;

    if (nic_drops > last_nic_drops) {
        drops_family_.Add({{"buffer", "nic"}}).Increment(nic_drops - last_nic_drops);
        last_nic_drops = nic_drops;
    }
    if (ring_drops > last_ring_drops) {
        drops_family_.Add({{"buffer", "ring"}}).Increment(ring_drops - last_ring_drops);
        last_ring_drops = ring_drops;
    }
    if (ingress_packets > last_packets) {
        packets_family_.Add({{"stage", "ingress"}}).Increment(ingress_packets - last_packets);
        last_packets = ingress_packets;
    }
    
    // ------------------------------------------
    // 4. Update Instantaneous Latency
    // ------------------------------------------
    latency_family_.Add({{"stage", "ingress_to_processor"}})
        .Set(static_cast<double>(internal_latency_ns) / 1000.0);
}
