// MPSC Queue - Multi Producer Single Consumer Lock-Free Queue
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
class MPSCQueue {
    static_assert(Capacity > 1, "Capacity must be greater than 1");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

    struct Slot {
        T data;
        std::atomic<size_t> sequence;
    };

public:
    MPSCQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    // Non-copyable, non-movable
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    MPSCQueue(MPSCQueue&&) = delete;
    MPSCQueue& operator=(MPSCQueue&&) = delete;
    
    bool push(const T& value) {
        size_t pos = head_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = buffer_[pos & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);

            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is empty, try to acquire it via CAS
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = value;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed, pos is updated, retry
            } else if (diff < 0) {
                // Queue is full (consumer hasn't freed this slot yet)
                return false;
            } else {
                // diff > 0: another producer already acquired this slot, reload pos and retry
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    bool push(T&& value) {
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = buffer_[pos & (Capacity - 1)];
            size_t seq = slot.sequence.load(std::memory_order_acquire);

            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    slot.data = std::move(value);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    bool pop(T& value) {
        // Only consumer thread modifies tail_
        size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[tail & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);

        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(tail + 1);
        if (diff == 0) {
            // Data is ready to be consumed
            value = std::move(slot.data);
            slot.sequence.store(tail + Capacity, std::memory_order_release);
            tail_.store(tail + 1, std::memory_order_release);
            return true;
        }
        // Queue is empty
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
    
    // Slot sequence technique allows using all Capacity slots
    // (unlike one-slot-empty strategy in SPSC)
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    alignas(64) std::array<Slot, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};  // Only consumer modifies, other threads only read
};

} // namespace lockfree

#ifdef _MSC_VER
#pragma warning(pop)
#endif
