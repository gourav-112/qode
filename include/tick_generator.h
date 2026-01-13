#pragma once

#include <cstdint>
#include <vector>
#include <random>
#include <chrono>
#include "protocol.h"

namespace mdf {

// Symbol state for tick generation
struct SymbolState {
    double price;
    double bid_price;
    double ask_price;
    double volatility;      // σ parameter
    double drift;           // μ parameter
    uint32_t bid_quantity;
    uint32_t ask_quantity;
    uint32_t last_trade_qty;
};

// Geometric Brownian Motion tick generator
// Implements: dS = μ * S * dt + σ * S * dW
class TickGenerator {
public:
    // Market conditions
    enum class MarketCondition {
        NEUTRAL,    // μ = 0.0
        BULLISH,    // μ = +0.05
        BEARISH     // μ = -0.05
    };
    
    TickGenerator(size_t num_symbols = 100);
    
    // Generate a tick for a random symbol (70% quote, 30% trade)
    // Returns the message bytes and sets out_size
    void generate_tick(uint8_t* out_buffer, size_t& out_size, 
                       uint16_t& out_symbol_id);
    
    // Generate tick for specific symbol
    void generate_tick_for_symbol(uint16_t symbol_id,
                                   uint8_t* out_buffer, size_t& out_size);
    
    // Generate heartbeat message
    void generate_heartbeat(uint8_t* out_buffer, size_t& out_size);
    
    // Get current state for a symbol
    const SymbolState& get_symbol_state(uint16_t symbol_id) const;
    
    // Configuration
    void set_market_condition(MarketCondition condition);
    void set_time_step(double dt) { dt_ = dt; }
    
    // Reset all symbol prices to initial values
    void reset();
    
    // Current sequence number
    uint32_t current_sequence() const { return sequence_; }
    
private:
    size_t num_symbols_;
    std::vector<SymbolState> symbols_;
    uint32_t sequence_ = 0;
    double dt_ = 0.001;  // Time step (1ms default)
    MarketCondition market_condition_ = MarketCondition::NEUTRAL;
    
    // Random number generation
    std::mt19937 rng_;
    std::normal_distribution<double> normal_dist_{0.0, 1.0};
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    std::uniform_int_distribution<uint16_t> symbol_dist_;
    
    // Box-Muller transform state (for optimization)
    bool has_spare_ = false;
    double spare_ = 0.0;
    
    // Generate normal random using Box-Muller
    double generate_normal();
    
    // Update price using GBM
    void update_price(SymbolState& symbol);
    
    // Update bid-ask spread based on price
    void update_spread(SymbolState& symbol);
    
    // Get current timestamp in nanoseconds
    uint64_t get_timestamp_ns() const;
};

} // namespace mdf
