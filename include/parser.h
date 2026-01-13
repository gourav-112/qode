#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <atomic>
#include "protocol.h"

namespace mdf {

// Parser callback types
using TradeCallback = std::function<void(const MessageHeader&, const TradePayload&)>;
using QuoteCallback = std::function<void(const MessageHeader&, const QuotePayload&)>;
using HeartbeatCallback = std::function<void(const MessageHeader&)>;
using GapCallback = std::function<void(uint32_t expected, uint32_t received)>;

// Parse result
enum class ParseResult {
    SUCCESS,
    NEED_MORE_DATA,
    INVALID_MESSAGE,
    CHECKSUM_ERROR,
    SEQUENCE_GAP
};

// Zero-copy binary parser for market data messages
class MessageParser {
public:
    static constexpr size_t MAX_BUFFER_SIZE = 16 * 1024 * 1024;  // 16MB max buffer
    static constexpr size_t INITIAL_BUFFER_SIZE = 4 * 1024 * 1024;  // 4MB initial
    
    MessageParser();
    
    // Append raw data to internal buffer
    // Returns bytes consumed
    size_t append_data(const void* data, size_t len);
    
    // Parse all complete messages in buffer
    // Calls registered callbacks for each message
    // Returns number of messages parsed
    size_t parse_messages();
    
    // Parse single message from buffer
    // Returns parse result
    ParseResult parse_one();
    
    // Register callbacks
    void set_trade_callback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    void set_quote_callback(QuoteCallback cb) { quote_cb_ = std::move(cb); }
    void set_heartbeat_callback(HeartbeatCallback cb) { heartbeat_cb_ = std::move(cb); }
    void set_gap_callback(GapCallback cb) { gap_cb_ = std::move(cb); }
    
    // Statistics
    uint64_t messages_parsed() const { return messages_parsed_.load(); }
    uint64_t trades_parsed() const { return trades_parsed_.load(); }
    uint64_t quotes_parsed() const { return quotes_parsed_.load(); }
    uint64_t checksum_errors() const { return checksum_errors_.load(); }
    uint64_t sequence_gaps() const { return sequence_gaps_.load(); }
    uint64_t malformed_messages() const { return malformed_messages_.load(); }
    
    // Reset parser state
    void reset();
    
    // Get buffer usage
    size_t buffer_used() const { return write_pos_ - read_pos_; }
    size_t buffer_capacity() const { return buffer_.size(); }
    
    // Set expected sequence (for testing/resync)
    void set_expected_sequence(uint32_t seq) { expected_sequence_ = seq; }
    uint32_t expected_sequence() const { return expected_sequence_; }
    
private:
    std::vector<uint8_t> buffer_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    
    uint32_t expected_sequence_ = 0;
    bool first_message_ = true;
    
    // Callbacks
    TradeCallback trade_cb_;
    QuoteCallback quote_cb_;
    HeartbeatCallback heartbeat_cb_;
    GapCallback gap_cb_;
    
    // Statistics
    std::atomic<uint64_t> messages_parsed_{0};
    std::atomic<uint64_t> trades_parsed_{0};
    std::atomic<uint64_t> quotes_parsed_{0};
    std::atomic<uint64_t> checksum_errors_{0};
    std::atomic<uint64_t> sequence_gaps_{0};
    std::atomic<uint64_t> malformed_messages_{0};
    
    // Compact buffer if needed
    void compact_buffer();
    
    // Validate message checksum
    bool validate_checksum(const void* data, size_t msg_len);
    
    // Check and report sequence gap
    bool check_sequence(uint32_t received_seq);
};

} // namespace mdf
