#include "visualizer.h"
#include "protocol.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

namespace mdf {

static struct termios original_termios;

Visualizer::Visualizer() {
    start_time_ = std::chrono::steady_clock::now();
    last_update_ = start_time_;
}

Visualizer::~Visualizer() {
    stop();
}

void Visualizer::init_terminal() {
    if (terminal_initialized_) return;
    
    // Save original terminal settings
    tcgetattr(STDIN_FILENO, &original_termios);
    
    // Set raw mode (no echo, no line buffering)
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;   // Non-blocking
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    
    // Set non-blocking stdin
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    // Hide cursor
    std::cout << "\033[?25l";
    
    // Clear screen
    std::cout << "\033[2J\033[H";
    
    update_terminal_size();
    terminal_initialized_ = true;
}

void Visualizer::restore_terminal() {
    if (!terminal_initialized_) return;
    
    // Show cursor
    std::cout << "\033[?25h";
    
    // Reset colors
    std::cout << color_reset();
    
    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
    
    // Restore blocking stdin
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    
    terminal_initialized_ = false;
}

void Visualizer::update_terminal_size() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        terminal_rows_ = w.ws_row;
        terminal_cols_ = w.ws_col;
    }
}

void Visualizer::start() {
    if (running_.load()) return;
    
    init_terminal();
    running_.store(true);
    start_time_ = std::chrono::steady_clock::now();
}

void Visualizer::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    restore_terminal();
    
    // Move cursor down and show final stats
    std::cout << "\n\nVisualization stopped.\n";
}

void Visualizer::set_connected(bool connected, const std::string& server) {
    connected_.store(connected);
    if (!server.empty()) {
        server_address_ = server;
    }
}

void Visualizer::update_stats(uint64_t messages, uint64_t bytes, uint64_t gaps) {
    messages_received_.store(messages, std::memory_order_relaxed);
    bytes_received_.store(bytes, std::memory_order_relaxed);
    sequence_gaps_.store(gaps, std::memory_order_relaxed);
    
    // Render if enough time has passed
    auto now = std::chrono::steady_clock::now();
    if (now - last_update_ >= std::chrono::milliseconds(REFRESH_INTERVAL_MS)) {
        render();
        last_update_ = now;
    }
}

