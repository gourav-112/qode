#include "socket.h"
#include "protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef USE_KQUEUE
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif

namespace mdf {

MarketDataSocket::MarketDataSocket() = default;

MarketDataSocket::~MarketDataSocket() {
    disconnect();
    
    if (event_fd_ >= 0) {
        ::close(event_fd_);
    }
}

bool MarketDataSocket::init_event_system() {
    if (event_fd_ >= 0) {
        return true;  // Already initialized
    }
    
#ifdef USE_KQUEUE
    event_fd_ = kqueue();
    if (event_fd_ < 0) {
        last_error_ = "Failed to create kqueue: " + std::string(strerror(errno));
        return false;
    }
#else
    event_fd_ = epoll_create1(0);
    if (event_fd_ < 0) {
        last_error_ = "Failed to create epoll: " + std::string(strerror(errno));
        return false;
    }
#endif
    
    return true;
}

bool MarketDataSocket::register_socket() {
#ifdef USE_KQUEUE
    struct kevent ev;
    EV_SET(&ev, fd_, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    if (kevent(event_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
        last_error_ = "Failed to register socket: " + std::string(strerror(errno));
        return false;
    }
#else
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Edge-triggered
    ev.data.fd = fd_;
    if (epoll_ctl(event_fd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        last_error_ = "Failed to register socket: " + std::string(strerror(errno));
        return false;
    }
#endif
    
    return true;
}

bool MarketDataSocket::connect(const std::string& host, uint16_t port,
                                uint32_t timeout_ms) {
    host_ = host;
    port_ = port;
    timeout_ms_ = timeout_ms;
    reconnect_count_ = 0;
    current_backoff_ms_ = 100;
    
    if (!init_event_system()) {
        return false;
    }
    
    return do_connect();
}

bool MarketDataSocket::do_connect() {
    // Close existing socket if any
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    
    connected_.store(false);
    
    // Resolve hostname
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    std::string port_str = std::to_string(port_);
    int ret = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0) {
        last_error_ = "Failed to resolve host: " + std::string(gai_strerror(ret));
        return false;
    }
    
    // Create socket
    fd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd_ < 0) {
        freeaddrinfo(result);
        last_error_ = "Failed to create socket: " + std::string(strerror(errno));
        return false;
    }
    
    // Set non-blocking for connect with timeout
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    
    // Initiate connection
    ret = ::connect(fd_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    
    if (ret < 0 && errno != EINPROGRESS) {
        last_error_ = "Connect failed: " + std::string(strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    // Wait for connection with timeout
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLOUT;
    
    ret = poll(&pfd, 1, timeout_ms_);
    if (ret <= 0) {
        last_error_ = ret == 0 ? "Connection timeout" : "Poll error";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    // Check connection result
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
        last_error_ = "Connection failed: " + std::string(strerror(error));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    // Configure socket
    configure_socket();
    
    // Register with event system
    if (!register_socket()) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    
    connected_.store(true);
    last_error_.clear();
    return true;
}

void MarketDataSocket::configure_socket() {
    set_tcp_nodelay(true);
    set_recv_buffer_size(DEFAULT_RECV_BUFFER);
    
    // Keep socket non-blocking
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

bool MarketDataSocket::set_tcp_nodelay(bool enable) {
    int flag = enable ? 1 : 0;
    return setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
}

bool MarketDataSocket::set_recv_buffer_size(size_t bytes) {
    int size = static_cast<int>(bytes);
    return setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == 0;
}

bool MarketDataSocket::set_socket_priority(int priority) {
#ifdef SO_PRIORITY
    return setsockopt(fd_, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) == 0;
#else
    (void)priority;
    return false;  // Not supported on macOS
#endif
}

ssize_t MarketDataSocket::receive(void* buffer, size_t max_len) {
    if (!connected_.load() || fd_ < 0) {
        return -1;
    }
    
    recv_calls_.fetch_add(1, std::memory_order_relaxed);
    
    ssize_t n = recv(fd_, buffer, max_len, MSG_DONTWAIT);
    
    if (n > 0) {
        bytes_received_.fetch_add(n, std::memory_order_relaxed);
        return n;
    }
    
    if (n == 0) {
        // Connection closed by server
        connected_.store(false);
        last_error_ = "Connection closed by server";
        return -1;
    }
    
    // n < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;  // No data available
    }
    
    // Real error
    connected_.store(false);
    last_error_ = "Receive error: " + std::string(strerror(errno));
    return -1;
}

int MarketDataSocket::wait_for_data(uint32_t timeout_ms) {
    if (!connected_.load() || fd_ < 0) {
        return -1;
    }
    
#ifdef USE_KQUEUE
    struct kevent events[1];
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    
    int nev = kevent(event_fd_, nullptr, 0, events, 1, &ts);
    
    if (nev < 0) {
        last_error_ = "kqueue wait error: " + std::string(strerror(errno));
        return -1;
    }
    
    if (nev == 0) {
        return 0;  // Timeout
    }
    
    // Check for errors
    if (events[0].flags & EV_EOF) {
        connected_.store(false);
        last_error_ = "Connection closed";
        return -1;
    }
    
    return 1;  // Data available
#else
    struct epoll_event events[1];
    int nev = epoll_wait(event_fd_, events, 1, timeout_ms);
    
    if (nev < 0) {
        last_error_ = "epoll wait error: " + std::string(strerror(errno));
        return -1;
    }
    
    if (nev == 0) {
        return 0;  // Timeout
    }
    
    // Check for errors
    if (events[0].events & (EPOLLERR | EPOLLHUP)) {
        connected_.store(false);
        last_error_ = "Connection error";
        return -1;
    }
    
    return 1;  // Data available
#endif
}

bool MarketDataSocket::send_subscription(const std::vector<uint16_t>& symbol_ids) {
    if (!connected_.load() || fd_ < 0) {
        return false;
    }
    
    // Build subscription message
    size_t msg_size = 1 + 2 + (symbol_ids.size() * 2);
    std::vector<uint8_t> buffer(msg_size);
    
    buffer[0] = SUBSCRIBE_CMD;
    uint16_t count = static_cast<uint16_t>(symbol_ids.size());
    std::memcpy(buffer.data() + 1, &count, 2);
    
    for (size_t i = 0; i < symbol_ids.size(); ++i) {
        std::memcpy(buffer.data() + 3 + (i * 2), &symbol_ids[i], 2);
    }
    
    ssize_t sent = send(fd_, buffer.data(), msg_size, 0);
    return sent == static_cast<ssize_t>(msg_size);
}

void MarketDataSocket::disconnect() {
    connected_.store(false);
    
    if (fd_ >= 0) {
        // Unregister from event system
#ifdef USE_KQUEUE
        if (event_fd_ >= 0) {
            struct kevent ev;
            EV_SET(&ev, fd_, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(event_fd_, &ev, 1, nullptr, 0, nullptr);
        }
#else
        if (event_fd_ >= 0) {
            epoll_ctl(event_fd_, EPOLL_CTL_DEL, fd_, nullptr);
        }
#endif
        
        ::close(fd_);
        fd_ = -1;
    }
}

bool MarketDataSocket::reconnect() {
    if (reconnect_count_ >= MAX_RETRY_COUNT) {
        last_error_ = "Max reconnect attempts exceeded";
        return false;
    }
    
    // Exponential backoff
    std::this_thread::sleep_for(std::chrono::milliseconds(current_backoff_ms_));
    current_backoff_ms_ = std::min(current_backoff_ms_ * 2, 30000u);  // Cap at 30 seconds
    
    reconnect_count_++;
    
    if (do_connect()) {
        current_backoff_ms_ = 100;  // Reset on success
        return true;
    }
    
    return false;
}

} // namespace mdf
