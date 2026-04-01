/**
 * RapidFeed Ring Buffer Benchmark
 *
 * Measures two things:
 *   1. Throughput  – how many ticks/sec the SPSC ring buffer can sustain
 *   2. Latency     – per-message round-trip through push → pop (p50/p99/p99.9)
 *
 * Build (from the build/ dir):
 *   cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . --target ring_buffer_bench
 * Run:
 *   ./bench/ring_buffer_bench
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "core/ring_buffer.hpp"
#include "core/internal_tick.hpp"

using Clock     = std::chrono::high_resolution_clock;
using ns_t      = std::chrono::nanoseconds;

// ---------------------------------------------------------------------------
// Helper: build a realistic InternalTick so the compiler can't optimise away
// ---------------------------------------------------------------------------
static InternalTick make_tick(uint32_t seq) {
    InternalTick t{};
    t.symbol_id      = 1;
    t.exchange_id    = 2;
    t.bid_price      = 95'000'000'000'000LL + seq;   // fixed-point, scaled 1e9
    t.ask_price      = 95'000'100'000'000LL + seq;
    t.bid_volume     = 500'000'000LL;
    t.ask_volume     = 750'000'000LL;
    t.exchange_ts_ns = static_cast<uint64_t>(
        Clock::now().time_since_epoch().count());
    t.receive_ts_ns  = t.exchange_ts_ns;
    t.sequence_num   = seq;
    t.flags          = 0;
    return t;
}

// ---------------------------------------------------------------------------
// Benchmark 1 – Throughput
//   Producer thread: pushes N ticks as fast as possible (spin on full)
//   Consumer thread: pops as fast as possible (spin on empty)
//   A start latch (atomic<bool>) ensures both threads are scheduled and
//   spinning before the clock starts, so thread-creation overhead (~100–500µs
//   on Windows) is excluded from the measurement.
//   Reports: million ticks/sec
// ---------------------------------------------------------------------------
static void bench_throughput(size_t n_messages, size_t ring_capacity) {
    RingBuffer<InternalTick> rb(ring_capacity);
    std::atomic<bool> go{false};

    InternalTick tx = make_tick(0);
    std::chrono::time_point<Clock> t_start, t_end;

    std::thread producer([&]() {
        while (!go.load(std::memory_order_acquire)) {}   // wait for start latch
        for (size_t i = 0; i < n_messages; ++i) {
            tx.sequence_num = static_cast<uint32_t>(i);
            while (!rb.push(tx)) { /* spin – ring full */ }
        }
    });

    std::thread consumer([&]() {
        InternalTick rx{};
        size_t count = 0;
        while (!go.load(std::memory_order_acquire)) {}   // wait for start latch
        while (count < n_messages) {
            if (rb.pop(rx)) ++count;
        }
    });

    // Both threads are created and spinning — start the clock now
    t_start = Clock::now();
    go.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    t_end = Clock::now();

    auto elapsed_ns = std::chrono::duration_cast<ns_t>(t_end - t_start).count();
    double elapsed_s  = elapsed_ns / 1e9;
    double mticks_sec = static_cast<double>(n_messages) / elapsed_s / 1e6;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "[Throughput] "
              << n_messages / 1'000'000 << "M ticks  |  "
              << elapsed_s * 1000.0 << " ms  |  "
              << mticks_sec << " M ticks/sec\n";
}

// ---------------------------------------------------------------------------
// Benchmark 2 – Round-trip latency (ping-pong between two threads)
//   Main thread pushes one tick and immediately waits for a response tick.
//   Responder thread pops from req_rb, copies to rsp_rb.
//   Captures nanosecond timestamps via high_resolution_clock.
//   Reports p50 / p99 / p99.9 in nanoseconds.
// ---------------------------------------------------------------------------
static void bench_latency(size_t n_samples) {
    RingBuffer<InternalTick> req_rb(64), rsp_rb(64);
    std::vector<int64_t> latencies;
    latencies.reserve(n_samples);

    // Warm up: run the full round-trip path (main → req_rb → responder → rsp_rb → main)
    // so both buffers' cache lines and the responder thread's cache are hot
    // before any timed samples are taken.
    std::atomic<bool> warmup_done{false};
    std::atomic<size_t> warmup_count{0};
    constexpr size_t kWarmupRounds = 1024;

    std::thread warmup_responder([&]() {
        InternalTick t{};
        while (warmup_count.load(std::memory_order_relaxed) < kWarmupRounds) {
            if (req_rb.pop(t)) {
                while (!rsp_rb.push(t)) {}
                warmup_count.fetch_add(1, std::memory_order_release);
            }
        }
        warmup_done.store(true, std::memory_order_release);
    });

    {
        InternalTick t = make_tick(0), r{};
        for (size_t i = 0; i < kWarmupRounds; ++i) {
            while (!req_rb.push(t)) {}
            while (!rsp_rb.pop(r)) {}
        }
    }
    warmup_responder.join();

    std::thread responder([&]() {
        InternalTick t{};
        size_t count = 0;
        while (count < n_samples) {
            if (req_rb.pop(t)) {
                while (!rsp_rb.push(t)) {}
                ++count;
            }
        }
    });

    InternalTick tx = make_tick(0), rx{};
    for (size_t i = 0; i < n_samples; ++i) {
        tx.sequence_num = static_cast<uint32_t>(i);
        auto t0 = Clock::now();
        while (!req_rb.push(tx)) {}
        while (!rsp_rb.pop(rx)) {}
        auto t1 = Clock::now();
        latencies.push_back(
            std::chrono::duration_cast<ns_t>(t1 - t0).count());
    }

    responder.join();

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) -> int64_t {
        size_t idx = static_cast<size_t>(p / 100.0 * n_samples);
        if (idx >= n_samples) idx = n_samples - 1;
        return latencies[idx];
    };
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0LL) /
                 static_cast<double>(n_samples);

    std::cout << "[Latency]    "
              << n_samples / 1000 << "K samples  |  "
              << "avg="  << static_cast<int64_t>(avg) << "ns  "
              << "p50="  << pct(50)   << "ns  "
              << "p99="  << pct(99)   << "ns  "
              << "p99.9=" << pct(99.9) << "ns\n";
}

