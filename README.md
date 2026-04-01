# RapidFeed - High-Frequency Trading Price Aggregator

A low-latency C++ engine that aggregates cryptocurrency prices from multiple exchanges (Binance, Kraken, Coinbase) with Prometheus metrics, graphana dashboards and Kubernetes deployment.

## Architecture

```
Exchange Simulator → NIC Buffer → Ring Buffer → Price Book → Prometheus → Grafana
                                                                                                                     
```

**4 Threads:**
1. **Exchange Simulator**: Generates market data (BTC/ETH)
2. **Ingress Consumer**: Moves packets from NIC to ring buffer
3. **Processor**: Updates price book, calculates latency, pushes metrics
4. **Logger**: Real-time console output

## Quick Start (2 Ways)

### Option 1: Native Build

**Prerequisites:**
- Docker Desktop

**Build & Run:**
```powershell
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run
.\build\Release\RapidFeed.exe
```

**Console Output:**
```
[Price Book]
  BTC/USD: Bid=$95123.45 (Binance) Ask=$95120.10 (Kraken) Mid=$95121.78 Spread=$3.35
  ETH/USD: Bid=$3456.78 (Coinbase) Ask=$3455.12 (Kraken) Mid=$3455.95 Spread=$1.66
.... [ Streaming output ]
```

**Access Metrics:**
- **RapidFeed Metrics**: http://localhost:8080/metrics

**Run Prometheus + Grafana (Docker):**
```powershell
# Run Prometheus with alerts
docker run -d --name prometheus -p 9090:9090 `
  -v ${PWD}/infra/prometheus-local.yml:/etc/prometheus/prometheus.yml `
  -v ${PWD}/infra/prometheus-rules-local.yml:/etc/prometheus/rules.yml `
  prom/prometheus

# Run Grafana
docker run -d --name grafana -p 3000:3000 -e GF_SECURITY_ADMIN_PASSWORD=admin grafana/grafana
```

Then access:
- **Prometheus**: http://localhost:9090 (alerts at http://localhost:9090/alerts)
- **Grafana**: http://localhost:3000 (admin/admin)

**Note**: Prometheus will scrape metrics from your native RapidFeed running on port 8080 and evaluate all 6 alerts.

---

### Option 2: Kubernetes

**Prerequisites:**
- Docker Desktop (with Kubernetes enabled)
- kubectl

**Deploy:**
```powershell
# Build image
docker build -t rapidfeed:latest -f infra/Dockerfile .

# Deploy all 3 pods (RapidFeed + Prometheus + Grafana)
kubectl apply -k infra/

# Check status
kubectl get pods
```

**Access Services:**
- **RapidFeed Metrics**: http://localhost:30080/metrics
- **Prometheus**: http://localhost:30090
- **Grafana**: http://localhost:30300 (admin/admin)

**Clean Up:**
```powershell
kubectl delete -k infra/
```

See [infra/README.md](infra/README.md) for detailed Kubernetes instructions.

## Key Metrics

**Prometheus Metrics:**
- `rapidfeed_buffer_peak` - Peak buffer utilization (NIC/Ring)
- `rapidfeed_drops_total` - Packet drops (overflow detection)
- `rapidfeed_packets_total` - Ingress throughput
- `rapidfeed_latency_us` - Processing latency (microseconds)

**Alerts (6):**
- NICBufferHigh (>90% util)
- RingBufferHigh (>70% util)
- NICOverflow (packet drops)
- RingOverflow (packet drops)
- HighLatency (>200µs)
- Stalled (no packets for 1m)

## Benchmarks

The file [`bench/ring_buffer_bench.cpp`](bench/ring_buffer_bench.cpp) benchmarks the SPSC ring buffer that sits on the hot path between the exchange simulator and the price book processor. Four separate scenarios are measured, each isolating a different aspect of performance.

---

### Background: what is being measured and why

The ring buffer (`RingBuffer<InternalTick>`) is a lock-free Single Producer Single Consumer (SPSC) queue. One thread pushes ticks in, another pops them out — no mutex, no OS calls on the hot path. The four benchmarks answer four different questions:

1. **How many ticks/sec can it sustain end-to-end across two threads?** (Throughput)
2. **How long does one tick take to travel from producer to consumer and back?** (Latency — measured as round-trip on one clock, divided by 2 for one-way estimate)
3. **What does `pop()` actually cost in hardware, with zero cross-thread noise?** (Raw pop speed — single-threaded drain)
4. **What does `push()` actually cost in hardware, with zero cross-thread noise?** (Raw push speed — single-threaded fill)

---

### Benchmark 1 — Throughput

**Question:** How many `InternalTick` structs can two threads exchange per second when running flat-out?

**Setup:**
```
Thread A (producer)          Thread B (consumer)
──────────────────           ───────────────────
loop 10,000,000 times:       loop until 10M received:
  spin until push() = true     spin until pop() = true
