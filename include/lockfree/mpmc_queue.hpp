// MPMC Queue - Multi Producer Multi Consumer Lock-Free Queue
#pragma once

#include <atomic>
#include <array>
#include <cstddef>

// Suppress MSVC warning C4324: structure was padded due to alignment specifier
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324)
#endif

namespace lockfree {

template<typename T, size_t Capacity>
class MPMCQueue {
    static_assert(Capacity > 1, "Capacity must be greater than 1");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    struct Slot {
        T data;
        std::atomic<size_t> sequence;
    };

public:
    MPMCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    // Non-copyable, non-movable
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;
    
    bool push(const T& value) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = buffer_[pos & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);

            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                // 슬롯이 비어있음, CAS로 획득 시도
                // memory_order_relaxed 
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = value;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
        return false;
    }
    
    bool push(T&& value) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = buffer_[pos & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);

            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                // 슬롯이 비어있음, CAS로 획득 시도
                // memory_order_relaxed 
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = std::move(value);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
        return false;
    }
    
    bool pop(T& value) {
        // Hint: Use tail_.compare_exchange_weak for consumer competition
        size_t pos = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = buffer_[pos & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);

            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 1) {
                // 슬롯이 채워져 있음, CAS로 획득 시도
                if (tail_.compare_exchange_weak(pos, pos+1, std::memory_order_relaxed)) {
                    value = std::move(slot.data);
                    slot.sequence.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 1) {
                // Queue is empty
                return false;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
        return false;
    }
    
    // NOTE: These functions may return approximate values (lock-free characteristic)
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] bool full() const noexcept {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire) >= capacity();
    }
    
    [[nodiscard]] size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - 
               tail_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    alignas(64) std::array<Slot, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};  // Multiple producers compete via CAS
    alignas(64) std::atomic<size_t> tail_{0};  // Multiple consumers compete via CAS
};

} // namespace lockfree

#ifdef _MSC_VER
#pragma warning(pop)
#endif
