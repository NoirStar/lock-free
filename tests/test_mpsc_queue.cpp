/**
 * MPSC Queue Test Suite
 * 
 * Multi Producer Single Consumer Lock-Free Queue Tests
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <algorithm>
#include <memory>
#include <string>
#include "lockfree/mpsc_queue.hpp"

// ============================================
// Basic Functionality Tests
// ============================================

TEST(MPSCQueueTest, InitialState) {
    lockfree::MPSCQueue<int, 16> queue;
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.capacity(), 16);  // Full Capacity (slot sequence allows all slots)
}

TEST(MPSCQueueTest, SinglePushPop) {
    lockfree::MPSCQueue<int, 16> queue;
    
    EXPECT_TRUE(queue.push(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    
    int value = 0;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(MPSCQueueTest, PopFromEmpty) {
    lockfree::MPSCQueue<int, 16> queue;
    
    int value = 0;
    EXPECT_FALSE(queue.pop(value));
    EXPECT_TRUE(queue.empty());
}

TEST(MPSCQueueTest, PushToFull) {
    lockfree::MPSCQueue<int, 4> queue;  // capacity = 4
    
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_TRUE(queue.push(4));
    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.push(5));  // Should fail - queue is full
    
    // Verify values
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 4);
    EXPECT_TRUE(queue.empty());
}

TEST(MPSCQueueTest, FIFOOrder) {
    lockfree::MPSCQueue<int, 16> queue;
    
    // Push 1 to 10
    for (int i = 1; i <= 10; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    
    // Pop should return in FIFO order
    for (int i = 1; i <= 10; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
    
    EXPECT_TRUE(queue.empty());
}

// ============================================
// Multi Producer Tests
// ============================================

TEST(MPSCQueueTest, TwoProducers) {
    lockfree::MPSCQueue<int, 256> queue;
    constexpr int ITEMS_PER_PRODUCER = 1000;
    
    std::atomic<bool> start{false};
    
    // Producer 1: pushes 0-999
    std::thread p1([&]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    // Producer 2: pushes 10000-10999
    std::thread p2([&]() {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            while (!queue.push(i + 10000)) {
                std::this_thread::yield();
            }
        }
    });
    
    // Consumer
    std::vector<int> results;
    results.reserve(ITEMS_PER_PRODUCER * 2);
    
    std::thread consumer([&]() {
        while (!start.load()) std::this_thread::yield();
        while (results.size() < ITEMS_PER_PRODUCER * 2) {
            int value;
            if (queue.pop(value)) {
                results.push_back(value);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    start.store(true);
    
    p1.join();
    p2.join();
    consumer.join();
    
    // Verify all values received
    EXPECT_EQ(results.size(), ITEMS_PER_PRODUCER * 2);
    
    // Count values from each producer
    int p1_count = 0, p2_count = 0;
    for (int v : results) {
        if (v < 10000) ++p1_count;
        else ++p2_count;
    }
    
    EXPECT_EQ(p1_count, ITEMS_PER_PRODUCER);
    EXPECT_EQ(p2_count, ITEMS_PER_PRODUCER);
}

TEST(MPSCQueueTest, FourProducers) {
    lockfree::MPSCQueue<int, 512> queue;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 500;
    
    std::atomic<bool> start{false};
    std::vector<std::thread> producers;
    
    // Create 4 producers, each pushes unique range
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                int value = p * 100000 + i;  // Unique per producer
                while (!queue.push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Consumer
    std::set<int> received;
    std::thread consumer([&]() {
        while (!start.load()) std::this_thread::yield();
        while (received.size() < NUM_PRODUCERS * ITEMS_PER_PRODUCER) {
            int value;
            if (queue.pop(value)) {
                received.insert(value);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    start.store(true);
    
    for (auto& t : producers) t.join();
    consumer.join();
    
    // All values unique and received
    EXPECT_EQ(received.size(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}

TEST(MPSCQueueTest, MultiProducerStress) {
    lockfree::MPSCQueue<int, 1024> queue;
    constexpr int NUM_PRODUCERS = 8;
    constexpr int ITEMS_PER_PRODUCER = 5000;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> producers_done{false};
    
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                while (!queue.push(i)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    std::thread consumer([&]() {
        while (!producers_done.load() || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                total_popped.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    for (auto& t : producers) t.join();
    producers_done.store(true);
    consumer.join();
    
    EXPECT_EQ(total_pushed.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(total_popped.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}

TEST(MPSCQueueTest, ProducerContention) {
    // Small queue with many producers = high contention
    lockfree::MPSCQueue<int, 8> queue;  // capacity = 7
    constexpr int NUM_PRODUCERS = 16;
    constexpr int ITEMS_PER_PRODUCER = 100;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> done{false};
    
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                while (!queue.push(i)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    std::thread consumer([&]() {
        while (!done.load() || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                total_popped.fetch_add(1, std::memory_order_relaxed);
            }
            // No yield - aggressive consumption
        }
    });
    
    for (auto& t : producers) t.join();
    done.store(true);
    consumer.join();
    
    EXPECT_EQ(total_pushed.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(total_popped.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}

// ============================================
// Type Tests
// ============================================

TEST(MPSCQueueTest, StringType) {
    lockfree::MPSCQueue<std::string, 16> queue;
    
    EXPECT_TRUE(queue.push("Hello"));
    EXPECT_TRUE(queue.push("World"));
    EXPECT_TRUE(queue.push("Lock-Free"));
    
    std::string value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, "Hello");
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, "World");
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, "Lock-Free");
    EXPECT_TRUE(queue.empty());
}

TEST(MPSCQueueTest, MoveOnlyType) {
    lockfree::MPSCQueue<std::unique_ptr<int>, 16> queue;
    
    // Push using move
    auto ptr1 = std::make_unique<int>(42);
    auto ptr2 = std::make_unique<int>(100);
    
    EXPECT_TRUE(queue.push(std::move(ptr1)));
    EXPECT_TRUE(queue.push(std::move(ptr2)));
    
    EXPECT_EQ(ptr1, nullptr);  // Moved
    EXPECT_EQ(ptr2, nullptr);  // Moved
    
    // Pop
    std::unique_ptr<int> result;
    EXPECT_TRUE(queue.pop(result));
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);
    
    EXPECT_TRUE(queue.pop(result));
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(*result, 100);
    
    EXPECT_TRUE(queue.empty());
}
