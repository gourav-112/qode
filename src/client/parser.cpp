#include "parser.h"
#include <cstring>
#include <algorithm>

namespace mdf {

MessageParser::MessageParser()
    : buffer_(INITIAL_BUFFER_SIZE) {
}

size_t MessageParser::append_data(const void* data, size_t len) {
    if (len == 0) return 0;
    
    // Check if we need to compact or grow buffer
    size_t available = buffer_.size() - write_pos_;
    if (available < len) {
        compact_buffer();
        available = buffer_.size() - write_pos_;
        
        // Still not enough? Grow buffer
        if (available < len) {
            size_t new_size = std::min(buffer_.size() * 2, MAX_BUFFER_SIZE);
            if (new_size <= buffer_.size()) {
                // Can't grow anymore, drop oldest data
                malformed_messages_.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
            buffer_.resize(new_size);
        }
    }
    
    // Copy data to buffer
    std::memcpy(buffer_.data() + write_pos_, data, len);
    write_pos_ += len;
    
    return len;
}

void MessageParser::compact_buffer() {
    if (read_pos_ == 0) return;
    
    size_t used = write_pos_ - read_pos_;
    if (used > 0) {
        std::memmove(buffer_.data(), buffer_.data() + read_pos_, used);
    }
    write_pos_ = used;
    read_pos_ = 0;
}

size_t MessageParser::parse_messages() {
    size_t count = 0;
    
    while (true) {
        ParseResult result = parse_one();
        if (result == ParseResult::NEED_MORE_DATA) {
            break;
        }
        if (result == ParseResult::SUCCESS || result == ParseResult::SEQUENCE_GAP) {
            count++;
        }
        // Continue parsing even on errors (skip to next message attempt)
    }
    
    return count;
}

ParseResult MessageParser::parse_one() {
    size_t available = write_pos_ - read_pos_;
    
    // Need at least header to determine message type
    if (available < HEADER_SIZE) {
        return ParseResult::NEED_MORE_DATA;
    }
    
    const uint8_t* msg_start = buffer_.data() + read_pos_;
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(msg_start);
    
    // Validate message type and get expected size
    MessageType type = static_cast<MessageType>(header->message_type);
    size_t msg_size = get_message_size(type);
    
    if (msg_size == 0) {
        // Invalid message type - skip one byte and try again
        read_pos_++;
        malformed_messages_.fetch_add(1, std::memory_order_relaxed);
        return ParseResult::INVALID_MESSAGE;
    }
    
    // Check if we have complete message
    if (available < msg_size) {
        return ParseResult::NEED_MORE_DATA;
    }
    
    // Prevent buffer overflow with malicious large message
    if (msg_size > QUOTE_MSG_SIZE) {
        read_pos_++;
        malformed_messages_.fetch_add(1, std::memory_order_relaxed);
        return ParseResult::INVALID_MESSAGE;
    }
    
    // Validate checksum
    if (!validate_checksum(msg_start, msg_size)) {
        read_pos_++;
        checksum_errors_.fetch_add(1, std::memory_order_relaxed);
        return ParseResult::CHECKSUM_ERROR;
    }
    
    // Check sequence
    bool has_gap = !check_sequence(header->sequence_number);
    
    // Parse based on message type
    switch (type) {
        case MessageType::TRADE: {
            if (trade_cb_) {
                const TradePayload* payload = 
                    reinterpret_cast<const TradePayload*>(msg_start + HEADER_SIZE);
                trade_cb_(*header, *payload);
            }
            trades_parsed_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        case MessageType::QUOTE: {
            if (quote_cb_) {
                const QuotePayload* payload = 
                    reinterpret_cast<const QuotePayload*>(msg_start + HEADER_SIZE);
                quote_cb_(*header, *payload);
            }
            quotes_parsed_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        
        case MessageType::HEARTBEAT: {
            if (heartbeat_cb_) {
                heartbeat_cb_(*header);
            }
            break;
        }
        
        default:
            break;
    }
    
    messages_parsed_.fetch_add(1, std::memory_order_relaxed);
    read_pos_ += msg_size;
    
    return has_gap ? ParseResult::SEQUENCE_GAP : ParseResult::SUCCESS;
}

bool MessageParser::validate_checksum(const void* data, size_t msg_len) {
    if (msg_len < CHECKSUM_SIZE) return false;
    
    // Checksum is last 4 bytes
    size_t data_len = msg_len - CHECKSUM_SIZE;
    uint32_t expected = calculate_checksum(data, data_len);
    
    uint32_t received;
    std::memcpy(&received, static_cast<const uint8_t*>(data) + data_len, sizeof(received));
    
    return expected == received;
}

bool MessageParser::check_sequence(uint32_t received_seq) {
    if (first_message_) {
        first_message_ = false;
        expected_sequence_ = received_seq + 1;
        return true;
    }
    
    if (received_seq != expected_sequence_) {
        // Sequence gap detected
        if (gap_cb_) {
            gap_cb_(expected_sequence_, received_seq);
        }
        sequence_gaps_.fetch_add(1, std::memory_order_relaxed);
        expected_sequence_ = received_seq + 1;
        return false;
    }
    
    expected_sequence_ = received_seq + 1;
    return true;
}

void MessageParser::reset() {
    read_pos_ = 0;
    write_pos_ = 0;
    expected_sequence_ = 0;
    first_message_ = true;
    
    messages_parsed_.store(0, std::memory_order_relaxed);
    trades_parsed_.store(0, std::memory_order_relaxed);
    quotes_parsed_.store(0, std::memory_order_relaxed);
    checksum_errors_.store(0, std::memory_order_relaxed);
    sequence_gaps_.store(0, std::memory_order_relaxed);
    malformed_messages_.store(0, std::memory_order_relaxed);
}

} // namespace mdf
