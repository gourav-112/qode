#include "exchange_simulator.h"
#include "protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>

#ifdef USE_KQUEUE
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif

namespace mdf {

ExchangeSimulator::ExchangeSimulator(uint16_t port, size_t num_symbols)
    : port_(port)
    , tick_gen_(std::make_unique<TickGenerator>(num_symbols))
    , client_mgr_(std::make_unique<ClientManager>()) {
}

ExchangeSimulator::~ExchangeSimulator() {
    stop();
    
    if (event_fd_ >= 0) {
        ::close(event_fd_);
    }
    if (server_fd_ >= 0) {
        ::close(server_fd_);
    }
}

bool ExchangeSimulator::init_server_socket() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // Bind
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Listen
    if (listen(server_fd_, 128) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

bool ExchangeSimulator::init_event_system() {
#ifdef USE_KQUEUE
    event_fd_ = kqueue();
    if (event_fd_ < 0) {
        std::cerr << "Failed to create kqueue: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Add server socket to kqueue
    struct kevent ev;
    EV_SET(&ev, server_fd_, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(event_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
        std::cerr << "Failed to add server to kqueue: " << strerror(errno) << std::endl;
        return false;
    }
#else
    event_fd_ = epoll_create1(0);
    if (event_fd_ < 0) {
        std::cerr << "Failed to create epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Add server socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered
    ev.data.fd = server_fd_;
    if (epoll_ctl(event_fd_, EPOLL_CTL_ADD, server_fd_, &ev) < 0) {
        std::cerr << "Failed to add server to epoll: " << strerror(errno) << std::endl;
        return false;
    }
#endif
    
    return true;
}

void ExchangeSimulator::start() {
    if (!init_server_socket()) {
        return;
    }
    
    if (!init_event_system()) {
        return;
    }
    
    running_.store(true);
    std::cout << "Exchange Simulator started on port " << port_ << std::endl;
    std::cout << "Generating ticks for " << MAX_SYMBOLS << " symbols at " 
              << tick_rate_ << " msgs/sec" << std::endl;
}

void ExchangeSimulator::run() {
    if (!running_.load()) {
        start();
        if (!running_.load()) {
            return;
        }
    }
    
    // Calculate tick interval
    auto tick_interval = std::chrono::nanoseconds(1000000000 / tick_rate_);
    auto last_tick = std::chrono::steady_clock::now();
    auto last_heartbeat = last_tick;
    
    uint8_t msg_buffer[QUOTE_MSG_SIZE];  // Largest message type
    
    while (running_.load()) {
        // Check for events with short timeout
#ifdef USE_KQUEUE
        struct kevent events[64];
        struct timespec timeout = {0, 1000000};  // 1ms
        int nev = kevent(event_fd_, nullptr, 0, events, 64, &timeout);
        
        for (int i = 0; i < nev; ++i) {
            int fd = static_cast<int>(events[i].ident);
            
            if (fd == server_fd_) {
                handle_new_connection();
            } else {
                bool is_error = (events[i].flags & EV_EOF) || (events[i].flags & EV_ERROR);
                bool is_read = (events[i].filter == EVFILT_READ);
                handle_client_event(fd, is_read, is_error);
            }
        }
#else
        struct epoll_event events[64];
        int nev = epoll_wait(event_fd_, events, 64, 1);  // 1ms timeout
        
        for (int i = 0; i < nev; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == server_fd_) {
                handle_new_connection();
            } else {
                bool is_error = (events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP);
                bool is_read = (events[i].events & EPOLLIN);
                handle_client_event(fd, is_read, is_error);
            }
        }
#endif
        
        // Generate and broadcast ticks
        auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= tick_interval) {
            // Generate multiple ticks if we're behind
            int ticks_to_generate = std::min(100, 
                static_cast<int>((now - last_tick).count() / tick_interval.count()));
            
            for (int i = 0; i < ticks_to_generate && client_mgr_->client_count() > 0; ++i) {
                generate_and_broadcast_tick();
            }
            
            last_tick = now;
        }
        
        // Send heartbeat every second
        if (now - last_heartbeat >= std::chrono::seconds(1)) {
            send_heartbeat();
            last_heartbeat = now;
        }
    }
}

void ExchangeSimulator::stop() {
    running_.store(false);
}

void ExchangeSimulator::handle_new_connection() {
    // Accept all pending connections (edge-triggered mode)
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, 
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &addr_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more pending connections
            }
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            break;
        }
        
        // Get client info
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(client_addr.sin_port);
        
        // Add to event system
#ifdef USE_KQUEUE
        struct kevent ev;
        EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
        kevent(event_fd_, &ev, 1, nullptr, 0, nullptr);
#else
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = client_fd;
        epoll_ctl(event_fd_, EPOLL_CTL_ADD, client_fd, &ev);
#endif
        
        // Add to client manager
        client_mgr_->add_client(client_fd, ip_str, port);
        
        std::cout << "Client connected: " << ip_str << ":" << port << std::endl;
    }
}

