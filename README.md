# High-Performance Market Data Feed Handler

A low-latency multi-asset market data processor for NSE (National Stock Exchange) co-location. This system consists of an Exchange Simulator (TCP server) generating realistic market data using Geometric Brownian Motion, and a Feed Handler (TCP client) that receives, parses, and displays the data in real-time.

## Features

- **Exchange Simulator (Server)**
  - Geometric Brownian Motion (GBM) price generation for realistic market simulation
  - Configurable tick rates (10K - 500K messages/second)
  - Multi-client support via kqueue (macOS) / epoll (Linux)
  - Slow consumer detection and flow control
  - Fault injection for testing (sequence gaps)

- **Feed Handler (Client)**
  - Zero-copy binary message parsing
  - Lock-free symbol cache with SeqLock pattern
  - Real-time terminal visualization with ANSI colors
  - Automatic reconnection with exponential backoff
  - Latency tracking with histogram-based percentiles

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│              Exchange Simulator (Server)                 │
│  - Generate ticks with Geometric Brownian Motion         │
│  - Manage 100+ symbols                                   │
│  - TCP Server (kqueue/epoll-based)                       │
│  - Configurable tick rate (10K-500K msgs/sec)            │
└──────────────────────────┬──────────────────────────────┘
                           │ Binary Protocol over TCP
                           ▼
┌─────────────────────────────────────────────────────────┐
│                 Feed Handler (Client)                    │
│  - TCP Client (kqueue/epoll-based)                       │
│  - Zero-copy binary parser                               │
│  - Lock-free symbol cache                                │
│  - Terminal visualization with statistics                │
└─────────────────────────────────────────────────────────┘
```

## Building

### Prerequisites
- C++17 compatible compiler (GCC 7+, Clang 5+, Apple Clang 10+)
- CMake 3.16 or higher
- pthread library

### Build Commands

```bash
# Build in Release mode (default)
./scripts/build.sh

# Build in Debug mode (with sanitizers)
./scripts/build.sh Debug
```

## Usage

### Running the Demo

The easiest way to see the system in action:

```bash
./scripts/run_demo.sh
```

This starts both the server and client, displaying live market data.

### Running Components Separately

**Start the Exchange Simulator:**
```bash
./scripts/run_server.sh [options]
# Options:
#   -p, --port <port>      Server port (default: 9876)
#   -s, --symbols <count>  Number of symbols (default: 100)
#   -r, --rate <rate>      Tick rate/sec (default: 100000)
#   -m, --market <type>    neutral, bull, bear (default: neutral)
#   -f, --fault            Enable fault injection
```

**Start the Feed Handler:**
```bash
./scripts/run_client.sh [options]
# Options:
#   -h, --host <host>      Server hostname (default: localhost)
#   -p, --port <port>      Server port (default: 9876)
#   -n, --no-visual        Disable visualization
#   -r, --no-reconnect     Disable auto-reconnect
```

### Interactive Controls
- Press `q` to quit
- Press `r` to reset statistics

## Binary Protocol

### Message Header (16 bytes)
| Field | Size | Description |
|-------|------|-------------|
| Message Type | 2 bytes | 0x01=Trade, 0x02=Quote, 0x03=Heartbeat |
| Sequence Number | 4 bytes | Monotonically increasing |
| Timestamp | 8 bytes | Nanoseconds since epoch |
| Symbol ID | 2 bytes | 0-499 for 500 symbols |

### Payloads
- **Trade**: Price (8 bytes) + Quantity (4 bytes) + Checksum (4 bytes)
- **Quote**: BidPrice (8) + BidQty (4) + AskPrice (8) + AskQty (4) + Checksum (4)
- **Heartbeat**: Checksum (4 bytes) only

## Performance Targets

| Metric | Target |
|--------|--------|
| Tick Generation | 100K+ ticks/second |
| Parser Throughput | 100K+ messages/second |
| Cache Read Latency | < 50 nanoseconds |
| End-to-End Latency | p50 < 50μs, p99 < 200μs |

## Project Structure

```
├── src/
│   ├── server/
│   │   ├── exchange_simulator.cpp   # TCP server
│   │   ├── tick_generator.cpp       # GBM implementation
│   │   ├── client_manager.cpp       # Multi-client handling
│   │   └── main.cpp
│   ├── client/
│   │   ├── feed_handler.cpp         # Main client
│   │   ├── socket.cpp               # TCP client socket
│   │   ├── parser.cpp               # Binary parser
│   │   ├── visualizer.cpp           # Terminal UI
│   │   └── main.cpp
│   └── common/
│       ├── cache.cpp                # Lock-free symbol cache
│       ├── memory_pool.cpp          # Buffer pool
│       └── latency_tracker.cpp      # Performance measurement
├── include/                         # Public headers
├── docs/                            # Documentation
├── scripts/                         # Build and run scripts
├── tests/                           # Unit tests
└── CMakeLists.txt
```

## Documentation

- [DESIGN.md](docs/DESIGN.md) - System architecture and design decisions
- [GBM.md](docs/GBM.md) - Geometric Brownian Motion explanation
- [NETWORK.md](docs/NETWORK.md) - Network implementation details
- [PERFORMANCE.md](docs/PERFORMANCE.md) - Performance analysis
- [QUESTIONS.md](docs/QUESTIONS.md) - Answers to critical thinking questions

## License

This project is for educational purposes.
