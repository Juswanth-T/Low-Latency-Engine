// ==========================================
// Decoder Factory Implementation
// ==========================================
// This component normalizes the crypto exchange protocols.
// Each exchange uses a slightly different JSON schema, timestamp format, and 
// data type convention (e.g., strings vs floats for prices).

#include "ingress/decoder_factory.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

// ==========================================
// 1. Binance Decoder
// Format: {"s":"BTCUSDT", "b":"95000.50", "B":"1.23", ...}
// ==========================================
class BinanceDecoder : public Decoder {
public:
    InternalTick decode(const std::string& raw_msg, uint64_t receive_ts) override {
        auto j = json::parse(raw_msg);
        InternalTick tick{};

        tick.exchange_id = 1;
        // Mapping Binance Tickers
        std::string sym = j["s"];
        tick.symbol_id = (sym == "BTCUSDT") ? 1 : 2;

        tick.bid_price = to_fixed(j["b"]);
        tick.bid_volume = to_fixed(j["B"]);
        tick.ask_price = to_fixed(j["a"]);
        tick.ask_volume = to_fixed(j["A"]);
        
        // Binance 'T' is in Milliseconds
        tick.exchange_ts_ns = j["T"].get<uint64_t>() * 1000000;
        tick.receive_ts_ns = receive_ts;

        return tick;
    }
};

// ==========================================
// 2. Kraken Decoder
// Format: {"pair":"XBT/USD", "b":["95000.50", "123", "1.23"], ...}
// ==========================================
class KrakenDecoder : public Decoder {
public:
    InternalTick decode(const std::string& raw_msg,uint64_t receive_ts) override {
        auto j = json::parse(raw_msg);
        InternalTick tick{};

        tick.exchange_id = 2;
        // Mapping Kraken Tickers (XBT vs BTC)
        std::string sym = j["pair"];
        tick.symbol_id = (sym == "XBT/USD") ? 1 : 2;

        // Kraken sends price/volume in an array: [price, whole_v, float_v]
        tick.bid_price = to_fixed(j["b"][0]);
        tick.bid_volume = to_fixed(j["b"][2]);
        tick.ask_price = to_fixed(j["a"][0]);
        tick.ask_volume = to_fixed(j["a"][2]);

        // Kraken 'ts' is in Seconds (float)
        tick.exchange_ts_ns = static_cast<uint64_t>(j["ts"].get<double>() * 1e9);
        tick.receive_ts_ns = receive_ts;

        return tick;
    }
};

// ==========================================
// 3. Coinbase Decoder
// Format: {"product_id":"BTC-USD", "bid": 95000.50, ...}
// ==========================================
class CoinbaseDecoder : public Decoder {
public:
    InternalTick decode(const std::string& raw_msg, uint64_t receive_ts) override {
        auto j = json::parse(raw_msg);
        InternalTick tick{};

        tick.exchange_id = 3;
        std::string sym = j["product_id"];
        tick.symbol_id = (sym == "BTC-USD") ? 1 : 2;

        tick.bid_price = to_fixed(j["bid"]);
        tick.bid_volume = to_fixed(j["bid_size"]);
        tick.ask_price = to_fixed(j["ask"]);
        tick.ask_volume = to_fixed(j["ask_size"]);

        tick.exchange_ts_ns = j["time"].get<uint64_t>() * 1000000;
        tick.receive_ts_ns = receive_ts;

        return tick;
    }
};

// ==========================================
// Factory Implementation
// ==========================================
std::unique_ptr<Decoder> DecoderFactory::get_decoder(const std::string& exchange_name) {
    if (exchange_name == "BINANCE") return std::make_unique<BinanceDecoder>();
    if (exchange_name == "KRAKEN")  return std::make_unique<KrakenDecoder>();
    if (exchange_name == "COINBASE") return std::make_unique<CoinbaseDecoder>();
    
    return nullptr;
}