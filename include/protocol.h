#pragma once

#include <cstdint>
#include <cstring>

namespace mdf {

// Message Types
enum class MessageType : uint16_t {
    TRADE = 0x01,
    QUOTE = 0x02,
    HEARTBEAT = 0x03
};

// Subscription command
constexpr uint8_t SUBSCRIBE_CMD = 0xFF;

// Header size
constexpr size_t HEADER_SIZE = 16;
constexpr size_t TRADE_PAYLOAD_SIZE = 12;    // Price(8) + Quantity(4)
constexpr size_t QUOTE_PAYLOAD_SIZE = 24;    // BidPrice(8) + BidQty(4) + AskPrice(8) + AskQty(4)
constexpr size_t CHECKSUM_SIZE = 4;

constexpr size_t TRADE_MSG_SIZE = HEADER_SIZE + TRADE_PAYLOAD_SIZE + CHECKSUM_SIZE;
constexpr size_t QUOTE_MSG_SIZE = HEADER_SIZE + QUOTE_PAYLOAD_SIZE + CHECKSUM_SIZE;
constexpr size_t HEARTBEAT_MSG_SIZE = HEADER_SIZE + CHECKSUM_SIZE;

constexpr size_t MAX_SYMBOLS = 500;
constexpr uint16_t DEFAULT_PORT = 9876;

// Message Header (16 bytes)
#pragma pack(push, 1)
struct MessageHeader {
    uint16_t message_type;      // 0x01=Trade, 0x02=Quote, 0x03=Heartbeat
    uint32_t sequence_number;   // Monotonically increasing
    uint64_t timestamp_ns;      // Nanoseconds since epoch
    uint16_t symbol_id;         // 0-499 for 500 symbols
};

// Trade Payload (12 bytes)
struct TradePayload {
    double price;               // 8 bytes
    uint32_t quantity;          // 4 bytes
};

// Quote Payload (24 bytes)
struct QuotePayload {
    double bid_price;           // 8 bytes
    uint32_t bid_quantity;      // 4 bytes
    double ask_price;           // 8 bytes
    uint32_t ask_quantity;      // 4 bytes
};

// Complete Trade Message
struct TradeMessage {
    MessageHeader header;
    TradePayload payload;
    uint32_t checksum;
};

// Complete Quote Message
struct QuoteMessage {
    MessageHeader header;
    QuotePayload payload;
    uint32_t checksum;
};

// Heartbeat Message
struct HeartbeatMessage {
    MessageHeader header;
    uint32_t checksum;
};

// Subscription Request
struct SubscriptionRequest {
    uint8_t command;            // 0xFF
    uint16_t symbol_count;
    // Followed by symbol_count * uint16_t symbol_ids
};
#pragma pack(pop)

// Calculate XOR checksum of bytes
inline uint32_t calculate_checksum(const void* data, size_t len) {
    uint32_t checksum = 0;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    // XOR 4 bytes at a time
    size_t i = 0;
    for (; i + 4 <= len; i += 4) {
        uint32_t word;
        std::memcpy(&word, bytes + i, 4);
        checksum ^= word;
    }
    
    // Handle remaining bytes
    for (; i < len; i++) {
        checksum ^= static_cast<uint32_t>(bytes[i]) << ((i % 4) * 8);
    }
    
    return checksum;
}

// Get message size by type
inline size_t get_message_size(MessageType type) {
    switch (type) {
        case MessageType::TRADE: return TRADE_MSG_SIZE;
        case MessageType::QUOTE: return QUOTE_MSG_SIZE;
        case MessageType::HEARTBEAT: return HEARTBEAT_MSG_SIZE;
        default: return 0;
    }
}

// Symbol names for display (sample NSE stocks)
inline const char* get_symbol_name(uint16_t symbol_id) {
    static const char* symbols[] = {
        "RELIANCE", "TCS", "INFY", "HDFC", "ICICIBANK",
        "HDFCBANK", "SBIN", "BHARTIARTL", "ITC", "KOTAKBANK",
        "LT", "HINDUNILVR", "AXISBANK", "BAJFINANCE", "MARUTI",
        "ASIANPAINT", "TITAN", "SUNPHARMA", "ULTRACEMCO", "WIPRO",
        "HCLTECH", "TECHM", "POWERGRID", "NTPC", "ONGC",
        "TATASTEEL", "JSWSTEEL", "COALINDIA", "BPCL", "IOC",
        "GRASIM", "ADANIPORTS", "DRREDDY", "DIVISLAB", "CIPLA",
        "APOLLOHOSP", "EICHERMOT", "HEROMOTOCO", "BAJAJ-AUTO", "M&M",
        "TATAMOTORS", "NESTLEIND", "BRITANNIA", "DABUR", "GODREJCP",
        "PIDILITIND", "BERGER", "HAVELLS", "VOLTAS", "BLUESTAR"
        // Add more as needed, will use generic names for rest
    };
    
    constexpr size_t num_named = sizeof(symbols) / sizeof(symbols[0]);
    if (symbol_id < num_named) {
        return symbols[symbol_id];
    }
    
    static char buffer[16];
    snprintf(buffer, sizeof(buffer), "SYM%03u", symbol_id);
    return buffer;
}

} // namespace mdf
