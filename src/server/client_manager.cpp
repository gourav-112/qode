#include "client_manager.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace mdf {

ClientManager::ClientManager() = default;

ClientManager::~ClientManager() {
    // Close all client connections
    for (auto& [fd, client] : clients_) {
        ::close(fd);
    }
    clients_.clear();
}

bool ClientManager::add_client(int fd, const std::string& address, uint16_t port) {
    if (clients_.find(fd) != clients_.end()) {
        return false;  // Already exists
    }
    
    // Set socket options for low latency
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    // Set send buffer size
    int sendbuf = MAX_SEND_BUFFER_SIZE;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf));
    
    ClientConnection conn;
    conn.fd = fd;
    conn.address = address;
    conn.port = port;
    conn.connect_time = std::chrono::steady_clock::now();
    conn.last_activity = conn.connect_time;
    
    clients_[fd] = std::move(conn);
    return true;
}

void ClientManager::remove_client(int fd) {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        ::close(fd);
        clients_.erase(it);
    }
}

bool ClientManager::has_client(int fd) const {
    return clients_.find(fd) != clients_.end();
}

const ClientConnection* ClientManager::get_client(int fd) const {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool ClientManager::handle_subscription(int fd, const uint16_t* symbol_ids, size_t count) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }
    
    auto& client = it->second;
    client.subscribed_symbols.clear();
    client.subscribe_all = (count == 0);
    
    for (size_t i = 0; i < count; ++i) {
        client.subscribed_symbols.insert(symbol_ids[i]);
    }
    
    return true;
}

size_t ClientManager::broadcast(const void* data, size_t len, uint16_t symbol_id) {
    size_t count = 0;
    
    for (auto& [fd, client] : clients_) {
        // Skip slow consumers
        if (client.is_slow) {
            continue;
        }
        
        // Check subscription
        if (!client.subscribe_all && 
            client.subscribed_symbols.find(symbol_id) == client.subscribed_symbols.end()) {
            continue;
        }
        
        if (send_to_client(fd, data, len)) {
            ++count;
            client.messages_sent++;
            client.bytes_sent += len;
            client.last_activity = std::chrono::steady_clock::now();
        }
    }
    
    total_messages_sent_.fetch_add(count, std::memory_order_relaxed);
    total_bytes_sent_.fetch_add(count * len, std::memory_order_relaxed);
    
    return count;
}

bool ClientManager::send_to_client(int fd, const void* data, size_t len) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }
    
    auto& client = it->second;
    
    // Check pending bytes before sending
    size_t pending = get_pending_bytes(fd);
    client.pending_bytes = pending;
    
    if (pending > slow_threshold_) {
        mark_slow_consumer(fd);
        return false;
    }
    
    ssize_t sent = ::send(fd, data, len, MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Would block - mark as slow
            mark_slow_consumer(fd);
            return false;
        }
        // Other error - connection issue
        return false;
    }
    
    if (static_cast<size_t>(sent) < len) {
        // Partial send - mark as slow
        mark_slow_consumer(fd);
        return false;
    }
    
    // Full send - clear slow status if it was set
    if (client.is_slow && pending < slow_threshold_ / 2) {
        clear_slow_status(fd);
    }
    
    return true;
}

std::vector<int> ClientManager::get_all_client_fds() const {
    std::vector<int> fds;
    fds.reserve(clients_.size());
    for (const auto& [fd, client] : clients_) {
        fds.push_back(fd);
    }
    return fds;
}

std::vector<int> ClientManager::get_slow_clients() const {
    std::vector<int> slow;
    for (const auto& [fd, client] : clients_) {
        if (client.is_slow) {
            slow.push_back(fd);
        }
    }
    return slow;
}

void ClientManager::mark_slow_consumer(int fd) {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        it->second.is_slow = true;
        it->second.slow_consumer_count++;
    }
}

void ClientManager::clear_slow_status(int fd) {
    auto it = clients_.find(fd);
    if (it != clients_.end()) {
        it->second.is_slow = false;
    }
}

size_t ClientManager::get_pending_bytes(int fd) const {
    int pending = 0;
    if (ioctl(fd, TIOCOUTQ, &pending) < 0) {
        return 0;
    }
    return static_cast<size_t>(pending);
}

} // namespace mdf
