/**
 * SPSC Queue Test Suite
 * 
 * TDD: Write tests first, then implement!
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "lockfree/spsc_queue.hpp"

constexpr size_t DEFAULT_CAPACITY = 16;

// ============================================
// Basic Functionality Tests
// ============================================

TEST(SPSCQueueTest, InitialState) {
    // TODO: Create queue, check empty() == true, full() == false
    lockfree::SPSCQueue<int, DEFAULT_CAPACITY> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
}

TEST(SPSCQueueTest, SinglePushPop) {
    lockfree::SPSCQueue<int, DEFAULT_CAPACITY> queue;
    EXPECT_TRUE(queue.push(42));
    EXPECT_FALSE(queue.empty());
    
    int value = 0;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(SPSCQueueTest, PopFromEmpty) {
    lockfree::SPSCQueue<int, DEFAULT_CAPACITY> queue;
    int value = 999;
    EXPECT_FALSE(queue.pop(value));
    EXPECT_EQ(value, 999);  // value should be unchanged
}

TEST(SPSCQueueTest, PushToFull) {
    lockfree::SPSCQueue<int, DEFAULT_CAPACITY> queue;
    // capacity() returns Capacity - 1 (one-slot-empty strategy)
    for (size_t i = 0; i < queue.capacity(); ++i) {
        EXPECT_TRUE(queue.push(static_cast<int>(i)));
    }
    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.push(999));  // should fail
}

TEST(SPSCQueueTest, FIFOOrder) {
    lockfree::SPSCQueue<int, 8> queue;
    for (int i = 1; i <= 5; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    for (int i = 1; i <= 5; ++i) {
        int value = 0;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
}

TEST(SPSCQueueTest, WrapAround) {
    lockfree::SPSCQueue<int, 8> queue;  // capacity = 7
    // Fill half
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    // Pop all
    for (int i = 0; i < 4; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
    // Now head and tail are at position 4
    // Fill again to test wrap-around
    for (int i = 100; i < 107; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    EXPECT_TRUE(queue.full());
    // Pop and verify wrap-around worked
    for (int i = 100; i < 107; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
}

// ============================================
// Multithreaded Tests
// ============================================

TEST(SPSCQueueTest, ConcurrentBasic) {
    lockfree::SPSCQueue<int, 64> queue;
    constexpr int NUM_ITEMS = 1000;
    std::vector<int> received;
    received.reserve(NUM_ITEMS);
    
    std::thread producer([&queue]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    std::thread consumer([&queue, &received]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            int value;
            while (!queue.pop(value)) {
                std::this_thread::yield();
            }
            received.push_back(value);
        }
    });
    
    producer.join();
    consumer.join();
    
    ASSERT_EQ(received.size(), NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i);
    }
}

TEST(SPSCQueueTest, ConcurrentStress) {
    lockfree::SPSCQueue<int, 1024> queue;
    constexpr int NUM_ITEMS = 1000000;
    std::atomic<bool> producer_done{false};
    std::vector<int> received;
    received.reserve(NUM_ITEMS);
    
    std::thread producer([&queue, &producer_done]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    std::thread consumer([&queue, &received, &producer_done]() {
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                received.push_back(value);
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    ASSERT_EQ(received.size(), NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i);
    }
}

// ============================================
// Type Tests
// ============================================

TEST(SPSCQueueTest, StringType) {
    lockfree::SPSCQueue<std::string, 16> queue;
    EXPECT_TRUE(queue.push("hello"));
    EXPECT_TRUE(queue.push("world"));
    
    std::string value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, "hello");
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, "world");
}

TEST(SPSCQueueTest, MoveOnlyType) {
    lockfree::SPSCQueue<std::unique_ptr<int>, 16> queue;
    EXPECT_TRUE(queue.push(std::make_unique<int>(42)));
    EXPECT_TRUE(queue.push(std::make_unique<int>(100)));
    
    std::unique_ptr<int> value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(*value, 42);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(*value, 100);
}
