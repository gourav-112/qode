#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>

namespace mdf {

class MarketDataSocket {
public:
    static constexpr size_t DEFAULT_RECV_BUFFER = 4 * 1024 * 1024;  // 4MB
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;
    static constexpr int MAX_RETRY_COUNT = 5;
    
    MarketDataSocket();
    ~MarketDataSocket();
    
    // Connect to exchange feed with retry logic
    bool connect(const std::string& host, uint16_t port,
                 uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);
    
    // Non-blocking receive into pre-allocated buffer
    // Returns bytes received, 0 for no data, -1 for error
    ssize_t receive(void* buffer, size_t max_len);
    
    // Send subscription request
    bool send_subscription(const std::vector<uint16_t>& symbol_ids);
    
    // Connection management
    bool is_connected() const { return connected_.load(); }
    void disconnect();
    
    // Attempt reconnection with exponential backoff
    bool reconnect();
    
    // Socket options for low latency
    bool set_tcp_nodelay(bool enable);
    bool set_recv_buffer_size(size_t bytes);
    bool set_socket_priority(int priority);
    
    // Wait for data with timeout (uses kqueue/epoll)
    // Returns: 1 = data available, 0 = timeout, -1 = error
    int wait_for_data(uint32_t timeout_ms);
    
    // Statistics
    uint64_t bytes_received() const { return bytes_received_.load(); }
    uint64_t recv_calls() const { return recv_calls_.load(); }
    uint32_t reconnect_count() const { return reconnect_count_; }
    
    // Get last error message
    const std::string& last_error() const { return last_error_; }
    
    // Non-copyable
    MarketDataSocket(const MarketDataSocket&) = delete;
    MarketDataSocket& operator=(const MarketDataSocket&) = delete;
    
private:
    int fd_ = -1;
    int event_fd_ = -1;  // kqueue/epoll fd
    
    std::string host_;
    uint16_t port_ = 0;
    uint32_t timeout_ms_ = DEFAULT_TIMEOUT_MS;
    
    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> recv_calls_{0};
    
    uint32_t reconnect_count_ = 0;
    uint32_t current_backoff_ms_ = 100;  // Start at 100ms
    std::string last_error_;
    
    // Initialize event system
    bool init_event_system();
    
    // Update event system with socket
    bool register_socket();
    
    // Internal connect with timeout
    bool do_connect();
    
    // Configure socket options
    void configure_socket();
};

} // namespace mdf
