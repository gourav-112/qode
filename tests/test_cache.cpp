#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "../include/cache.h"

using namespace mdf;

void test_basic_operations() {
    std::cout << "Testing basic cache operations... ";
    
    SymbolCache cache(100);
    
    // Initial state should be empty
    MarketState state = cache.get_snapshot(0);
    assert(state.update_count == 0);
    
    // Update quote
    cache.update_quote(0, 100.0, 1000, 100.5, 2000, 1234567890);
    state = cache.get_snapshot(0);
    
    assert(state.best_bid == 100.0);
    assert(state.best_ask == 100.5);
    assert(state.bid_quantity == 1000);
    assert(state.ask_quantity == 2000);
    assert(state.update_count == 1);
    
    // Update trade
    cache.update_trade(0, 100.25, 500, 1234567891);
    state = cache.get_snapshot(0);
    
    assert(state.last_traded_price == 100.25);
    assert(state.last_traded_quantity == 500);
    assert(state.update_count == 2);
    
    std::cout << "PASSED\n";
}

void test_multiple_symbols() {
    std::cout << "Testing multiple symbols... ";
    
    SymbolCache cache(100);
    
    // Update different symbols
    for (uint16_t i = 0; i < 50; ++i) {
        cache.update_quote(i, 100.0 + i, 1000, 100.5 + i, 2000, i);
    }
    
    // Verify each symbol has correct data
    for (uint16_t i = 0; i < 50; ++i) {
        MarketState state = cache.get_snapshot(i);
        assert(state.best_bid == 100.0 + i);
        assert(state.update_count == 1);
    }
    
    std::cout << "PASSED\n";
}

void test_total_updates() {
    std::cout << "Testing total update count... ";
    
    SymbolCache cache(10);
    
    // 5 updates to symbol 0
    for (int i = 0; i < 5; ++i) {
        cache.update_quote(0, 100.0, 1000, 100.5, 2000, i);
    }
    
    // 3 updates to symbol 1
    for (int i = 0; i < 3; ++i) {
        cache.update_trade(1, 200.0, 500, i);
    }
    
    assert(cache.get_total_updates() == 8);
    
    std::cout << "PASSED\n";
}

void test_top_symbols() {
    std::cout << "Testing top symbols retrieval... ";
    
    SymbolCache cache(10);
    
    // Symbol 2 gets most updates
    for (int i = 0; i < 10; ++i) {
        cache.update_quote(2, 100.0, 1000, 100.5, 2000, i);
    }
    
    // Symbol 5 gets second most
    for (int i = 0; i < 5; ++i) {
        cache.update_quote(5, 200.0, 1000, 200.5, 2000, i);
    }
    
    // Symbol 0 gets one update
    cache.update_quote(0, 50.0, 1000, 50.5, 2000, 0);
    
    uint16_t ids[3];
    MarketState states[3];
    cache.get_top_symbols(ids, states, 3);
    
    assert(ids[0] == 2);  // Most active
    assert(ids[1] == 5);  // Second most active
    assert(ids[2] == 0);  // Third
    
    std::cout << "PASSED\n";
}

void test_concurrent_read() {
    std::cout << "Testing concurrent reads... ";
    
    SymbolCache cache(1);
    
    // Writer updates continuously
    std::atomic<bool> stop{false};
    std::thread writer([&]() {
        double price = 100.0;
        for (int i = 0; !stop.load() && i < 100000; ++i) {
            price += 0.01;
            cache.update_quote(0, price - 0.1, 1000, price + 0.1, 2000, i);
        }
    });
    
    // Reader checks consistency
    int read_count = 0;
    bool inconsistent = false;
    for (int i = 0; i < 10000; ++i) {
        MarketState state = cache.get_snapshot(0);
        // Spread should be 0.2 (ask - bid)
        if (state.best_ask > 0 && state.best_bid > 0) {
            double spread = state.best_ask - state.best_bid;
            if (spread < 0.19 || spread > 0.21) {
                inconsistent = true;
                break;
            }
            read_count++;
        }
    }
    
    stop.store(true);
    writer.join();
    
    assert(!inconsistent);
    assert(read_count > 0);
    
    std::cout << "PASSED (reads: " << read_count << ")\n";
}

int main() {
    std::cout << "=== Symbol Cache Tests ===\n";
    
    test_basic_operations();
    test_multiple_symbols();
    test_total_updates();
    test_top_symbols();
    test_concurrent_read();
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
