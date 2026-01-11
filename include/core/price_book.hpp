#pragma once
#include <unordered_map>
#include <shared_mutex>
#include <iostream>
#include "internal_tick.hpp"

struct SymbolState {
    uint32_t symbol_id;
    int64_t  last_bid_price = 0;
    int64_t  last_ask_price = 0;
    int64_t  last_mid_price = 0;
    int64_t  weighted_mid_price = 0; 
    int64_t  total_volume = 0;
    uint64_t last_update_ts = 0;
    int64_t  spread = 0;
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