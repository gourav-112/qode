#include "memory_pool.h"
#include <cassert>
#include <cstring>

namespace mdf {

MemoryPool::MemoryPool(size_t block_size, size_t num_blocks)
    : block_size_(std::max(block_size, sizeof(FreeNode)))
    , num_blocks_(num_blocks) {
    
    // Ensure block size is aligned
    block_size_ = (block_size_ + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    
    // Allocate storage with extra space for alignment
    size_t total_size = (block_size_ * num_blocks_) + CACHE_LINE_SIZE;
    storage_ = std::make_unique<uint8_t[]>(total_size);
    
    // Align base pointer
    uintptr_t base = reinterpret_cast<uintptr_t>(storage_.get());
    uintptr_t aligned = (base + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    aligned_base_ = reinterpret_cast<uint8_t*>(aligned);
    
    // Initialize free list
    reset();
}

MemoryPool::~MemoryPool() = default;

void* MemoryPool::allocate() {
    FreeNode* node = free_list_.load(std::memory_order_acquire);
    
    while (node) {
        FreeNode* next = node->next.load(std::memory_order_relaxed);
        if (free_list_.compare_exchange_weak(node, next,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
            allocated_count_.fetch_add(1, std::memory_order_relaxed);
            return node;
        }
        // CAS failed, node is updated to current head, retry
    }
    
    return nullptr;  // Pool exhausted
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    
    // Verify pointer is within our pool
    if (ptr < aligned_base_ || 
        ptr >= aligned_base_ + (block_size_ * num_blocks_)) {
        return;  // Not our pointer
    }
    
    FreeNode* node = static_cast<FreeNode*>(ptr);
    FreeNode* expected = free_list_.load(std::memory_order_relaxed);
    
    do {
        node->next.store(expected, std::memory_order_relaxed);
    } while (!free_list_.compare_exchange_weak(expected, node,
                                                std::memory_order_release,
                                                std::memory_order_relaxed));
    
    allocated_count_.fetch_sub(1, std::memory_order_relaxed);
}

size_t MemoryPool::get_allocated_count() const {
    return allocated_count_.load(std::memory_order_relaxed);
}

size_t MemoryPool::get_available_count() const {
    return num_blocks_ - get_allocated_count();
}

void MemoryPool::reset() {
    allocated_count_.store(0, std::memory_order_relaxed);
    
    // Build free list from all blocks
    FreeNode* head = nullptr;
    
    for (size_t i = 0; i < num_blocks_; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(aligned_base_ + (i * block_size_));
        node->next.store(head, std::memory_order_relaxed);
        head = node;
    }
    
    free_list_.store(head, std::memory_order_release);
}

} // namespace mdf
