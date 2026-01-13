#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <functional>
#include "tick_generator.h"
#include "client_manager.h"

namespace mdf {

class ExchangeSimulator {
public:
    // Initialize with port and symbol count
    ExchangeSimulator(uint16_t port, size_t num_symbols = 100);
    ~ExchangeSimulator();
    
    // Start accepting connections (blocking)
    void start();
    
    // Event loop - run tick generation and message broadcast
    void run();
    
    // Stop the simulator
    void stop();
    
    // Configuration
    void set_tick_rate(uint32_t ticks_per_second);
    void enable_fault_injection(bool enable);
    void set_market_condition(TickGenerator::MarketCondition condition);
    
    // Statistics
    uint64_t messages_sent() const { return messages_sent_.load(); }
    uint64_t total_bytes_sent() const { return bytes_sent_.load(); }
    size_t client_count() const;
    uint32_t current_tick_rate() const { return tick_rate_; }
    
    // Callbacks for external handling
    using DisconnectCallback = std::function<void(int fd, const std::string& reason)>;
    void set_disconnect_callback(DisconnectCallback cb) { disconnect_cb_ = cb; }
    
    // Non-copyable
    ExchangeSimulator(const ExchangeSimulator&) = delete;
    ExchangeSimulator& operator=(const ExchangeSimulator&) = delete;
    
private:
    uint16_t port_;
    int server_fd_ = -1;
    int event_fd_ = -1;  // kqueue or epoll fd
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    
    uint32_t tick_rate_ = 100000;  // 100K msgs/sec default
    bool fault_injection_ = false;
    uint32_t fault_skip_counter_ = 0;
    
    std::unique_ptr<TickGenerator> tick_gen_;
    std::unique_ptr<ClientManager> client_mgr_;
    
    DisconnectCallback disconnect_cb_;
    
    // Initialize server socket
    bool init_server_socket();
    
    // Initialize event notification (kqueue/epoll)
    bool init_event_system();
    
    // Accept new client connections
    void handle_new_connection();
    
    // Handle client events (data, disconnect)
    void handle_client_event(int client_fd, bool is_read, bool is_error);
    
    // Generate and broadcast tick
    void generate_and_broadcast_tick();
    
    // Send heartbeat to all clients
    void send_heartbeat();
    
    // Handle client disconnection
    void handle_client_disconnect(int client_fd, const std::string& reason = "");
    
    // Process subscription request from client
    bool process_subscription(int client_fd);
};

} // namespace mdf
