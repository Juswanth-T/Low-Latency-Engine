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

The `bench/` directory contains a standalone benchmark that measures the SPSC ring buffer at the core of the hot path.

### What it measures

| Benchmark | What it does |
|-----------|-------------|
| **Throughput** | Producer + consumer on separate threads, 10M ticks, spin on full/empty |
| **Round-trip latency** | Ping-pong between two threads: push → responder pop → push response → main pop. Reports avg / p50 / p99 / p99.9 |
| **Burst absorption** | Pre-fills 500 ticks (simulator max burst) then drains single-threaded, measures total time and ns/tick |

### Results (measured on this machine, Windows 11, MSYS2 UCRT64, g++ 15.2.0 -O3)

```
=== RapidFeed SPSC Ring Buffer Benchmark ===
InternalTick size: 64 bytes  (cache-line aligned: 64 bytes)

[Throughput] 10M ticks  |  1004.50 ms  |  9.96 M ticks/sec
[Latency]    200K samples  |  avg=454ns  p50=400ns  p99=700ns  p99.9=3000ns
[Burst]      500 ticks (single-threaded drain)  |  2900 ns total  |  5.80 ns/tick

Note: latency figures include clock::now() overhead (~20-40ns).
      Subtract that for pure ring-buffer push/pop time.
```

> **p99.9 (3µs):** Windows OS thread scheduling jitter — not a ring buffer issue. On Linux with CPU-pinned threads this drops to ~1µs.

### Why these numbers make sense

- **~10M ticks/sec cross-thread on Windows** — Windows thread scheduler adds overhead vs Linux. Linux with `pthread_setaffinity_np` (pinned cores) typically yields 50–100M ticks/sec for this design.
- **~400ns round-trip** — the ring buffer uses pure busy-wait (no mutex, no `condition_variable`, no system calls on the hot path). The latency comes from **cache-line ping-pong**: the `head_` and `tail_` atomics live on separate 64-byte cache lines, but ownership still bounces between cores via the MESI coherency protocol (~100–200ns per hop on Windows). Two round-trips (push direction + response direction) plus two `clock::now()` calls (~40ns each) account for the ~400ns total. One-way push/pop latency is roughly half (~200ns).
- **5.8 ns/tick burst drain** — single-threaded, no contention, shows raw cache speed. The 64KB ring (1024 × 64-byte `InternalTick`) fits entirely in L2 cache.

### How to run it yourself (Windows + MSYS2)

Normal `g++` invocations from Git Bash or VS Code's terminal fail silently on Windows because `cc1plus.exe` (the C++ compiler frontend) tries to write temp files to `TEMP`, which is often unmapped or unwritable in those shells. Use MSYS2's own `bash.exe` with `TEMP` set explicitly:

```bash
# From the project root — run this in PowerShell or cmd.exe
C:\msys64\usr\bin\bash.exe -c "
  export PATH='/c/msys64/ucrt64/bin:/c/msys64/usr/bin' &&
  export TEMP='/c/Users/<your-username>/AppData/Local/Temp' &&
  export TMP='/c/Users/<your-username>/AppData/Local/Temp' &&
  cd '/c/Users/<your-username>/Desktop/TimePass/RapidFeed' &&
  g++ -Iinclude -O3 -std=c++17 bench/ring_buffer_bench.cpp src/core/ring_buffer.cpp -o bench_out.exe &&
  ./bench_out.exe
"
```

Replace `<your-username>` with your Windows username. On Linux/macOS it's just:

```bash
g++ -Iinclude -O3 -std=c++17 bench/ring_buffer_bench.cpp src/core/ring_buffer.cpp -o bench_out && ./bench_out
```

Or via CMake after the main build:

```bash
cmake --build build --target ring_buffer_bench --config Release
./build/bench/ring_buffer_bench.exe
```

---

## Dependencies

Auto-fetched by CMake:
- **nlohmann/json** (v3.11.3) - JSON parsing
- **prometheus-cpp** (v1.2.4) - Metrics exporter

