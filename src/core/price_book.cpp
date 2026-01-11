// ==========================================
// Price Book Implementation
// ==========================================
// It maintains the current state of the market for every symbol.


#include "core/price_book.hpp"
#include <mutex>

// ==========================================
// Tick Processing & Feature Engineering
// ==========================================

void PriceBook::handle_tick(const InternalTick& tick) {

    std::unique_lock<std::shared_mutex> lock(rw_metadata_mtx_);

    auto& state = market_map_[tick.symbol_id];

    // ------------------------------------------
    // 1. Basic Price Calculations
    // ------------------------------------------
    int64_t current_mid = (tick.bid_price + tick.ask_price) / 2;
    int64_t total_tick_vol = tick.bid_volume + tick.ask_volume;

    state.spread = tick.ask_price - tick.bid_price;

    // ------------------------------------------
    // 2. Order Imbalance (Pressure)
    // ------------------------------------------
    if (total_tick_vol > 0) {
        state.imbalance = static_cast<double>(tick.bid_volume - tick.ask_volume) /
                          static_cast<double>(total_tick_vol);
    }

    // ------------------------------------------
    // 3. Weighted Mid Price (Micro-Price)
    // ------------------------------------------
    if (total_tick_vol > 0) {
        state.weighted_mid_price = (tick.bid_price * tick.ask_volume +
                                    tick.ask_price * tick.bid_volume) / total_tick_vol;
    }

    // ------------------------------------------
    // 4. VWAP Accumulation
    // ------------------------------------------
    state.vwap_accum_price += (current_mid * total_tick_vol);
    state.vwap_accum_vol += total_tick_vol;

    // ------------------------------------------
    // 5. State Update
    // ------------------------------------------
    state.symbol_id = tick.symbol_id;
    state.last_bid_price = tick.bid_price;
    state.last_ask_price = tick.ask_price;
    state.last_mid_price = current_mid;
    state.total_volume += total_tick_vol;
    state.last_update_ts = tick.exchange_ts_ns;
}

// ==========================================
// State Accessor (Thread-Safe Reader)
// ==========================================


SymbolState PriceBook::get_symbol_state(uint32_t symbol_id) const {
    std::shared_lock<std::shared_mutex> lock(rw_metadata_mtx_);

    if (market_map_.find(symbol_id) != market_map_.end()) {
        return market_map_.at(symbol_id);
    }
    return SymbolState{};
}