void Visualizer::reset_stats() {
    messages_received_.store(0, std::memory_order_relaxed);
    bytes_received_.store(0, std::memory_order_relaxed);
    sequence_gaps_.store(0, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();
    last_message_count_ = 0;
    
    if (cache_) {
        cache_->reset();
    }
    if (latency_tracker_) {
        latency_tracker_->reset();
    }
}

bool Visualizer::process_input() {
    char c;
    while (read(STDIN_FILENO, &c, 1) > 0) {
        switch (c) {
            case 'q':
            case 'Q':
                return true;  // Quit
            case 'r':
            case 'R':
                reset_stats();
                break;
        }
    }
    return false;
}

void Visualizer::render() {
    if (!running_.load()) return;
    
    update_terminal_size();
    
    // Move cursor to top-left
    std::cout << "\033[H";
    
    render_header();
    render_market_table();
    render_statistics();
    render_footer();
    
    std::cout.flush();
}

void Visualizer::render_header() {
    std::cout << color_bold() << color_cyan();
    std::cout << "═══════════════════════════════════════════════════════════════════════\n";
    std::cout << "                    NSE Market Data Feed Handler                        \n";
    std::cout << "═══════════════════════════════════════════════════════════════════════\n";
    std::cout << color_reset();
    
    // Connection status
    std::cout << "Connected to: ";
    if (connected_.load()) {
        std::cout << color_green() << server_address_ << color_reset();
    } else {
        std::cout << color_red() << "DISCONNECTED" << color_reset();
    }
    
    // Uptime
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    std::cout << "  │  Uptime: " << color_yellow() << format_duration(uptime) << color_reset();
    
    // Message count and rate
    uint64_t msgs = messages_received_.load();
    double elapsed_sec = std::chrono::duration<double>(now - start_time_).count();
    double rate = elapsed_sec > 0 ? msgs / elapsed_sec : 0;
    
    std::cout << "  │  Messages: " << color_cyan() << format_number(msgs) << color_reset();
    std::cout << "  │  Rate: " << color_green() << format_rate(rate) << color_reset() << "\n\n";
}

void Visualizer::render_market_table() {
    if (!cache_) {
        std::cout << color_yellow() << "No market data available\n" << color_reset();
        return;
    }
    
    // Table header
    std::cout << color_bold();
    std::cout << std::left << std::setw(12) << "Symbol"
              << std::right << std::setw(12) << "Bid"
              << std::setw(12) << "Ask"
              << std::setw(12) << "LTP"
              << std::setw(12) << "Volume"
              << std::setw(10) << "Chg%"
              << std::setw(10) << "Updates";
    std::cout << color_reset() << "\n";
    
    std::cout << std::string(78, '-') << "\n";
    
    // Get top symbols
    uint16_t symbol_ids[MAX_SYMBOLS_DISPLAY];
    MarketState states[MAX_SYMBOLS_DISPLAY];
    cache_->get_top_symbols(symbol_ids, states, MAX_SYMBOLS_DISPLAY);
    
    for (size_t i = 0; i < MAX_SYMBOLS_DISPLAY; ++i) {
        const auto& state = states[i];
        if (state.update_count == 0) continue;
        
        // Calculate % change
        double pct_change = 0.0;
        if (state.opening_price > 0 && state.last_traded_price > 0) {
            pct_change = ((state.last_traded_price - state.opening_price) / 
                          state.opening_price) * 100.0;
        }
        
        // Symbol name
        std::cout << std::left << std::setw(12) << get_symbol_name(symbol_ids[i]);
        
        // Prices
        std::cout << std::right << std::setw(12) << format_price(state.best_bid);
        std::cout << std::setw(12) << format_price(state.best_ask);
        std::cout << std::setw(12) << format_price(state.last_traded_price);
        
        // Volume (sum of bid/ask qty as proxy)
        std::cout << std::setw(12) << format_number(state.bid_quantity + state.ask_quantity);
        
        // % Change with color
        if (pct_change >= 0) {
            std::cout << color_green();
        } else {
            std::cout << color_red();
        }
        std::cout << std::setw(9) << std::fixed << std::setprecision(2)
                  << (pct_change >= 0 ? "+" : "") << pct_change << "%";
        std::cout << color_reset();
        
        // Updates
        std::cout << std::setw(10) << format_number(state.update_count);
        std::cout << "\n";
    }
    
    std::cout << "\n";
}

void Visualizer::render_statistics() {
    std::cout << color_bold() << "Statistics:" << color_reset() << "\n";
    
    // Parser throughput (using message rate as proxy)
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time_).count();
    uint64_t msgs = messages_received_.load();
    double throughput = elapsed > 0 ? msgs / elapsed : 0;
    
    std::cout << "  Parser Throughput: " << color_cyan() << format_rate(throughput) << color_reset();
    
    // Latency percentiles
    if (latency_tracker_) {
        LatencyStats stats = latency_tracker_->get_stats();
        std::cout << "  │  End-to-End Latency: "
                  << "p50=" << color_yellow() << format_latency(stats.p50) << color_reset()
                  << " p99=" << color_yellow() << format_latency(stats.p99) << color_reset()
                  << " p999=" << color_yellow() << format_latency(stats.p999) << color_reset();
    }
    std::cout << "\n";
    
    // Sequence gaps and cache updates
    std::cout << "  Sequence Gaps: " << color_red() << sequence_gaps_.load() << color_reset();
    
    if (cache_) {
        std::cout << "  │  Cache Updates: " << color_cyan() 
                  << format_number(cache_->get_total_updates()) << color_reset();
    }
    std::cout << "\n\n";
}

void Visualizer::render_footer() {
    std::cout << color_yellow();
    std::cout << "Press 'q' to quit, 'r' to reset stats";
    std::cout << color_reset();
    
    // Clear rest of line
    std::cout << "\033[K\n";
}

// Color helpers
const char* Visualizer::color_green() { return "\033[32m"; }
const char* Visualizer::color_red() { return "\033[31m"; }
const char* Visualizer::color_yellow() { return "\033[33m"; }
const char* Visualizer::color_cyan() { return "\033[36m"; }
const char* Visualizer::color_reset() { return "\033[0m"; }
const char* Visualizer::color_bold() { return "\033[1m"; }

// Format helpers
std::string Visualizer::format_number(uint64_t n) {
    if (n >= 1000000000) {
        return std::to_string(n / 1000000000) + "." + 
               std::to_string((n / 100000000) % 10) + "B";
    }
    if (n >= 1000000) {
        return std::to_string(n / 1000000) + "." + 
               std::to_string((n / 100000) % 10) + "M";
    }
    if (n >= 1000) {
        return std::to_string(n / 1000) + "." + 
               std::to_string((n / 100) % 10) + "K";
    }
    return std::to_string(n);
}

std::string Visualizer::format_price(double price) {
    if (price == 0.0) return "-";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << price;
    return ss.str();
}

std::string Visualizer::format_duration(std::chrono::seconds duration) {
    int hours = duration.count() / 3600;
    int mins = (duration.count() % 3600) / 60;
    int secs = duration.count() % 60;
    
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setw(2) << mins << ":"
       << std::setw(2) << secs;
    return ss.str();
}

std::string Visualizer::format_rate(double rate) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(0) << rate << " msg/s";
    return ss.str();
}

std::string Visualizer::format_latency(uint64_t ns) {
    if (ns >= 1000000) {
        return std::to_string(ns / 1000000) + "ms";
    }
    if (ns >= 1000) {
        return std::to_string(ns / 1000) + "μs";
    }
    return std::to_string(ns) + "ns";
}

} // namespace mdf
