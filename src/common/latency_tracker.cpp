#include "latency_tracker.h"
#include <fstream>
#include <algorithm>

namespace mdf {

LatencyTracker::LatencyTracker() {
    reset();
}

void LatencyTracker::record(uint64_t latency_ns) {
    // Increment sample count
    sample_count_.fetch_add(1, std::memory_order_relaxed);
    
    // Update sum for mean calculation (may overflow, but acceptable)
    sum_.fetch_add(latency_ns, std::memory_order_relaxed);
    
    // Update min
    uint64_t current_min = min_.load(std::memory_order_relaxed);
    while (latency_ns < current_min && 
           !min_.compare_exchange_weak(current_min, latency_ns, 
                                        std::memory_order_relaxed));
    
    // Update max
    uint64_t current_max = max_.load(std::memory_order_relaxed);
    while (latency_ns > current_max && 
           !max_.compare_exchange_weak(current_max, latency_ns, 
                                        std::memory_order_relaxed));
    
    // Update histogram
    if (latency_ns < MAX_TRACKED_NS) {
        size_t bucket = latency_ns / BUCKET_WIDTH_NS;
        histogram_[bucket].fetch_add(1, std::memory_order_relaxed);
    } else {
        overflow_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Store in ring buffer (for detailed analysis if needed)
    uint64_t idx = write_index_.fetch_add(1, std::memory_order_relaxed);
    ring_buffer_[idx % RING_BUFFER_SIZE].store(latency_ns, std::memory_order_relaxed);
}

LatencyStats LatencyTracker::get_stats() const {
    LatencyStats stats;
    
    stats.sample_count = sample_count_.load(std::memory_order_relaxed);
    if (stats.sample_count == 0) {
        stats.min = 0;
        return stats;
    }
    
    stats.min = min_.load(std::memory_order_relaxed);
    stats.max = max_.load(std::memory_order_relaxed);
    stats.mean = sum_.load(std::memory_order_relaxed) / stats.sample_count;
    
    // Calculate percentiles from histogram
    stats.p50 = percentile_from_histogram(50.0);
    stats.p95 = percentile_from_histogram(95.0);
    stats.p99 = percentile_from_histogram(99.0);
    stats.p999 = percentile_from_histogram(99.9);
    
    return stats;
}

uint64_t LatencyTracker::percentile_from_histogram(double percentile) const {
    uint64_t total = sample_count_.load(std::memory_order_relaxed);
    if (total == 0) return 0;
    
    uint64_t target = static_cast<uint64_t>((percentile / 100.0) * total);
    uint64_t cumulative = 0;
    
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        cumulative += histogram_[i].load(std::memory_order_relaxed);
        if (cumulative >= target) {
            // Return midpoint of bucket
            return (i * BUCKET_WIDTH_NS) + (BUCKET_WIDTH_NS / 2);
        }
    }
    
    // Overflow bucket - return max
    return max_.load(std::memory_order_relaxed);
}

void LatencyTracker::reset() {
    sample_count_.store(0, std::memory_order_relaxed);
    sum_.store(0, std::memory_order_relaxed);
    min_.store(UINT64_MAX, std::memory_order_relaxed);
    max_.store(0, std::memory_order_relaxed);
    overflow_count_.store(0, std::memory_order_relaxed);
    
    for (auto& bucket : histogram_) {
        bucket.store(0, std::memory_order_relaxed);
    }
    
    for (auto& sample : ring_buffer_) {
        sample.store(0, std::memory_order_relaxed);
    }
    
    write_index_.store(0, std::memory_order_relaxed);
}

bool LatencyTracker::export_csv(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << "bucket_start_ns,bucket_end_ns,count\n";
    
    for (size_t i = 0; i < NUM_BUCKETS; ++i) {
        uint64_t count = histogram_[i].load(std::memory_order_relaxed);
        if (count > 0) {
            file << (i * BUCKET_WIDTH_NS) << ","
                 << ((i + 1) * BUCKET_WIDTH_NS) << ","
                 << count << "\n";
        }
    }
    
    // Overflow bucket
    uint64_t overflow = overflow_count_.load(std::memory_order_relaxed);
    if (overflow > 0) {
        file << MAX_TRACKED_NS << ",inf," << overflow << "\n";
    }
    
    return true;
}

} // namespace mdf
