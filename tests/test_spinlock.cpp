/**
 * SpinLock Test Suite
 * 
 * Tests for lock-free spinlock implementation
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include "lockfree/spinlock.hpp"

// ============================================
// Basic Functionality Tests
// ============================================

TEST(SpinLockTest, LockUnlock) {
    lockfree::SpinLock lock;
    lock.lock();
    lock.unlock();
    // Should not deadlock or crash
    SUCCEED();
}

TEST(SpinLockTest, TryLockSuccess) {
    lockfree::SpinLock lock;
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinLockTest, TryLockFail) {
    lockfree::SpinLock lock;
    lock.lock();
    
    // try_lock should fail when already locked
    std::atomic<bool> try_lock_result{true};
    std::thread t([&lock, &try_lock_result]() {
        try_lock_result = lock.try_lock();
    });
    t.join();
    
    EXPECT_FALSE(try_lock_result);
    lock.unlock();
}

TEST(SpinLockTest, MultipleLockUnlock) {
    lockfree::SpinLock lock;
    for (int i = 0; i < 100; ++i) {
        lock.lock();
        lock.unlock();
    }
    SUCCEED();
}

// ============================================
// SpinLockGuard Tests
// ============================================

TEST(SpinLockGuardTest, BasicUsage) {
    lockfree::SpinLock lock;
    {
        lockfree::SpinLockGuard guard(lock);
        // Lock should be held here
    }
    // Lock should be released here
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinLockGuardTest, ExceptionSafety) {
    lockfree::SpinLock lock;
    try {
        lockfree::SpinLockGuard guard(lock);
        throw std::runtime_error("test exception");
    } catch (...) {
        // Lock should be released even after exception
    }
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

// ============================================
// Multithreaded Tests
// ============================================

TEST(SpinLockTest, ConcurrentIncrement) {
    lockfree::SpinLock lock;
    int counter = 0;
    constexpr int NUM_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;
    
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&lock, &counter]() {
            for (int j = 0; j < INCREMENTS_PER_THREAD; ++j) {
                lock.lock();
                ++counter;
                lock.unlock();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter, NUM_THREADS * INCREMENTS_PER_THREAD);
}

TEST(SpinLockTest, ConcurrentIncrementWithGuard) {
    lockfree::SpinLock lock;
    int counter = 0;
    constexpr int NUM_THREADS = 4;
    constexpr int INCREMENTS_PER_THREAD = 10000;
    
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&lock, &counter]() {
            for (int j = 0; j < INCREMENTS_PER_THREAD; ++j) {
                lockfree::SpinLockGuard guard(lock);
                ++counter;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter, NUM_THREADS * INCREMENTS_PER_THREAD);
}

TEST(SpinLockTest, MutualExclusion) {
    lockfree::SpinLock lock;
    std::atomic<int> in_critical_section{0};
    std::atomic<bool> violation_detected{false};
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 1000;
    
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITERATIONS; ++j) {
                lock.lock();
                
                // Only one thread should be in critical section
                int prev = in_critical_section.fetch_add(1, std::memory_order_relaxed);
                if (prev != 0) {
                    violation_detected.store(true, std::memory_order_relaxed);
                }
                
                // Simulate some work
                std::this_thread::yield();
                
                in_critical_section.fetch_sub(1, std::memory_order_relaxed);
                lock.unlock();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_FALSE(violation_detected.load());
    EXPECT_EQ(in_critical_section.load(), 0);
}

TEST(SpinLockTest, TryLockContention) {
    lockfree::SpinLock lock;
    std::atomic<int> successful_locks{0};
    std::atomic<int> failed_locks{0};
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 1000;
    
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITERATIONS; ++j) {
                if (lock.try_lock()) {
                    successful_locks.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                    lock.unlock();
                } else {
                    failed_locks.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // At least some locks should have succeeded
    EXPECT_GT(successful_locks.load(), 0);
    // Total should equal all attempts
    EXPECT_EQ(successful_locks.load() + failed_locks.load(), NUM_THREADS * ITERATIONS);
}

// ============================================
// Stress Tests
// ============================================

TEST(SpinLockTest, StressTest) {
    lockfree::SpinLock lock;
    std::atomic<long long> counter{0};
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 100000;
    
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&lock, &counter]() {
            for (int j = 0; j < ITERATIONS; ++j) {
                lockfree::SpinLockGuard guard(lock);
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(counter.load(), static_cast<long long>(NUM_THREADS) * ITERATIONS);
}

TEST(SpinLockTest, ProducerConsumerPattern) {
    lockfree::SpinLock lock;
    std::vector<int> shared_data;
    std::atomic<bool> producer_done{false};
    constexpr int NUM_ITEMS = 10000;
    
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            lockfree::SpinLockGuard guard(lock);
            shared_data.push_back(i);
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    std::vector<int> consumed;
    consumed.reserve(NUM_ITEMS);
    
    std::thread consumer([&]() {
        while (!producer_done.load(std::memory_order_acquire) || !shared_data.empty()) {
            lockfree::SpinLockGuard guard(lock);
            if (!shared_data.empty()) {
                consumed.push_back(shared_data.back());
                shared_data.pop_back();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    // All items should be consumed
    EXPECT_EQ(consumed.size(), NUM_ITEMS);
}

// ============================================
// Performance Characteristics Tests
// ============================================

TEST(SpinLockTest, LowContentionPerformance) {
    lockfree::SpinLock lock;
    constexpr int ITERATIONS = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; ++i) {
        lock.lock();
        lock.unlock();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Just ensure it completes in reasonable time (< 1 second)
    EXPECT_LT(duration.count(), 1000000);
}

TEST(SpinLockTest, AlternatingThreads) {
    lockfree::SpinLock lock;
    std::atomic<int> counter{0};
    constexpr int ITERATIONS = 10000;
    
    std::thread t1([&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            lockfree::SpinLockGuard guard(lock);
            ++counter;
        }
    });
    
    std::thread t2([&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            lockfree::SpinLockGuard guard(lock);
            ++counter;
        }
    });
    
    t1.join();
    t2.join();
    
    EXPECT_EQ(counter.load(), 2 * ITERATIONS);
}
