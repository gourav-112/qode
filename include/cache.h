#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include "protocol.h"

namespace mdf {

// Market state for a single symbol
struct alignas(64) MarketState {
    double best_bid = 0.0;
    double best_ask = 0.0;
    uint32_t bid_quantity = 0;
    uint32_t ask_quantity = 0;
    double last_traded_price = 0.0;
    uint32_t last_traded_quantity = 0;
    uint64_t last_update_time = 0;
    uint64_t update_count = 0;
    double opening_price = 0.0;   // For % change calculation
};

// Single symbol entry with SeqLock for lock-free reads
// Aligned to cache line to prevent false sharing
struct alignas(128) SymbolEntry {
    std::atomic<uint64_t> sequence{0};  // Odd = writing, Even = valid
    MarketState state;
    char padding[128 - sizeof(std::atomic<uint64_t>) - sizeof(MarketState)];
};

// Lock-free symbol cache using SeqLock pattern
// Single writer (feed handler), multiple readers (visualization)
class SymbolCache {
public:
    SymbolCache(size_t num_symbols = MAX_SYMBOLS);
    
    // Writer methods (single writer thread)
    void update_quote(uint16_t symbol_id, double bid_price, uint32_t bid_qty,
                      double ask_price, uint32_t ask_qty, uint64_t timestamp);
    
    void update_trade(uint16_t symbol_id, double price, uint32_t quantity,
                      uint64_t timestamp);
    
    void update_bid(uint16_t symbol_id, double price, uint32_t quantity,
                    uint64_t timestamp);
    
    void update_ask(uint16_t symbol_id, double price, uint32_t quantity,
                    uint64_t timestamp);
    
    // Reader methods (lock-free, consistent snapshot)
    MarketState get_snapshot(uint16_t symbol_id) const;
    
    // Get all symbols sorted by update count (for visualization)
    void get_top_symbols(uint16_t* out_ids, MarketState* out_states,
                         size_t count) const;
    
    // Get total update count across all symbols
    uint64_t get_total_updates() const;
    
    // Reset all state
    void reset();
    
    size_t num_symbols() const { return num_symbols_; }
    
private:
    size_t num_symbols_;
    std::array<SymbolEntry, MAX_SYMBOLS> entries_;
    
    // Begin write - returns sequence to pass to end_write
    void begin_write(uint16_t symbol_id);
    void end_write(uint16_t symbol_id);
};

} // namespace mdf