// ---------------------------------------------------------------------------
// Benchmark 3 – Single-threaded burst drain
//   Pre-fills burst_size ticks into the ring (no concurrent producer),
//   then times how fast a single consumer can drain them.
//   This isolates raw pop throughput from cache with no cross-core contention.
//   Matches the simulator's max burst size (500 ticks).
// ---------------------------------------------------------------------------
static void bench_burst(size_t burst_size, size_t ring_capacity) {
    RingBuffer<InternalTick> rb(ring_capacity);
    InternalTick tx = make_tick(0);

    // Pre-fill burst into the buffer
    size_t pushed = 0;
    for (; pushed < burst_size; ++pushed) {
        tx.sequence_num = static_cast<uint32_t>(pushed);
        if (!rb.push(tx)) break;  // stop if full (shouldn't happen with 1024 cap)
    }

    auto t0 = Clock::now();
    InternalTick rx{};
    size_t popped = 0;
    while (popped < pushed) {
        if (rb.pop(rx)) ++popped;
    }
    auto elapsed_ns = std::chrono::duration_cast<ns_t>(Clock::now() - t0).count();

    std::cout << "[Burst]      "
              << pushed << " ticks (single-threaded drain)  |  "
              << elapsed_ns << " ns total  |  "
              << elapsed_ns / static_cast<double>(pushed) << " ns/tick\n";
}

// ---------------------------------------------------------------------------
// Benchmark 4 – Single-threaded raw push speed
//   Symmetric counterpart to bench_burst (which times pop).
//   Warm-up pass first: push+drain 500 ticks so head_/tail_/buffer_ cache
//   lines are hot in L1. Then time pushing 500 ticks into the empty ring.
//   Single thread only — no cross-core MESI traffic, no consumer competing.
//   Reports ns/tick for push() in isolation.
// ---------------------------------------------------------------------------
static void bench_push(size_t burst_size, size_t ring_capacity) {
    RingBuffer<InternalTick> rb(ring_capacity);
    InternalTick tx = make_tick(0);
    InternalTick rx{};

    // Warm up: push then drain so all cache lines are hot before timing
    for (size_t i = 0; i < burst_size; ++i) {
        tx.sequence_num = static_cast<uint32_t>(i);
        rb.push(tx);
    }
    while (rb.pop(rx)) {}  // drain — ring is now empty, cache lines hot

    // Timed push: single thread fills the ring
    auto t0 = Clock::now();
    size_t pushed = 0;
    for (; pushed < burst_size; ++pushed) {
        tx.sequence_num = static_cast<uint32_t>(pushed);
        if (!rb.push(tx)) break;  // stop if full (shouldn't happen with 1024 cap)
    }
    auto elapsed_ns = std::chrono::duration_cast<ns_t>(Clock::now() - t0).count();

    std::cout << "[Push]       "
              << pushed << " ticks (single-threaded fill)   |  "
              << elapsed_ns << " ns total  |  "
              << elapsed_ns / static_cast<double>(pushed) << " ns/tick\n";
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== RapidFeed SPSC Ring Buffer Benchmark ===\n";
    std::cout << "InternalTick size: " << sizeof(InternalTick) << " bytes"
              << "  (cache-line aligned: " << alignof(InternalTick) << " bytes)\n\n";

    // Throughput: 10M messages, ring size 1024 (matches production config)
    bench_throughput(10'000'000, 1024);

    // Latency: 200K round-trips
    bench_latency(200'000);

    // Raw pop speed: 500 ticks pre-filled, single-threaded drain
    bench_burst(500, 1024);

    // Raw push speed: warm cache, single-threaded fill of empty ring
    bench_push(500, 1024);

    std::cout << "\nNote: latency figures include clock::now() overhead (~20-40ns).\n";
    std::cout << "      Subtract that for pure ring-buffer push/pop time.\n";
    return 0;
}
