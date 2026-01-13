#include "tick_generator.h"
#include <cmath>
#include <cstring>

namespace mdf {

TickGenerator::TickGenerator(size_t num_symbols)
    : num_symbols_(num_symbols)
    , symbols_(num_symbols)
    , rng_(std::random_device{}())
    , symbol_dist_(0, static_cast<uint16_t>(num_symbols - 1)) {
    
    reset();
}

void TickGenerator::reset() {
    sequence_ = 0;
    
    // Initialize each symbol with random starting price and volatility
    std::uniform_real_distribution<double> price_dist(100.0, 5000.0);
    std::uniform_real_distribution<double> volatility_dist(0.01, 0.06);
    std::uniform_int_distribution<uint32_t> qty_dist(100, 10000);
    
    for (size_t i = 0; i < num_symbols_; ++i) {
        auto& sym = symbols_[i];
        sym.price = price_dist(rng_);
        sym.volatility = volatility_dist(rng_);
        sym.drift = 0.0;  // Neutral market
        
        // Initialize bid/ask with realistic spread
        update_spread(sym);
        
        sym.bid_quantity = qty_dist(rng_);
        sym.ask_quantity = qty_dist(rng_);
        sym.last_trade_qty = 0;
    }
}

double TickGenerator::generate_normal() {
    // Box-Muller transform - generates pairs of normal randoms
    if (has_spare_) {
        has_spare_ = false;
        return spare_;
    }
    
    double u1, u2;
    do {
        u1 = uniform_dist_(rng_);
        u2 = uniform_dist_(rng_);
    } while (u1 <= 0.0);  // log(0) is undefined
    
    double mag = std::sqrt(-2.0 * std::log(u1));
    double z0 = mag * std::cos(2.0 * M_PI * u2);
    double z1 = mag * std::sin(2.0 * M_PI * u2);
    
    spare_ = z1;
    has_spare_ = true;
    
    return z0;
}

void TickGenerator::update_price(SymbolState& symbol) {
    // GBM: dS = μ * S * dt + σ * S * dW
    double dW = generate_normal() * std::sqrt(dt_);
    double drift_term = symbol.drift * symbol.price * dt_;
    double volatility_term = symbol.volatility * symbol.price * dW;
    
    symbol.price += drift_term + volatility_term;
    
    // Ensure price stays positive and reasonable
    symbol.price = std::max(1.0, std::min(symbol.price, 100000.0));
    
    // Update bid/ask spread
    update_spread(symbol);
}

void TickGenerator::update_spread(SymbolState& symbol) {
    // Spread is 0.05% to 0.2% of price
    double spread_pct = 0.0005 + (uniform_dist_(rng_) * 0.0015);
    double half_spread = symbol.price * spread_pct / 2.0;
    
    symbol.bid_price = symbol.price - half_spread;
    symbol.ask_price = symbol.price + half_spread;
    
    // Round to 2 decimal places (paisa precision)
    symbol.bid_price = std::round(symbol.bid_price * 100.0) / 100.0;
    symbol.ask_price = std::round(symbol.ask_price * 100.0) / 100.0;
}

uint64_t TickGenerator::get_timestamp_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch());
    return static_cast<uint64_t>(ns.count());
}

void TickGenerator::generate_tick(uint8_t* out_buffer, size_t& out_size,
                                   uint16_t& out_symbol_id) {
    // Select random symbol
    out_symbol_id = symbol_dist_(rng_);
    generate_tick_for_symbol(out_symbol_id, out_buffer, out_size);
}

