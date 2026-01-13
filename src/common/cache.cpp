#include "cache.h"
#include <algorithm>
#include <vector>

namespace mdf {

SymbolCache::SymbolCache(size_t num_symbols) 
    : num_symbols_(std::min(num_symbols, MAX_SYMBOLS)) {
    reset();
}

void SymbolCache::begin_write(uint16_t symbol_id) {
    if (symbol_id >= num_symbols_) return;
    
    // Increment sequence to odd (indicates write in progress)
    auto& entry = entries_[symbol_id];
    uint64_t seq = entry.sequence.load(std::memory_order_relaxed);
    entry.sequence.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
}

void SymbolCache::end_write(uint16_t symbol_id) {
    if (symbol_id >= num_symbols_) return;
    
    // Increment sequence to even (indicates write complete)
    auto& entry = entries_[symbol_id];
    std::atomic_thread_fence(std::memory_order_release);
    uint64_t seq = entry.sequence.load(std::memory_order_relaxed);
    entry.sequence.store(seq + 1, std::memory_order_release);
}

void SymbolCache::update_quote(uint16_t symbol_id, double bid_price, uint32_t bid_qty,
                                double ask_price, uint32_t ask_qty, uint64_t timestamp) {
    if (symbol_id >= num_symbols_) return;
    
    begin_write(symbol_id);
    
    auto& state = entries_[symbol_id].state;
    state.best_bid = bid_price;
    state.bid_quantity = bid_qty;
    state.best_ask = ask_price;
    state.ask_quantity = ask_qty;
    state.last_update_time = timestamp;
    state.update_count++;
    
    // Set opening price on first update
    if (state.opening_price == 0.0) {
        state.opening_price = (bid_price + ask_price) / 2.0;
    }
    
    end_write(symbol_id);
}

void SymbolCache::update_trade(uint16_t symbol_id, double price, uint32_t quantity,
                                uint64_t timestamp) {
    if (symbol_id >= num_symbols_) return;
    
    begin_write(symbol_id);
    
    auto& state = entries_[symbol_id].state;
    state.last_traded_price = price;
    state.last_traded_quantity = quantity;
    state.last_update_time = timestamp;
    state.update_count++;
    
    // Set opening price on first trade
    if (state.opening_price == 0.0) {
        state.opening_price = price;
    }
    
    end_write(symbol_id);
}

void SymbolCache::update_bid(uint16_t symbol_id, double price, uint32_t quantity,
                              uint64_t timestamp) {
    if (symbol_id >= num_symbols_) return;
    
    begin_write(symbol_id);
    
    auto& state = entries_[symbol_id].state;
    state.best_bid = price;
    state.bid_quantity = quantity;
    state.last_update_time = timestamp;
    state.update_count++;
    
    end_write(symbol_id);
}

void SymbolCache::update_ask(uint16_t symbol_id, double price, uint32_t quantity,
                              uint64_t timestamp) {
    if (symbol_id >= num_symbols_) return;
    
    begin_write(symbol_id);
    
    auto& state = entries_[symbol_id].state;
    state.best_ask = price;
    state.ask_quantity = quantity;
    state.last_update_time = timestamp;
    state.update_count++;
    
    end_write(symbol_id);
}

MarketState SymbolCache::get_snapshot(uint16_t symbol_id) const {
    if (symbol_id >= num_symbols_) return MarketState{};
    
    const auto& entry = entries_[symbol_id];
    MarketState snapshot;
    
    // SeqLock read - retry if sequence changes during read
    uint64_t seq1, seq2;
    do {
        seq1 = entry.sequence.load(std::memory_order_acquire);
        
        // If odd, write in progress - spin
        while (seq1 & 1) {
            seq1 = entry.sequence.load(std::memory_order_acquire);
        }
        
        std::atomic_thread_fence(std::memory_order_acquire);
        snapshot = entry.state;
        std::atomic_thread_fence(std::memory_order_acquire);
        
        seq2 = entry.sequence.load(std::memory_order_acquire);
    } while (seq1 != seq2);
    
    return snapshot;
}

void SymbolCache::get_top_symbols(uint16_t* out_ids, MarketState* out_states,
                                   size_t count) const {
    // Collect all symbols with their update counts
    std::vector<std::pair<uint64_t, uint16_t>> symbols;
    symbols.reserve(num_symbols_);
    
    for (uint16_t i = 0; i < num_symbols_; ++i) {
        MarketState state = get_snapshot(i);
        if (state.update_count > 0) {
            symbols.emplace_back(state.update_count, i);
        }
    }
    
    // Sort by update count descending
    std::partial_sort(symbols.begin(), 
                      symbols.begin() + std::min(count, symbols.size()),
                      symbols.end(),
                      std::greater<std::pair<uint64_t, uint16_t>>());
    
    // Fill output arrays
    size_t n = std::min(count, symbols.size());
    for (size_t i = 0; i < n; ++i) {
        out_ids[i] = symbols[i].second;
        out_states[i] = get_snapshot(symbols[i].second);
    }
    
    // Zero out remaining slots
    for (size_t i = n; i < count; ++i) {
        out_ids[i] = 0;
        out_states[i] = MarketState{};
    }
}

uint64_t SymbolCache::get_total_updates() const {
    uint64_t total = 0;
    for (size_t i = 0; i < num_symbols_; ++i) {
        MarketState state = get_snapshot(i);
        total += state.update_count;
    }
    return total;
}

void SymbolCache::reset() {
    for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
        entries_[i].sequence.store(0, std::memory_order_relaxed);
        entries_[i].state = MarketState{};
    }
}

} // namespace mdf
