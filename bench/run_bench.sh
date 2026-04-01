#!/usr/bin/env bash
set -e

export PATH='/c/msys64/ucrt64/bin:/c/msys64/usr/bin'
export TEMP='/c/Users/Juswanth-T/AppData/Local/Temp'
export TMP='/c/Users/Juswanth-T/AppData/Local/Temp'

ROOT='/c/Users/Juswanth-T/Desktop/TimePass/RapidFeed'
BIN="$ROOT/bench_out.exe"
ROUNDS=50
RESULTS="$ROOT/bench/bench_results.txt"

cd "$ROOT"

echo "=== Compiling ==="
g++ -Iinclude -O3 -std=c++17 \
    bench/ring_buffer_bench.cpp src/core/ring_buffer.cpp \
    -o "$BIN"
echo "OK"
echo ""

> "$RESULTS"

for i in $(seq 1 $ROUNDS); do
    printf "Run %2d/%d ... " "$i" "$ROUNDS"
    out=$("$BIN" 2>/dev/null)

    tp=$(echo "$out"      | grep Throughput | grep -oP '[\d.]+(?= M ticks/sec)')
    lat_avg=$(echo "$out" | grep Latency    | grep -oP 'avg=\K\d+')
    lat_p50=$(echo "$out" | grep Latency    | grep -oP 'p50=\K\d+')
    lat_p99=$(echo "$out" | grep Latency    | grep -oP 'p99=\K\d+(?=ns)')
    lat_p999=$(echo "$out"| grep Latency    | grep -oP 'p99\.9=\K\d+')
    burst_ns=$(echo "$out"| grep Burst      | grep -oP '\d+(?= ns total)')
    burst_per=$(echo "$out"| grep Burst     | grep -oP '[\d.]+(?= ns/tick)')
    push_ns=$(echo "$out" | grep Push       | grep -oP '\d+(?= ns total)')
    push_per=$(echo "$out"| grep Push       | grep -oP '[\d.]+(?= ns/tick)')

    echo "$tp $lat_avg $lat_p50 $lat_p99 $lat_p999 $burst_ns $burst_per $push_ns $push_per" >> "$RESULTS"
    printf "tp=%s  p50=%sns  pop=%.1fns/tick  push=%.1fns/tick\n" "$tp" "$lat_p50" "$burst_per" "$push_per"
done

echo ""
echo "=== Averages over $ROUNDS runs ==="

python3 - <<'EOF'
with open("bench/bench_results.txt") as f:
    rows = [list(map(float, l.split())) for l in f if l.strip()]

n = len(rows)
cols = list(zip(*rows))
names = [
    "Throughput (M ticks/sec)",
    "Latency avg (ns)",
    "Latency p50 (ns)",
    "Latency p99 (ns)",
    "Latency p99.9 (ns)",
    "Pop total (ns)",
    "Pop per tick (ns)",
    "Push total (ns)",
    "Push per tick (ns)",
]

for name, col in zip(names, cols):
    print(f"{name:<28}: {sum(col)/n:.2f}  min={min(col):.2f}  max={max(col):.2f}")
EOF

rm -f "$BIN"
