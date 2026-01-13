# System Design Document

## 1. System Architecture

### Client-Server Diagram

```
                                      Network
┌────────────────────────┐              │              ┌────────────────────────┐
│   Exchange Simulator   │              │              │     Feed Handler       │
│        (Server)        │              │              │       (Client)         │
├────────────────────────┤              │              ├────────────────────────┤
│                        │              │              │                        │
│  ┌──────────────────┐  │              │              │  ┌──────────────────┐  │
│  │  Tick Generator  │  │              │              │  │   TCP Socket     │  │
│  │  (GBM Engine)    │  │              │              │  │   (kqueue/epoll) │  │
│  └────────┬─────────┘  │              │              │  └────────┬─────────┘  │
│           │            │              │              │           │            │
│  ┌────────▼─────────┐  │   TCP/IP     │              │  ┌────────▼─────────┐  │
│  │  Client Manager  │──┼──────────────┼──────────────┼──│  Binary Parser   │  │
│  │  (Multi-client)  │  │              │              │  │  (Zero-copy)     │  │
│  └──────────────────┘  │              │              │  └────────┬─────────┘  │
│                        │              │              │           │            │
│  ┌──────────────────┐  │              │              │  ┌────────▼─────────┐  │
│  │  Event Loop      │  │              │              │  │  Symbol Cache    │  │
│  │  (kqueue/epoll)  │  │              │              │  │  (Lock-free)     │  │
│  └──────────────────┘  │              │              │  └────────┬─────────┘  │
│                        │              │              │           │            │
└────────────────────────┘              │              │  ┌────────▼─────────┐  │
                                        │              │  │  Visualizer      │  │
                                        │              │  │  (ANSI Terminal) │  │
                                        │              │  └──────────────────┘  │
                                        │              │                        │
                                        │              └────────────────────────┘
```

### Thread Model

**Server (Exchange Simulator):**
- Single-threaded event loop using kqueue (macOS) or epoll (Linux)
- Tick generation and broadcasting happen in the same thread
- Non-blocking I/O for all client connections

**Client (Feed Handler):**
- Main thread: Network I/O + Parsing (hot path)
- Visualization updates from the same thread (every 500ms)
- Lock-free cache allows safe concurrent reads

### Data Flow

```
[Tick Generator] → [Binary Encoder] → [TCP Send Buffer] → [Network]
                                                              │
                                                              ▼
[Visualizer] ← [Symbol Cache] ← [Binary Parser] ← [TCP Recv Buffer]
```

---

## 2. Geometric Brownian Motion

See [GBM.md](GBM.md) for detailed mathematical explanation.

**Key Implementation Choices:**
- Box-Muller transform for generating normal random variables
- Per-symbol volatility σ ∈ [0.01, 0.06] for realistic diversity
- Time step dt = 0.001 (1ms granularity)
- Drift μ configurable: 0.0 (neutral), +0.05 (bull), -0.05 (bear)

---

## 3. Network Layer Design

See [NETWORK.md](NETWORK.md) for detailed network implementation.

**Key Design Decisions:**

| Aspect | Choice | Rationale |
|--------|--------|-----------|
| I/O Multiplexing | kqueue/epoll | Scalable, low-latency event notification |
| Trigger Mode | Edge-triggered | Reduces system calls, works with non-blocking I/O |
| Buffer Size | 4MB | Handles burst traffic, reduces buffer overflow |
| TCP_NODELAY | Enabled | Disables Nagle's algorithm for low latency |

**Reconnection Strategy:**
```
Initial Delay: 100ms
Backoff: delay = min(delay * 2, 30000ms)
Max Attempts: 5
```

---

## 4. Memory Management Strategy

### Buffer Lifecycle
1. **Receive Buffer**: Pre-allocated 4MB buffer, reused for all recv() calls
2. **Parser Buffer**: Ring buffer that grows dynamically up to 16MB
3. **Message Buffers**: Stack-allocated for small, fixed-size messages

### Allocation Patterns
- **Hot Path**: Zero allocations (stack + pre-allocated buffers)
- **Initialization**: All major allocations during startup
- **Memory Pool**: Lock-free pool for network buffers (unused in current impl)

### Cache Considerations
- SymbolEntry aligned to 128 bytes (2 cache lines)
- Prevents false sharing between symbols
- Atomic operations use appropriate memory ordering

---

## 5. Concurrency Model

### Lock-Free Techniques

**SeqLock Pattern (Symbol Cache):**
```cpp
// Writer (single thread)
sequence_.store(seq + 1);  // Mark odd = writing
std::atomic_thread_fence(memory_order_release);
// ... write data ...
std::atomic_thread_fence(memory_order_release);
sequence_.store(seq + 2);  // Mark even = valid

// Reader (any thread)
do {
    seq1 = sequence_.load(memory_order_acquire);
    while (seq1 & 1) { /* spin if odd */ }
    // ... read data ...
    seq2 = sequence_.load(memory_order_acquire);
} while (seq1 != seq2);
```

### Memory Ordering
- **Writers**: `memory_order_release` to ensure visibility
- **Readers**: `memory_order_acquire` to see latest values
- **Statistics**: `memory_order_relaxed` (eventual consistency acceptable)

---

## 6. Visualization Design

### Update Strategy
- Polling-based: Check every 500ms
- Non-blocking: Uses non-blocking stdin for keyboard input

### ANSI Escape Codes
- `\033[H` - Move cursor home
- `\033[2J` - Clear screen
- `\033[32m` - Green color
- `\033[0m` - Reset attributes

### Statistics Calculation
- Message rate: Rolling average over session duration
- Latency: Histogram-based percentiles (O(1) lookup)
- Cache reads: Lock-free snapshots (no blocking)

---

## 7. Performance Optimization

### Hot Path Identification
```
recv() → append_data() → parse_one() → update_cache()
   │           │              │              │
   └───────────┴──────────────┴──────────────┘
                    HOT PATH
```

### Optimization Techniques
1. **Zero-copy Parsing**: Parse directly from receive buffer
2. **Cache-line Alignment**: 128-byte aligned symbol entries
3. **Branch Prediction**: Likely/unlikely hints for common paths
4. **Inlining**: Small functions inlined in headers

### System Call Minimization
- Edge-triggered mode reduces wakeups
- Large receive buffer reduces recv() calls
- Batched message parsing
