#pragma once

#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <prometheus/counter.h>
#include <chrono>
#include <memory>
#include <string>

class PrometheusHandler {
public:
    explicit PrometheusHandler(const std::string& bind_address);

    void update(
        size_t nic_buffer_size,
        size_t ring_buffer_size,
        uint64_t nic_drops,
        uint64_t ring_drops,
        uint64_t ingress_packets,
        uint64_t internal_latency_ns);

private:
    prometheus::Exposer exposer_;
    std::shared_ptr<prometheus::Registry> registry_;

    prometheus::Family<prometheus::Gauge>& buffer_peak_family_;
    prometheus::Family<prometheus::Counter>& drops_family_;
    prometheus::Family<prometheus::Counter>& packets_family_;
    prometheus::Family<prometheus::Gauge>& latency_family_;

    double nic_size_peak_ = 0.0;
    double nic_util_peak_ = 0.0;
    double ring_size_peak_ = 0.0;
    double ring_util_peak_ = 0.0;
    std::chrono::steady_clock::time_point last_reset_;
};
