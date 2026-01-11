#pragma once
#include <cstdint>
#include <string>

struct alignas(64) InternalTick{

uint32_t symbol_id;    
    uint32_t exchange_id;  
    int64_t  bid_price;     
    int64_t  ask_price;     
    int64_t  bid_volume;    
    int64_t  ask_volume;    
    uint64_t exchange_ts_ns;
    uint64_t receive_ts_ns; 
    uint32_t sequence_num;
    uint32_t flags;


 };

 static_assert(sizeof(InternalTick) == 64, "InternalTick must be exactly 64 bytes for cache efficiency");

