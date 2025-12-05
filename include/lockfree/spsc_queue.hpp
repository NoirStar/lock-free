// SPSC Queue - Single Producer Single Consumer Lock-Free Queue
#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>

// Suppress MSVC warning C4324: structure was padded due to alignment specifier
// This is intentional to prevent false sharing
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace lockfree {

template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 1, "Capacity must be greater than 1");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    SPSCQueue() = default;
    
    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;
    
    bool push(const T& value) {
        size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= capacity()) {
            return false;
        }
        buffer_[head & (Capacity - 1)] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    
    // TODO: Implement push (move)
    bool push(T&& value) {
        size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= capacity()) {
            return false;
        }
        buffer_[head & (Capacity - 1)] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    
    // TODO: Implement pop
    bool pop(T& value) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == tail) {
            return false;
        }
        value = std::move(buffer_[tail & (Capacity - 1)]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    
    // TODO: Implement empty
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    // TODO: Implement full
    [[nodiscard]] bool full() const noexcept {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire) >= capacity();
    }
    
    // TODO: Implement size (optional)
    [[nodiscard]] size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire);
    }
    
    // Returns usable capacity (Capacity - 1 if using one-slot-empty strategy)
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity - 1;
    }

private:
    alignas(64) std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace lockfree

#ifdef _MSC_VER
#pragma warning(pop)
#endif
