#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include "socket.h"
#include "parser.h"
#include "cache.h"
#include "visualizer.h"
#include "latency_tracker.h"

namespace mdf {

// Feed handler configuration
struct FeedHandlerConfig {
    std::string host = "localhost";
    uint16_t port = DEFAULT_PORT;
    uint32_t connect_timeout_ms = 5000;
    size_t num_symbols = MAX_SYMBOLS;
    bool auto_reconnect = true;
    bool enable_visualization = true;
    std::vector<uint16_t> subscribe_symbols;  // Empty = subscribe all
};

// Feed handler - main client class
class FeedHandler {
public:
    FeedHandler();
    ~FeedHandler();
    
    // Configure handler
    void configure(const FeedHandlerConfig& config);
    
    // Start handler (connects and begins receiving)
    bool start();
    
    // Run event loop (blocking)
    void run();
    
    // Stop handler
    void stop();
    
    // Get market state for symbol
    MarketState get_market_state(uint16_t symbol_id) const;
    
    // Get statistics
    uint64_t messages_received() const;
    uint64_t bytes_received() const;
    uint64_t sequence_gaps() const;
    LatencyStats get_latency_stats() const;
    
    // Check connection status
    bool is_connected() const;
    
    // Force reconnection
    bool reconnect();
    
    // Non-copyable
    FeedHandler(const FeedHandler&) = delete;
    FeedHandler& operator=(const FeedHandler&) = delete;
    
private:
    FeedHandlerConfig config_;
    
    std::unique_ptr<MarketDataSocket> socket_;
    std::unique_ptr<MessageParser> parser_;
    std::unique_ptr<SymbolCache> cache_;
    std::unique_ptr<Visualizer> visualizer_;
    std::unique_ptr<LatencyTracker> latency_tracker_;
    
    std::atomic<bool> running_{false};
    
    // Receive buffer (4MB aligned)
    static constexpr size_t RECV_BUFFER_SIZE = 4 * 1024 * 1024;
    std::unique_ptr<uint8_t[]> recv_buffer_;
    
    // Statistics
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_received_{0};
    
    // Process received data
    void process_data();
    
    // Message callbacks
    void on_trade(const MessageHeader& header, const TradePayload& payload);
    void on_quote(const MessageHeader& header, const QuotePayload& payload);
    void on_heartbeat(const MessageHeader& header);
    void on_sequence_gap(uint32_t expected, uint32_t received);
};

} // namespace mdf
