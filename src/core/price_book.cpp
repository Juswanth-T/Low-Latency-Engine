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

    int64_t current_mid = (tick.bid_price + tick.ask_price) / 2;
    int64_t total_tick_vol = tick.bid_volume + tick.ask_volume;

    // ------------------------------------------
    // 1. Order Imbalance (Pressure)
    // ------------------------------------------
    if (total_tick_vol > 0) {
        state.imbalance = static_cast<double>(tick.bid_volume - tick.ask_volume) /
                          static_cast<double>(total_tick_vol);
    }

    // ------------------------------------------
    // 2. Weighted Mid Price (Micro-Price)
    // ------------------------------------------
    if (total_tick_vol > 0) {
        state.weighted_mid_price = (tick.bid_price * tick.ask_volume +
                                    tick.ask_price * tick.bid_volume) / total_tick_vol;
    }

    // ------------------------------------------
    // 3. VWAP Accumulation
    // ------------------------------------------
    state.vwap_accum_price += (current_mid * total_tick_vol);
    state.vwap_accum_vol += total_tick_vol;

    // ------------------------------------------
    // 4. Per-Exchange State Update
    // ------------------------------------------
    state.exchange_bid_prices[tick.exchange_id] = tick.bid_price;
    state.exchange_ask_prices[tick.exchange_id] = tick.ask_price;

    // ------------------------------------------
    // 5. Compute Best Bid/Ask Across All Exchanges
    // ------------------------------------------
    state.best_bid_price = 0;
    state.best_bid_exchange = 0;
    for (uint8_t i = 1; i <= 3; ++i) {
        if (state.exchange_bid_prices[i] > state.best_bid_price) {
            state.best_bid_price = state.exchange_bid_prices[i];
            state.best_bid_exchange = i;
        }
    }

    state.best_ask_price = INT64_MAX;
    state.best_ask_exchange = 0;
    for (uint8_t i = 1; i <= 3; ++i) {
        if (state.exchange_ask_prices[i] > 0 &&
            state.exchange_ask_prices[i] < state.best_ask_price) {
            state.best_ask_price = state.exchange_ask_prices[i];
            state.best_ask_exchange = i;
        }
    }

    // ------------------------------------------
    // 6. Derived Metrics
    // ------------------------------------------
    state.last_mid_price = (state.best_bid_price + state.best_ask_price) / 2;
    state.spread = state.best_ask_price - state.best_bid_price;

    // ------------------------------------------
    // 7. State Update
    // ------------------------------------------
    state.symbol_id = tick.symbol_id;
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
