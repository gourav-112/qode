# Critical Thinking Questions - Answers

## Exchange Simulator (Server)

### 1. How do you efficiently broadcast to multiple clients without blocking?

**Approach**: Non-blocking sends with send buffer monitoring.

```cpp
for (auto& client : clients) {
    if (!client.is_slow) {
        // Non-blocking send, returns immediately if buffer full
        send(client.fd, data, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    }
}
```

Key techniques:
- Use `MSG_DONTWAIT` for non-blocking sends
- Track pending bytes with `ioctl(TIOCOUTQ)`
- Skip slow clients to avoid blocking on one client affecting others
- Consider separate broadcast thread for very high client counts

### 2. What happens when a client's TCP send buffer fills up?

When the send buffer is full:
1. `send()` returns `-1` with `errno = EAGAIN/EWOULDBLOCK`
2. Or returns partial bytes sent

**Our handling**:
- Mark client as "slow consumer"
- Skip this client for subsequent messages
- Monitor buffer drain via `TIOCOUTQ`
- Clear slow status when buffer drains below threshold/2

### 3. How do you ensure fair distribution when some clients are slower?

Fair distribution strategies:
1. **Skip slow clients** (our approach): Fast clients aren't penalized by slow ones
2. **Per-client queues**: Each client has own message queue, drops if full
3. **Round-robin with timeout**: Move to next client if send would block

Trade-off: Slow clients miss messages but fast clients stay up-to-date.

### 4. How would you handle 1000+ concurrent client connections?

Scaling approaches:

1. **Connection limit per process**: Accept up to 10K FDs with `ulimit -n`
2. **Multiple processes**: SO_REUSEPORT for load balancing
3. **Event batching**: Process multiple events per loop iteration
4. **Client grouping**: Multicast groups for common subscriptions

---

## TCP Client Socket

### 1. Why use epoll edge-triggered instead of level-triggered?

**Edge-triggered advantages**:
- Only notified on state changes, not while data is available
- Fewer epoll_wait returns per message burst
- Forces complete buffer drain, reducing syscall count

**Trade-off**: More complex programming (must drain buffer completely).

### 2. How do you handle EAGAIN/EWOULDBLOCK?

```cpp
ssize_t n = recv(fd, buffer, size, MSG_DONTWAIT);
if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available right now
        // Wait for next event (edge-triggered will notify on new data)
        return 0;
    }
    // Real error
    return -1;
}
```

### 3. What happens if the kernel receive buffer fills up?

If the receive buffer fills:
1. TCP advertises zero window to sender
2. Sender stops transmitting until window opens
3. This creates back-pressure through the system

**Prevention**: Set large receive buffer (4MB) with `SO_RCVBUF`.

### 4. How do you detect a silent connection drop (no FIN/RST)?

**Methods**:
1. **Heartbeats**: Server sends heartbeat every second; client expects it
2. **TCP keepalive**: `SO_KEEPALIVE` with short intervals
3. **Application timeout**: No data for N seconds = assume dead

```cpp
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle_time, sizeof(idle_time));
```

### 5. Should reconnection logic be in the same thread or separate?

**Same thread** (our approach):
- Simpler implementation
- Blocks message processing during reconnect
- Acceptable for single-connection client

**Separate thread** (for production):
- Main thread continues processing
- Background thread handles reconnection
- Better for multi-connection scenarios

---

## Binary Protocol Parser

### 1. How do you buffer incomplete messages across recv() calls?

Use an accumulating buffer:

```cpp
class Parser {
    vector<uint8_t> buffer_;
    size_t read_pos_, write_pos_;
    
    void append(const void* data, size_t len) {
        memcpy(buffer_ + write_pos_, data, len);
        write_pos_ += len;
    }
    
    ParseResult parse_one() {
        if (available() < HEADER_SIZE)
            return NEED_MORE_DATA;
        
        size_t msg_size = get_message_size(header);
        if (available() < msg_size)
            return NEED_MORE_DATA;
        
        // Parse complete message
        read_pos_ += msg_size;
        return SUCCESS;
    }
};
```

### 2. What happens when you detect a sequence gap?

**Our approach**: Log and continue
- Market data is ephemeral; old data has low value
- Requesting retransmission adds latency
- Better to use latest data than wait for old

**Alternative for reliable feeds**:
- Request gap fill from recovery port
- Use separate gap-fill channel to avoid blocking main feed

### 3. How would you handle messages arriving out of order?

TCP guarantees in-order delivery, so out-of-order only happens with:
1. **UDP-based feeds**: Use sequence numbers to reorder
2. **Multiple TCP connections**: Merge with sequence-aware priority queue
3. **Packet retransmission**: TCP handles transparently

For our single TCP connection, this isn't an issue.

### 4. How do you prevent buffer overflow with malicious large message lengths?

```cpp
// Validate message size before reading payload
size_t msg_size = get_message_size(type);

if (msg_size == 0 || msg_size > MAX_MESSAGE_SIZE) {
    // Skip one byte and try to resync
    read_pos_++;
    return INVALID_MESSAGE;
}

if (msg_size > buffer_.capacity()) {
    // Don't grow unboundedly
    return INVALID_MESSAGE;
}
```

