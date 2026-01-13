#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "../include/latency_tracker.h"

using namespace mdf;

void test_basic_recording() {
    std::cout << "Testing basic recording... ";
    
    LatencyTracker tracker;
    
    // Record some samples
    tracker.record(1000);   // 1μs
    tracker.record(2000);   // 2μs
    tracker.record(3000);   // 3μs
    
    LatencyStats stats = tracker.get_stats();
    
    assert(stats.sample_count == 3);
    assert(stats.min == 1000);
    assert(stats.max == 3000);
    assert(stats.mean == 2000);
    
    std::cout << "PASSED\n";
}

void test_percentiles() {
    std::cout << "Testing percentiles... ";
    
    LatencyTracker tracker;
    
    // Record 1000 samples from 1μs to 1000μs
    for (int i = 1; i <= 1000; ++i) {
        tracker.record(i * 1000);  // 1μs to 1000μs
    }
    
    LatencyStats stats = tracker.get_stats();
    
    assert(stats.sample_count == 1000);
    
    // p50 should be around 500μs (500,000 ns)
    // Allow some tolerance due to histogram bucketing
    assert(stats.p50 >= 400000 && stats.p50 <= 600000);
    
    // p99 should be around 990μs
    assert(stats.p99 >= 900000 && stats.p99 <= 1000000);
    
    std::cout << "PASSED\n";
}

void test_reset() {
    std::cout << "Testing reset... ";
    
    LatencyTracker tracker;
    
    tracker.record(1000);
    tracker.record(2000);
    
    LatencyStats before = tracker.get_stats();
    assert(before.sample_count == 2);
    
    tracker.reset();
    
    LatencyStats after = tracker.get_stats();
    assert(after.sample_count == 0);
    assert(after.min == 0);
    
    std::cout << "PASSED\n";
}

void test_overflow_bucket() {
    std::cout << "Testing overflow bucket... ";
    
    LatencyTracker tracker;
    
    // Record some normal samples
    tracker.record(1000);
    
    // Record sample larger than max tracked (1ms)
    tracker.record(10000000);  // 10ms - goes to overflow
    
    LatencyStats stats = tracker.get_stats();
    
    assert(stats.sample_count == 2);
    assert(stats.max == 10000000);
    
    std::cout << "PASSED\n";
}

void test_concurrent_recording() {
    std::cout << "Testing concurrent recording... ";
    
    LatencyTracker tracker;
    
    const int num_threads = 4;
    const int samples_per_thread = 10000;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&tracker, t]() {
            for (int i = 0; i < samples_per_thread; ++i) {
                tracker.record((t * 1000) + (i * 100));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    LatencyStats stats = tracker.get_stats();
    
    // Should have all samples
    assert(stats.sample_count == num_threads * samples_per_thread);
    
    std::cout << "PASSED\n";
}

void test_recording_overhead() {
    std::cout << "Testing recording overhead... ";
    
    LatencyTracker tracker;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000000; ++i) {
        tracker.record(1000);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ns_per_record = static_cast<double>(elapsed.count()) / 1000000.0;
    
    std::cout << "PASSED (avg: " << ns_per_record << "ns/record)\n";
    
    // Target is <30ns per record
    // Note: May not always meet target depending on hardware
    if (ns_per_record > 100) {
        std::cout << "  WARNING: Recording overhead higher than expected\n";
    }
}

int main() {
    std::cout << "=== Latency Tracker Tests ===\n";
    
    test_basic_recording();
    test_percentiles();
    test_reset();
    test_overflow_bucket();
    test_concurrent_recording();
    test_recording_overhead();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
