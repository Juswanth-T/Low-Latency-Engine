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
```

**Access Metrics:**
- **RapidFeed Metrics**: http://localhost:8080/metrics

**Optional: Run Prometheus + Grafana (Docker):**
```powershell
# Run Prometheus
docker run -d -p 9090:9090 -v ${PWD}/infra/prometheus.yaml:/etc/prometheus/prometheus.yml prom/prometheus

# Run Grafana
docker run -d -p 3000:3000 -e GF_SECURITY_ADMIN_PASSWORD=admin grafana/grafana
```

Then access:
- **Prometheus**: http://localhost:9090
- **Grafana**: http://localhost:3000 (admin/admin)

---

### Option 2: Kubernetes (Docker + k8s)

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

## Dependencies

Auto-fetched by CMake:
- **nlohmann/json** (v3.11.3) - JSON parsing
- **prometheus-cpp** (v1.2.4) - Metrics exporter