```

Both threads are created first, then they spin on an atomic `go` flag until the main thread sets it. This ensures thread-creation overhead (~100–500µs on Windows) is excluded from the measurement. The clock starts right before `go = true` and stops after both threads finish.

```
elapsed_ms   = time from go=true → both threads done
M ticks/sec  = 10,000,000 / elapsed_seconds / 1,000,000
```

**Why spin instead of sleep/notify?** Because this is what the real system does — the ingress consumer busy-waits on the ring buffer. Mutex-based signalling would measure OS overhead, not the ring buffer.

---

### Benchmark 2 — Round-trip latency

**Question:** How long does it take for a tick to go from the producer, be picked up by a consumer, and a response come back?

**Setup — two ring buffers, two threads:**
```
Main thread                     Responder thread
───────────────                 ────────────────
t0 = now()                      loop:
push tick → req_rb                if pop(req_rb):
spin until pop(rsp_rb)              push → rsp_rb
t1 = now()
record (t1 - t0) ns
repeat 200,000 times
```

This is called **ping-pong**. The main thread sends one tick and immediately waits for the echo. Every round-trip is timestamped individually. After all 200,000 samples are collected:

```
sort all latencies
avg   = sum / 200,000
p50   = value at index 100,000   (median — 50% of trips were faster)
p99   = value at index 198,000   (99% of trips were faster than this)
p99.9 = value at index 199,800   (tail latency — worst 0.1%)
```

**Why measure round-trip if production is one-way?** You cannot accurately measure one-way latency across two threads — each core's `clock::now()` may drift relative to the other by hundreds of nanoseconds. By bouncing the tick back to the same thread, both timestamps come from the same clock, making the measurement accurate. Divide the reported value by 2 to get the one-way push→pop estimate (~210 ns at p50).

**Warm-up:** Before recording samples, 1024 round-trips are run and discarded. This gets the ring buffer's cache lines (`head_`, `tail_`, `buffer_`) into L1/L2 cache on both cores so the first real sample isn't a cold-cache outlier.

**Clock overhead:** Each iteration calls `Clock::now()` twice. On Windows with MSYS2 this costs ~20–40ns per call, so ~40–80ns of the reported latency is measurement overhead, not ring buffer time.

---

### Benchmark 3 — Raw `pop()` speed (single-threaded burst)

**Question:** What does `pop()` actually cost in hardware, with zero cross-thread noise?

**The problem with the other two benchmarks:** both involve two threads on two CPU cores. Every time the producer writes `head_` and the consumer reads it, the CPU's MESI cache coherency protocol has to transfer ownership of that cache line between cores via L3 (the shared cache) — typically 100–200 ns per transfer. This is not the ring buffer's cost, it's the physics of multi-core hardware. To measure the ring buffer's data structure cost alone, cross-core traffic must be eliminated entirely.

**How the cache hierarchy works on this machine:**

```
Core A (producer)               Core B (consumer)
─────────────────               ─────────────────
L1 private (~48KB)              L1 private (~48KB)
L2 private (~1.25MB)            L2 private (~1.25MB)
          └──── L3 shared across all cores (~16MB) ────┘
