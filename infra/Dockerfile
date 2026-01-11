# ==========================================
# Stage 1: Builder
# ==========================================
FROM gcc:12-bullseye AS builder

WORKDIR /build

RUN apt-get update && apt-get install -y \
    git \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Install CMake 3.28 (required for DOWNLOAD_EXTRACT_TIMESTAMP)
RUN wget https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.sh -O cmake.sh && \
    chmod +x cmake.sh && \
    ./cmake.sh --skip-license --prefix=/usr/local && \
    rm cmake.sh

COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF

RUN cmake --build build --parallel $(nproc)

# ==========================================
# Stage 2: Runtime
# ==========================================
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libstdc++6 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m -u 1000 rapidfeed

WORKDIR /app

COPY --from=builder /build/build/RapidFeed /app/rapidfeed

RUN chown -R rapidfeed:rapidfeed /app

USER rapidfeed

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/metrics || exit 1

CMD ["/app/rapidfeed"]