---

## Lock-Free Symbol Cache

### 1. How do you prevent readers from seeing inconsistent state during updates?

**SeqLock pattern**:
- Writer increments sequence to odd before writing
- Writer increments sequence to even after writing
- Reader checks if sequence is even AND unchanged

```cpp
// Reader
do {
    seq1 = sequence_.load(acquire);
    while (seq1 & 1) { /* spin */ }  // Wait if writing
    snapshot = state;  // Copy data
    seq2 = sequence_.load(acquire);
} while (seq1 != seq2);  // Retry if changed
```

### 2. What memory ordering do you need for atomic operations?

| Operation | Ordering | Rationale |
|-----------|----------|-----------|
| Writer: start seq | `release` | Data before this becomes visible |
| Writer: end seq | `release` | All writes complete before inc |
| Reader: load seq | `acquire` | See all writes before this |
| Statistics | `relaxed` | Eventual consistency OK |

### 3. How do you handle cache line bouncing with single writer, visualization reader?

**Cache line bouncing** occurs when multiple CPUs modify data on the same cache line.

**Our mitigation**:
- Each SymbolEntry is 128 bytes (2 cache lines)
- Sequence counter and data on same line
- Reader only reads, never modifies

Since we have single writer, no write-write contention exists.

### 4. Do you need read-copy-update (RCU) pattern here?

**RCU** is useful for:
- Multiple readers, infrequent writers
- Pointer-based data structures
- When you can't afford reader spinning

**Our case**: SeqLock is simpler and sufficient because:
- Single writer, so no complex synchronization needed
- Fixed-size structures (no pointer indirection)
- Spinning is rare (writes are fast)

---

## Terminal Visualization

### 1. How do you update display without interfering with network/parsing threads?

**Our approach**: Same thread, time-sliced
- Check time elapsed since last update
- Only render if > 500ms has passed
- Rendering is quick (~0.5ms)

**Alternative**: Separate display thread
- Would need message queue or atomic counters
- Overkill for our simple visualization

### 2. Should you use ncurses or raw ANSI codes?

**Raw ANSI codes** (our choice):
- No external dependency
- Full control over output
- Simpler for basic UI
- Works in any terminal

**ncurses**:
- Better for complex UIs
- Handles terminal differences
- Built-in windowing, menus
- Overkill for status display

### 3. How do you calculate percentage change when prices update continuously?

**Our approach**: Store opening price on first update
```cpp
if (state.opening_price == 0.0) {
    state.opening_price = price;
}
pct_change = (current_price - opening_price) / opening_price * 100;
```

**Alternatives**:
- Use daily open from market data
- Rolling window average
- Previous close from reference data

---

## Latency Tracking

### 1. Sorting is O(n log n) - how can you calculate percentiles faster?

**Answer**: Use a **Histogram** (Frequency Array).

Instead of storing all 'N' samples and sorting them, we define buckets for latency ranges and increment counters.

*   **Complexity**:
    *   **Record**: O(1) - just increment `buckets[latency]`
    *   **Calculate P99**: O(B) where B is number of buckets (iterate until count reaches 99% of total)
    *   **Memory**: Fixed size (e.g., 100k buckets), independent of 'N' samples.

**Our Implementation**:
We use a `LatencyTracker` with a `std::vector<uint64_t> histogram_` of 100,000 buckets (100ns precision up to 10ms, plus overflow).

### 2. How do you minimize the overhead of timestamping?

**Techniques**:

1.  **RDTSC Instruction**: Use CPU cycle counter (Read Time-Stamp Counter) instead of system calls like `clock_gettime` or `gettimeofday`.
    *   `rdtsc` takes ~20 CPU cycles.
    *   `clock_gettime` takes ~100-200 nanoseconds (syscall or vdso overhead).
2.  **Avoid Conversions**: Store raw cycle counts in the hot path. Convert to wall-time (nanoseconds) only when reporting statistics (lazy evaluation).
3.  **Cpu Frequency Scaling**: Ensure CPU affinity and disable frequency scaling (turbo boost) so cycle-to-time conversion remains constant (or use `rdtscp`).

**Our Code**: Currently uses `std::chrono::high_resolution_clock` for portability/simplicity, but in strict low-latency production, `rdtsc` is the standard.

### 3. What granularity of histogram buckets balances accuracy vs memory?

**Balance Strategy**:

*   **Granularity**: 100 nanoseconds (0.1 microseconds).
    *   Market data latency matters in microseconds. 100ns provides sufficient detail to distinguish between 5.1us and 5.2us.
*   **Range**: 0 to 10 milliseconds (10,000 microseconds).
    *   Covers the "normal" operation range.
*   **Memory Usage**:
    *   100,000 buckets * 8 bytes (uint64_t) ≈ 800 KB.
    *   This fits comfortably in L3 cache (often even L2), preventing cache misses during recording.

For values > 10ms (outliers), we use a single "overflow" bucket, as the exact value matters less than the fact it was "huge". High Dynamic Range (HDR) Histograms use logarithmic buckets to cover huge ranges with small memory, but linear buckets are faster to index (no log calculation needed).