```

When producer writes `buffer_[head]`, the cache line lives in core A's L1. When consumer reads it, that line is not in core B's L1 or L2 — it has to travel: **core A L1 → L3 → core B L2 → core B L1**. That round-trip is ~100–150 ns and is the dominant cost in the latency benchmark.

**To eliminate this:** push 500 ticks into the ring first (untimed, no consumer), then time a single thread draining all 500 with `pop()`. One thread, one core — no cross-core transfers.

```
Step 1 — fill (not timed):
  single thread pushes 500 ticks → ring
  all cache lines now sit in this core's L1/L2

Step 2 — drain (timed):
  t0 = now()
  same single thread pops all 500 ticks   ← L1/L2 hits only, no L3
  t1 = now()

raw pop cost = (t1 - t0) / 500 ≈ 5.9 ns per tick
```

**What those 5.9 ns consist of:**
- `tail_.load(memory_order_relaxed)` — atomic load, already in L1
- Compare against `head_` — already in L1
- Copy 64-byte `InternalTick` from `buffer_[tail]` — L1 hit
- `tail_.store(memory_order_release)` — atomic store to L1

No DRAM, no L3, no cross-core invalidations. Just the data structure operations at L1/L2 speed.

**Note:** `push()` is timed separately in Benchmark 4. Despite having the same logical operations (relaxed load → boundary check → 64-byte copy → release store), `push()` is measured to be **~70% slower** (~10.8 ns vs ~6.4 ns) due to the cache-line write ownership cost — see Benchmark 4 for the explanation.

**Why 500 ticks?** Matches the exchange simulator's maximum burst size. At 5.9 ns/tick, 500 ticks drains in ~2950 ns (~3 µs).

---

### Results (averaged over 50 runs)

**Environment:** Windows 11, MSYS2 UCRT64, g++ 15.2.0 `-O3`, `InternalTick` = 64 bytes (cache-line aligned)

Each benchmark was compiled once and executed 50 times back-to-back. The table below shows the mean across all runs, plus the observed min/max to show variance.

#### Throughput — 10M ticks, producer + consumer on separate threads

Both threads run **simultaneously** — not sequentially. A shared atomic `go` flag ensures both threads are already created and spinning before the clock starts, so thread-creation overhead (~500 µs on Windows) is excluded. When `go = true` is set, both threads are released at the same instant:

```
Main          Producer thread        Consumer thread
────          ───────────────        ───────────────
create prod → spins on go==false
create cons →                        spins on go==false
t_start=now()
go = true   → pushes 10M ticks  AND  pops 10M ticks   ← simultaneous
join both...
t_end=now()
```

The producer spins when the ring is full (capacity 1024); the consumer spins when it's empty. This is **not raw ring buffer speed** — it includes all cross-core MESI cache coherency overhead, which is the dominant cost: every `push` writes `head_`, which invalidates the consumer core's cached copy of `head_` and forces it to fetch the updated value from L3. Vice versa for `tail_` on every `pop`. This is unavoidable in any two-thread queue and is what the 9.2M ticks/sec number reflects — real sustained throughput as the system actually operates.

| Metric           | Mean     | Min     | Max      |
| :--------------- | -------: | ------: | -------: |
| Ticks/sec        | 9.31 M   | 7.81 M  | 11.93 M  |

#### One-way push→pop latency — 200K samples per run

In production the data flow is one-way: producer pushes, consumer pops. But you cannot measure one-way latency accurately across two threads — each thread calls `clock::now()` on its own core, and those core clocks can drift relative to each other by hundreds of nanoseconds, making the result meaningless.

The solution is to measure from **one clock on one core**: the main thread pushes a tick into `req_rb`, waits for an echo back on `rsp_rb`, then records the total time. A dedicated responder thread does nothing but pop from `req_rb` and immediately push into `rsp_rb`. Both timestamps (`t0` before push, `t1` after receiving echo) are taken on the same core with the same clock, so the measurement is accurate.

```
Main thread (one clock)        Responder thread
───────────────────────        ────────────────
t0 = now()
push → req_rb         →        pop from req_rb
                               push → rsp_rb