void ExchangeSimulator::handle_client_event(int client_fd, bool is_read, bool is_error) {
    if (is_error) {
        handle_client_disconnect(client_fd, "Connection error");
        return;
    }
    
    if (is_read) {
        // Try to read subscription request
        process_subscription(client_fd);
    }
}

bool ExchangeSimulator::process_subscription(int client_fd) {
    uint8_t buffer[1024];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            handle_client_disconnect(client_fd, "Read failed");
        }
        return false;
    }
    
    // Check for subscription command
    if (buffer[0] == SUBSCRIBE_CMD && n >= 3) {
        uint16_t count;
        std::memcpy(&count, buffer + 1, 2);
        
        if (n >= static_cast<ssize_t>(3 + count * 2)) {
            std::vector<uint16_t> symbols(count);
            for (uint16_t i = 0; i < count; ++i) {
                std::memcpy(&symbols[i], buffer + 3 + i * 2, 2);
            }
            client_mgr_->handle_subscription(client_fd, symbols.data(), count);
            std::cout << "Client subscribed to " << count << " symbols" << std::endl;
            return true;
        }
    }
    
    return false;
}

void ExchangeSimulator::generate_and_broadcast_tick() {
    uint8_t buffer[QUOTE_MSG_SIZE];
    size_t size;
    uint16_t symbol_id;
    
    // Fault injection - skip sequence numbers occasionally
    if (fault_injection_) {
        if (++fault_skip_counter_ % 100 == 0) {
            tick_gen_->generate_tick(buffer, size, symbol_id);  // Skip this one
        }
    }
    
    tick_gen_->generate_tick(buffer, size, symbol_id);
    
    size_t sent = client_mgr_->broadcast(buffer, size, symbol_id);
    messages_sent_.fetch_add(sent, std::memory_order_relaxed);
    bytes_sent_.fetch_add(sent * size, std::memory_order_relaxed);
}

void ExchangeSimulator::send_heartbeat() {
    uint8_t buffer[HEARTBEAT_MSG_SIZE];
    size_t size;
    tick_gen_->generate_heartbeat(buffer, size);
    
    // Broadcast heartbeat to all (symbol_id 0 matches all)
    for (int fd : client_mgr_->get_all_client_fds()) {
        client_mgr_->send_to_client(fd, buffer, size);
    }
}

void ExchangeSimulator::handle_client_disconnect(int client_fd, const std::string& reason) {
    const ClientConnection* client = client_mgr_->get_client(client_fd);
    if (client) {
        std::cout << "Client disconnected: " << client->address << ":" << client->port;
        if (!reason.empty()) {
            std::cout << " (" << reason << ")";
        }
        std::cout << std::endl;
        
        if (disconnect_cb_) {
            disconnect_cb_(client_fd, reason);
        }
    }
    
    // Remove from event system
#ifdef USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, client_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(event_fd_, &ev, 1, nullptr, 0, nullptr);
#else
    epoll_ctl(event_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
#endif
    
    client_mgr_->remove_client(client_fd);
}

void ExchangeSimulator::set_tick_rate(uint32_t ticks_per_second) {
    tick_rate_ = std::max(1u, std::min(ticks_per_second, 500000u));
}

void ExchangeSimulator::enable_fault_injection(bool enable) {
    fault_injection_ = enable;
    fault_skip_counter_ = 0;
}

void ExchangeSimulator::set_market_condition(TickGenerator::MarketCondition condition) {
    tick_gen_->set_market_condition(condition);
}

size_t ExchangeSimulator::client_count() const {
    return client_mgr_->client_count();
}

} // namespace mdf
