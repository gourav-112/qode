# Network Implementation Details

## 1. Server-Side Design

### Multi-Client epoll/kqueue Handling

The server uses a single event loop to handle multiple clients efficiently:

```cpp
#ifdef USE_KQUEUE
    struct kevent events[64];
    int nev = kevent(event_fd, nullptr, 0, events, 64, &timeout);
#else
    struct epoll_event events[64];
    int nev = epoll_wait(event_fd, events, 64, timeout_ms);
#endif

for (int i = 0; i < nev; ++i) {
    if (events[i].fd == server_fd) {
        handle_new_connection();
    } else {
        handle_client_event(events[i].fd);
    }
}
```

### Broadcast Strategy

**Current Implementation**: Sequential iteration over all clients
```cpp
size_t broadcast(const void* data, size_t len) {
    for (auto& [fd, client] : clients) {
        if (!client.is_slow) {
            send_to_client(fd, data, len);
        }
    }
}
```

**Alternative Approach** (for higher scale): Dedicated broadcast thread with message queue.

### Slow Client Detection

Monitor send buffer usage via `TIOCOUTQ`:
```cpp
size_t get_pending_bytes(int fd) {
    int pending = 0;
    ioctl(fd, TIOCOUTQ, &pending);
    return pending;
}

// Mark as slow if pending > 1MB
if (pending > SLOW_THRESHOLD) {
    client.is_slow = true;
}
```

---

## 2. Client-Side Design

### Why epoll/kqueue over select/poll?

| Method | Time Complexity | FD Limit | Notes |
|--------|-----------------|----------|-------|
| select | O(n) | 1024 | Copies FD set on each call |
| poll | O(n) | Unlimited | Still scans all FDs |
| epoll/kqueue | O(1) | Unlimited | Kernel maintains ready list |

For high-frequency trading, O(1) notification is crucial.

### Edge-Triggered vs Level-Triggered

**Edge-Triggered (Used)**:
- Notifies only on state change (data arrival)
- Requires draining buffer completely
- Fewer system calls

```cpp
// Edge-triggered: Must read until EAGAIN
while (true) {
    ssize_t n = recv(fd, buffer, size, MSG_DONTWAIT);
    if (n <= 0) break;
    process(buffer, n);
}
```

**Level-Triggered**:
- Notifies while data is available
- Simpler programming model
- More system calls

### Non-Blocking I/O Patterns

```cpp
// Set non-blocking mode
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

// Handle EAGAIN/EWOULDBLOCK
ssize_t n = recv(fd, buf, len, MSG_DONTWAIT);
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // No data available, wait for next event
    return 0;
}
```

---

## 3. TCP Stream Handling

### Message Boundary Detection

TCP is a stream protocol - messages may be fragmented:

```
Sent:    [MSG1][MSG2][MSG3]
Received:[MSG1][MS  |  G2][MSG3]
         ^^^^ One read ^^^^
```

**Solution**: Parse header first, then wait for complete payload:

```cpp
// Check if we have header
if (available < HEADER_SIZE) return NEED_MORE_DATA;

// Get message size from header
size_t msg_size = get_message_size(header->type);

// Check if we have complete message
if (available < msg_size) return NEED_MORE_DATA;

// Parse complete message
parse_message(buffer, msg_size);
```

### Partial Read Buffering

Use a ring buffer that accumulates data across reads:

```cpp
class Parser {
    vector<uint8_t> buffer_;  // 4MB initial
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    
    void append_data(const void* data, size_t len) {
        if (space_left() < len) compact();
        memcpy(buffer_ + write_pos_, data, len);
        write_pos_ += len;
    }
    
    void compact() {
        memmove(buffer_, buffer_ + read_pos_, used());
        write_pos_ -= read_pos_;
        read_pos_ = 0;
    }
};
```

### Buffer Sizing

```
Calculation:
- Max message rate: 500K msg/s
- Max message size: 44 bytes (Quote)
- Data rate: 22 MB/s
- Buffer for 100ms burst: 2.2 MB
- Safety margin: 2x = 4 MB
```

---

## 4. Connection Management

### Connection State Machine

```
    ┌──────────────┐
    │ DISCONNECTED │◄──────────────────────┐
    └──────┬───────┘                       │
           │ connect()                     │
           ▼                               │ max retries
    ┌──────────────┐                       │
    │  CONNECTING  │───────────────────────┤
    └──────┬───────┘ timeout/error         │
           │ success                       │
           ▼                               │
    ┌──────────────┐                       │
    │  CONNECTED   │───────────────────────┘
    └──────┬───────┘ error/close
           │
           ▼
    ┌──────────────┐
    │ RECONNECTING │
    └──────────────┘
```

### Retry Logic and Backoff

```cpp
// Exponential backoff with cap
current_backoff = 100ms;  // Initial

bool reconnect() {
    sleep(current_backoff);
    current_backoff = min(current_backoff * 2, 30s);
    
    if (connect()) {
        current_backoff = 100ms;  // Reset on success
        return true;
    }
    return false;
}
```

---

## 5. Error Handling

### Network Errors

| Error | Meaning | Recovery |
|-------|---------|----------|
| EAGAIN | No data (non-blocking) | Wait for next event |
| EWOULDBLOCK | Same as EAGAIN | Wait for next event |
| EPIPE | Broken pipe (write) | Reconnect |
| ECONNRESET | Connection reset | Reconnect |
| ETIMEDOUT | Connection timeout | Reconnect |

### Application-Level Errors

| Error | Detection | Handling |
|-------|-----------|----------|
| Checksum mismatch | calculate_checksum() | Skip message, log |
| Sequence gap | Compare seq numbers | Log, continue |
| Malformed message | Invalid type/size | Skip byte, retry parse |

### Recovery Strategies

1. **Transient Errors**: Retry with backoff
2. **Parse Errors**: Skip and continue
3. **Fatal Errors**: Disconnect and reconnect
4. **Max Retries**: Notify user, exit