pop from rsp_rb       ←
t1 = now()
record (t1 - t0)               ← this is 2× one-way latency
```

**One-way push→pop latency ≈ reported value ÷ 2 ≈ 210 ns (p50).**

This is repeated 200,000 times. All samples are sorted to compute percentiles.

| Percentile  | Mean    | Min    | Max     |
| :---------- | ------: | -----: | ------: |
| avg         | 452 ns  | 342 ns | 602 ns  |
| p50         | 428 ns  | 300 ns | 600 ns  |
| p99         | 720 ns  | 500 ns | 1300 ns |
| p99.9       | 3224 ns | 1700 ns| 4200 ns |

#### Raw `pop()` and `push()` speed — 500-tick single-threaded

Both measured with one thread, cache pre-warmed, zero cross-core MESI traffic.

**`pop()` — pre-fill ring, time the drain:**
```
untimed: push 500 ticks  (data lands in L1/L2 of this core)
  timed: t0 → pop 500 ticks → t1  |  ns/tick = (t1-t0) / 500
```

**`push()` — warm cache, time the fill:**
```
untimed: push+drain 500 ticks  (warms head_/tail_/buffer_ cache lines)
  timed: t0 → push 500 ticks into empty ring → t1  |  ns/tick = (t1-t0) / 500
```

| Metric            | Mean     | Min      | Max      |
| :---------------- | -------: | -------: | -------: |
| `pop()` per tick  | 6.41 ns  | 6.20 ns  | 8.60 ns  |
| `push()` per tick | 10.83 ns | 10.60 ns | 11.80 ns |

**Why push (~10.8 ns) is slower than pop (~6.4 ns):**
`push()` writes a full 64-byte `InternalTick` into the buffer. Before any write, the CPU must claim **exclusive ownership** of that cache line (MESI Modified state) — even single-threaded, this involves a write-invalidate through the cache hierarchy and is more expensive than a read. `pop()` only reads the struct (Shared state). This is a hardware asymmetry between cache-line reads and writes, not a code issue.

**Reading all results:**

| Number | What it means |
| :----- | :------------ |
| **9.3 M ticks/sec** | Sustained cross-thread throughput, Windows 11, averaged over 50 runs. Includes MESI coherency overhead |
| **~108 ns/tick pipeline interval** | Derived from throughput (1/9.3M). This is pipeline rate — producer and consumer run simultaneously. Not the same as round-trip |
| **p50 = 428 ns (round-trip)** | Measured round-trip on one clock. Estimated one-way push→pop ≈ 214 ns |
| **p99 = 720 ns** | 99% of all trips complete under 720 ns |
| **p99.9 = 3.2 µs** | Tail spike from Windows OS thread scheduling jitter — not the ring buffer |
| **6.4 ns/tick** | Raw `pop()` cost: single-thread, L1/L2-hot, zero cross-core overhead |
| **10.8 ns/tick** | Raw `push()` cost: same conditions, higher due to exclusive cache-line write ownership |

> All numbers measured on Windows 11, MSYS2 UCRT64, g++ 15.2.0 `-O3`, averaged over 50 runs. Latency figures include two `clock::now()` calls (~40–80 ns overhead total).

---

### How to run it yourself

#### Single run

**Windows (MSYS2 UCRT64) — from PowerShell or cmd.exe:**

```powershell
C:\msys64\usr\bin\bash.exe bench\script.sh
```

**Linux / macOS:**

```bash
g++ -Iinclude -O3 -std=c++17 bench/ring_buffer_bench.cpp src/core/ring_buffer.cpp -o bench_out && ./bench_out
```

#### 50-run averaged benchmark

Runs the benchmark 50 times, prints each result, then prints the mean/min/max across all runs. Results are also saved to `bench/bench_results.txt`.

**Windows (from PowerShell or cmd.exe):**

```powershell
C:\msys64\usr\bin\bash.exe bench\run_bench.sh
```

**Linux / macOS:**

```bash
bash bench/run_bench.sh
```

---

## Dependencies

Auto-fetched by CMake:
- **nlohmann/json** (v3.11.3) - JSON parsing
- **prometheus-cpp** (v1.2.4) - Metrics exporter

