#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "core/internal_tick.hpp"

using json = nlohmann::json;

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual InternalTick decode(const std::string& raw_msg, uint64_t receive_ts) = 0;

protected:
    static constexpr double SCALING_FACTOR = 1000000000.0;
    int64_t to_fixed(const json& j_val) {
        if (j_val.is_string()) {
            return static_cast<int64_t>(std::stod(j_val.get<std::string>()) * SCALING_FACTOR);
        }
        return static_cast<int64_t>(j_val.get<double>() * SCALING_FACTOR);
    }
};

class DecoderFactory {
public:
    static std::unique_ptr<Decoder> get_decoder(const std::string& exchange_name);
};