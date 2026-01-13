#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>

namespace mdf {

struct LatencyStats {
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint64_t mean = 0;
    uint64_t p50 = 0;
    uint64_t p95 = 0;
    uint64_t p99 = 0;
    uint64_t p999 = 0;
    uint64_t sample_count = 0;
};

class LatencyTracker {
public:
    static constexpr size_t RING_BUFFER_SIZE = 1 << 20;  // 1M samples
    static constexpr size_t NUM_BUCKETS = 1000;
    static constexpr uint64_t BUCKET_WIDTH_NS = 1000;    // 1μs per bucket, covers 0-1ms
    static constexpr uint64_t MAX_TRACKED_NS = NUM_BUCKETS * BUCKET_WIDTH_NS;
    
    LatencyTracker();
    
    // Record a latency sample (in nanoseconds)
    // Thread-safe, low overhead (<30ns target)
    void record(uint64_t latency_ns);
    
    // Get percentile statistics
    LatencyStats get_stats() const;
    
    // Reset all statistics
    void reset();
    
    // Export histogram to CSV
    bool export_csv(const std::string& filename) const;
    
private:
    // Ring buffer for raw samples (for exact percentile if needed)
    std::array<std::atomic<uint64_t>, RING_BUFFER_SIZE> ring_buffer_;
    std::atomic<uint64_t> write_index_{0};
    
    // Histogram buckets for fast percentile calculation
    std::array<std::atomic<uint64_t>, NUM_BUCKETS> histogram_;
    std::atomic<uint64_t> overflow_count_{0};  // Samples > MAX_TRACKED
    
    // Running statistics
    std::atomic<uint64_t> sample_count_{0};
    std::atomic<uint64_t> sum_{0};
    std::atomic<uint64_t> min_{UINT64_MAX};
    std::atomic<uint64_t> max_{0};
    
    // Helper to find percentile from histogram
    uint64_t percentile_from_histogram(double percentile) const;
};

} // namespace mdf
