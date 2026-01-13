#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <string>
#include <atomic>
#include <chrono>

namespace mdf {

// Client connection state
struct ClientConnection {
    int fd;
    std::string address;
    uint16_t port;
    
    // Subscription state
    std::unordered_set<uint16_t> subscribed_symbols;
    bool subscribe_all = true;  // By default, subscribe to all
    
    // Flow control
    size_t pending_bytes = 0;           // Bytes pending in send buffer
    size_t slow_consumer_count = 0;      // Count of slow consumer events
    bool is_slow = false;                // Currently slow
    
    // Statistics
    uint64_t messages_sent = 0;
    uint64_t bytes_sent = 0;
    std::chrono::steady_clock::time_point connect_time;
    std::chrono::steady_clock::time_point last_activity;
};

// Client manager - handles multiple client connections
class ClientManager {
public:
    static constexpr size_t MAX_SEND_BUFFER_SIZE = 4 * 1024 * 1024;  // 4MB
    static constexpr size_t SLOW_CONSUMER_THRESHOLD = 1 * 1024 * 1024;  // 1MB pending
    
    ClientManager();
    ~ClientManager();
    
    // Add new client connection
    bool add_client(int fd, const std::string& address, uint16_t port);
    
    // Remove client connection
    void remove_client(int fd);
    
    // Check if client exists
    bool has_client(int fd) const;
    
    // Get client info
    const ClientConnection* get_client(int fd) const;
    
    // Handle subscription request from client
    bool handle_subscription(int fd, const uint16_t* symbol_ids, size_t count);
    
    // Broadcast message to all subscribed clients
    // Returns number of clients that received the message
    size_t broadcast(const void* data, size_t len, uint16_t symbol_id);
    
    // Send to specific client (non-blocking)
    // Returns true if all bytes were sent, false if partial/blocked
    bool send_to_client(int fd, const void* data, size_t len);
    
    // Get list of all client FDs
    std::vector<int> get_all_client_fds() const;
    
    // Get slow clients for potential disconnection
    std::vector<int> get_slow_clients() const;
    
    // Mark client as slow (when send buffer fills up)
    void mark_slow_consumer(int fd);
    
    // Clear slow status (when buffer drains)
    void clear_slow_status(int fd);
    
    // Statistics
    size_t client_count() const { return clients_.size(); }
    uint64_t total_messages_sent() const { return total_messages_sent_.load(); }
    uint64_t total_bytes_sent() const { return total_bytes_sent_.load(); }
    
    // Set maximum pending bytes before considering client slow
    void set_slow_threshold(size_t bytes) { slow_threshold_ = bytes; }
    
private:
    std::unordered_map<int, ClientConnection> clients_;
    size_t slow_threshold_ = SLOW_CONSUMER_THRESHOLD;
    
    std::atomic<uint64_t> total_messages_sent_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    
    // Check socket send buffer status
    size_t get_pending_bytes(int fd) const;
};

} // namespace mdf
