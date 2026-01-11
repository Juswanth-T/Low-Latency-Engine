#pragma once

#include <unordered_map>
#include <shared_mutex>
#include <iostream>
#include <array>
#include "internal_tick.hpp"

struct SymbolState {
    uint32_t symbol_id;

    std::array<int64_t, 4> exchange_bid_prices{};
    std::array<int64_t, 4> exchange_ask_prices{};

    int64_t  best_bid_price = 0;
    int64_t  best_ask_price = INT64_MAX;
    uint8_t  best_bid_exchange = 0;
    uint8_t  best_ask_exchange = 0;

    int64_t  last_mid_price = 0;
    int64_t  spread = 0;

    int64_t  weighted_mid_price = 0;
    int64_t  total_volume = 0;
    uint64_t last_update_ts = 0;
    double   imbalance = 0.0;
    int64_t  vwap_accum_price = 0;
    int64_t  vwap_accum_vol = 0;
};

class PriceBook {
public:
    PriceBook() = default;

    void handle_tick(const InternalTick& tick);
    SymbolState get_symbol_state(uint32_t symbol_id) const;

private:
    std::unordered_map<uint32_t, SymbolState> market_map_;
    mutable std::shared_mutex rw_metadata_mtx_;
};
