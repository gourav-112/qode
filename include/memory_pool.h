#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace mdf {

// Lock-free memory pool for fixed-size allocations
// Designed for network buffer management with minimal contention
class MemoryPool {
public:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 4096;
    static constexpr size_t DEFAULT_POOL_SIZE = 1024;
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    MemoryPool(size_t block_size = DEFAULT_BLOCK_SIZE,
               size_t num_blocks = DEFAULT_POOL_SIZE);
    ~MemoryPool();
    
    // Allocate a block (returns nullptr if pool exhausted)
    void* allocate();
    
    // Return a block to the pool
    void deallocate(void* ptr);
    
    // Get pool statistics
    size_t get_allocated_count() const;
    size_t get_available_count() const;
    size_t block_size() const { return block_size_; }
    size_t capacity() const { return num_blocks_; }
    
    // Reset pool (all blocks available)
    void reset();
    
    // Non-copyable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
private:
    struct alignas(CACHE_LINE_SIZE) FreeNode {
        std::atomic<FreeNode*> next;
    };
    
    size_t block_size_;
    size_t num_blocks_;
    
    // Aligned memory storage
    std::unique_ptr<uint8_t[]> storage_;
    uint8_t* aligned_base_;
    
    // Lock-free free list using tagged pointer for ABA prevention
    std::atomic<FreeNode*> free_list_{nullptr};
    std::atomic<size_t> allocated_count_{0};
};

// RAII wrapper for pool-allocated buffers
class PoolBuffer {
public:
    PoolBuffer() : pool_(nullptr), data_(nullptr) {}
    PoolBuffer(MemoryPool* pool, void* data) : pool_(pool), data_(data) {}
    
    ~PoolBuffer() {
        if (data_ && pool_) {
            pool_->deallocate(data_);
        }
    }
    
    // Move-only
    PoolBuffer(PoolBuffer&& other) noexcept
        : pool_(other.pool_), data_(other.data_) {
        other.pool_ = nullptr;
        other.data_ = nullptr;
    }
    
    PoolBuffer& operator=(PoolBuffer&& other) noexcept {
        if (this != &other) {
            if (data_ && pool_) {
                pool_->deallocate(data_);
            }
            pool_ = other.pool_;
            data_ = other.data_;
            other.pool_ = nullptr;
            other.data_ = nullptr;
        }
        return *this;
    }
    
    PoolBuffer(const PoolBuffer&) = delete;
    PoolBuffer& operator=(const PoolBuffer&) = delete;
    
    void* data() { return data_; }
    const void* data() const { return data_; }
    explicit operator bool() const { return data_ != nullptr; }
    
    void* release() {
        void* ptr = data_;
        data_ = nullptr;
        pool_ = nullptr;
        return ptr;
    }
    
private:
    MemoryPool* pool_;
    void* data_;
};

} // namespace mdf
