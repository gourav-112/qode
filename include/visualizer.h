#pragma once

#include <cstdint>
#include <atomic>
#include <string>
#include "cache.h"
#include "latency_tracker.h"

namespace mdf {

// Terminal visualizer for market data feed
class Visualizer {
public:
    static constexpr int REFRESH_INTERVAL_MS = 500;
    static constexpr size_t MAX_SYMBOLS_DISPLAY = 20;
    
    Visualizer();
    ~Visualizer();
    
    // Start visualization (non-blocking, spawns thread)
    void start();
    
    // Stop visualization
    void stop();
    
    // Set data sources
    void set_cache(SymbolCache* cache) { cache_ = cache; }
    void set_latency_tracker(LatencyTracker* tracker) { latency_tracker_ = tracker; }
    
    // Update connection status
    void set_connected(bool connected, const std::string& server = "");
    
    // Update statistics (thread-safe)
    void update_stats(uint64_t messages_received, uint64_t bytes_received,
                      uint64_t sequence_gaps);
    
    // Handle keyboard input (called from main thread)
    // Returns true if 'q' was pressed
    bool process_input();
    
    // Reset displayed statistics
    void reset_stats();
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
private:
    SymbolCache* cache_ = nullptr;
    LatencyTracker* latency_tracker_ = nullptr;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::string server_address_;
    
    // Statistics (updated atomically)
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> sequence_gaps_{0};
    
    // Timing
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_update_;
    uint64_t last_message_count_ = 0;
    
    // Terminal state
    int terminal_rows_ = 24;
    int terminal_cols_ = 80;
    bool terminal_initialized_ = false;
    
    // Initialize terminal for raw mode
    void init_terminal();
    
    // Restore terminal to normal mode
    void restore_terminal();
    
    // Get terminal size
    void update_terminal_size();
    
    // Render the display
    void render();
    
    // Render sections
    void render_header();
    void render_market_table();
    void render_statistics();
    void render_footer();
    
    // Color helpers
    static const char* color_green();
    static const char* color_red();
    static const char* color_yellow();
    static const char* color_cyan();
    static const char* color_reset();
    static const char* color_bold();
    
    // Format helpers
    static std::string format_number(uint64_t n);
    static std::string format_price(double price);
    static std::string format_duration(std::chrono::seconds duration);
    static std::string format_rate(double rate);
    static std::string format_latency(uint64_t ns);
};

} // namespace mdf