void TickGenerator::generate_tick_for_symbol(uint16_t symbol_id,
                                              uint8_t* out_buffer, 
                                              size_t& out_size) {
    if (symbol_id >= num_symbols_) {
        out_size = 0;
        return;
    }
    
    auto& symbol = symbols_[symbol_id];
    
    // Update price using GBM
    update_price(symbol);
    
    // 70% quote, 30% trade
    bool is_trade = uniform_dist_(rng_) < 0.3;
    
    MessageHeader header;
    header.sequence_number = ++sequence_;
    header.timestamp_ns = get_timestamp_ns();
    header.symbol_id = symbol_id;
    
    if (is_trade) {
        // Generate trade message
        header.message_type = static_cast<uint16_t>(MessageType::TRADE);
        
        TradePayload payload;
        // Trade at mid-price with some randomness
        double trade_offset = (uniform_dist_(rng_) - 0.5) * 
                              (symbol.ask_price - symbol.bid_price);
        payload.price = std::round((symbol.price + trade_offset) * 100.0) / 100.0;
        payload.quantity = static_cast<uint32_t>(100 + uniform_dist_(rng_) * 9900);
        
        symbol.last_trade_qty = payload.quantity;
        
        // Copy to buffer
        std::memcpy(out_buffer, &header, sizeof(header));
        std::memcpy(out_buffer + sizeof(header), &payload, sizeof(payload));
        
        // Calculate and append checksum
        size_t msg_size = sizeof(header) + sizeof(payload);
        uint32_t checksum = calculate_checksum(out_buffer, msg_size);
        std::memcpy(out_buffer + msg_size, &checksum, sizeof(checksum));
        
        out_size = TRADE_MSG_SIZE;
    } else {
        // Generate quote message
        header.message_type = static_cast<uint16_t>(MessageType::QUOTE);
        
        QuotePayload payload;
        payload.bid_price = symbol.bid_price;
        payload.ask_price = symbol.ask_price;
        
        // Randomize quantities
        std::uniform_int_distribution<uint32_t> qty_change(-500, 500);
        int32_t bid_change = qty_change(rng_);
        int32_t ask_change = qty_change(rng_);
        
        symbol.bid_quantity = static_cast<uint32_t>(
            std::max(100, static_cast<int32_t>(symbol.bid_quantity) + bid_change));
        symbol.ask_quantity = static_cast<uint32_t>(
            std::max(100, static_cast<int32_t>(symbol.ask_quantity) + ask_change));
        
        payload.bid_quantity = symbol.bid_quantity;
        payload.ask_quantity = symbol.ask_quantity;
        
        // Copy to buffer
        std::memcpy(out_buffer, &header, sizeof(header));
        std::memcpy(out_buffer + sizeof(header), &payload, sizeof(payload));
        
        // Calculate and append checksum
        size_t msg_size = sizeof(header) + sizeof(payload);
        uint32_t checksum = calculate_checksum(out_buffer, msg_size);
        std::memcpy(out_buffer + msg_size, &checksum, sizeof(checksum));
        
        out_size = QUOTE_MSG_SIZE;
    }
}

void TickGenerator::generate_heartbeat(uint8_t* out_buffer, size_t& out_size) {
    MessageHeader header;
    header.message_type = static_cast<uint16_t>(MessageType::HEARTBEAT);
    header.sequence_number = ++sequence_;
    header.timestamp_ns = get_timestamp_ns();
    header.symbol_id = 0;
    
    std::memcpy(out_buffer, &header, sizeof(header));
    
    uint32_t checksum = calculate_checksum(out_buffer, sizeof(header));
    std::memcpy(out_buffer + sizeof(header), &checksum, sizeof(checksum));
    
    out_size = HEARTBEAT_MSG_SIZE;
}

const SymbolState& TickGenerator::get_symbol_state(uint16_t symbol_id) const {
    static SymbolState empty{};
    if (symbol_id >= num_symbols_) return empty;
    return symbols_[symbol_id];
}

void TickGenerator::set_market_condition(MarketCondition condition) {
    market_condition_ = condition;
    
    double drift = 0.0;
    switch (condition) {
        case MarketCondition::NEUTRAL: drift = 0.0; break;
        case MarketCondition::BULLISH: drift = 0.05; break;
        case MarketCondition::BEARISH: drift = -0.05; break;
    }
    
    for (auto& symbol : symbols_) {
        symbol.drift = drift;
    }
}

} // namespace mdf
