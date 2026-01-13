#include "feed_handler.h"
#include <iostream>
#include <chrono>

namespace mdf {

FeedHandler::FeedHandler()
    : socket_(std::make_unique<MarketDataSocket>())
    , parser_(std::make_unique<MessageParser>())
    , cache_(std::make_unique<SymbolCache>())
    , visualizer_(std::make_unique<Visualizer>())
    , latency_tracker_(std::make_unique<LatencyTracker>())
    , recv_buffer_(new uint8_t[RECV_BUFFER_SIZE]) {
    
    // Set up parser callbacks
    parser_->set_trade_callback([this](const MessageHeader& h, const TradePayload& p) {
        on_trade(h, p);
    });
    
    parser_->set_quote_callback([this](const MessageHeader& h, const QuotePayload& p) {
        on_quote(h, p);
    });
    
    parser_->set_heartbeat_callback([this](const MessageHeader& h) {
        on_heartbeat(h);
    });
    
    parser_->set_gap_callback([this](uint32_t expected, uint32_t received) {
        on_sequence_gap(expected, received);
    });
    
    // Set up visualizer
    visualizer_->set_cache(cache_.get());
    visualizer_->set_latency_tracker(latency_tracker_.get());
}

FeedHandler::~FeedHandler() {
    stop();
}

void FeedHandler::configure(const FeedHandlerConfig& config) {
    config_ = config;
}

bool FeedHandler::start() {
    // Connect to server
    std::cout << "Connecting to " << config_.host << ":" << config_.port << "...\n";
    
    if (!socket_->connect(config_.host, config_.port, config_.connect_timeout_ms)) {
        std::cerr << "Failed to connect: " << socket_->last_error() << "\n";
        return false;
    }
    
    std::cout << "Connected!\n";
    
    // Send subscription if specified
    if (!config_.subscribe_symbols.empty()) {
        if (!socket_->send_subscription(config_.subscribe_symbols)) {
            std::cerr << "Failed to send subscription\n";
            return false;
        }
        std::cout << "Subscribed to " << config_.subscribe_symbols.size() << " symbols\n";
    }
    
    // Start visualizer if enabled
    if (config_.enable_visualization) {
        visualizer_->set_connected(true, config_.host + ":" + std::to_string(config_.port));
        visualizer_->start();
    }
    
    running_.store(true);
    return true;
}

void FeedHandler::run() {
    if (!running_.load()) {
        if (!start()) {
            return;
        }
    }
    
    while (running_.load()) {
        // Check for user input (quit/reset)
        if (config_.enable_visualization && visualizer_->process_input()) {
            stop();
            break;
        }
        
        // Wait for data with timeout
        int result = socket_->wait_for_data(100);  // 100ms timeout
        
        if (result < 0) {
            // Error - try to reconnect
            if (config_.enable_visualization) {
                visualizer_->set_connected(false);
            }
            
            if (config_.auto_reconnect) {
                std::cerr << "Connection lost, attempting reconnect...\n";
                if (socket_->reconnect()) {
                    std::cout << "Reconnected!\n";
                    if (config_.enable_visualization) {
                        visualizer_->set_connected(true);
                    }
                    if (!config_.subscribe_symbols.empty()) {
                        socket_->send_subscription(config_.subscribe_symbols);
                    }
                } else if (socket_->reconnect_count() >= MarketDataSocket::MAX_RETRY_COUNT) {
                    std::cerr << "Failed to reconnect after " 
                              << socket_->reconnect_count() << " attempts\n";
                    stop();
                    break;
                }
            } else {
                stop();
                break;
            }
            continue;
        }
        
        if (result > 0) {
            process_data();
        }
        
        // Update visualizer
        if (config_.enable_visualization) {
            visualizer_->update_stats(
                messages_received_.load(),
                bytes_received_.load(),
                parser_->sequence_gaps()
            );
        }
    }
}

void FeedHandler::process_data() {
    // Receive data in a loop (edge-triggered mode)
    while (running_.load()) {
        ssize_t n = socket_->receive(recv_buffer_.get(), RECV_BUFFER_SIZE);
        
        if (n < 0) {
            // Error - will be handled in main loop
            return;
        }
        
        if (n == 0) {
            // No more data available
            break;
        }
        
        // Feed to parser
        parser_->append_data(recv_buffer_.get(), static_cast<size_t>(n));
        bytes_received_.fetch_add(n, std::memory_order_relaxed);
        
        // Parse all complete messages
        size_t parsed = parser_->parse_messages();
        messages_received_.fetch_add(parsed, std::memory_order_relaxed);
    }
}

void FeedHandler::on_trade(const MessageHeader& header, const TradePayload& payload) {
    // Record latency (time from message timestamp to now)
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    if (now_ns > header.timestamp_ns) {
        latency_tracker_->record(now_ns - header.timestamp_ns);
    }
    
    // Update cache
    cache_->update_trade(header.symbol_id, payload.price, payload.quantity,
                         header.timestamp_ns);
}

void FeedHandler::on_quote(const MessageHeader& header, const QuotePayload& payload) {
    // Record latency
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    if (now_ns > header.timestamp_ns) {
        latency_tracker_->record(now_ns - header.timestamp_ns);
    }
    
    // Update cache
    cache_->update_quote(header.symbol_id, 
                         payload.bid_price, payload.bid_quantity,
                         payload.ask_price, payload.ask_quantity,
                         header.timestamp_ns);
}

void FeedHandler::on_heartbeat(const MessageHeader& header) {
    // Just update timestamp for connection monitoring
    (void)header;
}

void FeedHandler::on_sequence_gap(uint32_t expected, uint32_t received) {
    // Log for debugging (in real system, might request retransmission)
    // Note: Logging disabled during visualization to avoid screen clutter
    if (!config_.enable_visualization) {
        std::cerr << "Sequence gap detected: expected " << expected 
                  << ", received " << received << "\n";
    }
}

void FeedHandler::stop() {
    running_.store(false);
    
    if (config_.enable_visualization) {
        visualizer_->stop();
    }
    
    socket_->disconnect();
}

MarketState FeedHandler::get_market_state(uint16_t symbol_id) const {
    return cache_->get_snapshot(symbol_id);
}

uint64_t FeedHandler::messages_received() const {
    return messages_received_.load();
}

uint64_t FeedHandler::bytes_received() const {
    return bytes_received_.load();
}

uint64_t FeedHandler::sequence_gaps() const {
    return parser_->sequence_gaps();
}

LatencyStats FeedHandler::get_latency_stats() const {
    return latency_tracker_->get_stats();
}

bool FeedHandler::is_connected() const {
    return socket_->is_connected();
}

bool FeedHandler::reconnect() {
    return socket_->reconnect();
}

} // namespace mdf
