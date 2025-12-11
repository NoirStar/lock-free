#include <gtest/gtest.h>
#include "lockfree/mpmc_queue.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace lockfree;

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST(MPMCQueue, InitialState) {
    MPMCQueue<int, 16> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 0);
}

TEST(MPMCQueue, SinglePushPop) {
    MPMCQueue<int, 16> queue;
    EXPECT_TRUE(queue.push(42));
    EXPECT_FALSE(queue.empty());
    
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(MPMCQueue, PopFromEmpty) {
    MPMCQueue<int, 16> queue;
    int value = -1;
    EXPECT_FALSE(queue.pop(value));
    EXPECT_EQ(value, -1);  // Value should not be modified
}

TEST(MPMCQueue, PushToFull) {
    MPMCQueue<int, 4> queue;  // capacity = 4
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_TRUE(queue.push(4));
    EXPECT_TRUE(queue.full());
    EXPECT_FALSE(queue.push(5));  // Should fail - queue is full
}

TEST(MPMCQueue, FIFOOrder) {
    MPMCQueue<int, 16> queue;
    for (int i = 1; i <= 10; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    
    for (int i = 1; i <= 10; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
}

TEST(MPMCQueue, WrapAround) {
    MPMCQueue<int, 4> queue;
    
    // Fill half
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    
    // Empty
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 2);
    
    // Fill again - should wrap around
    for (int i = 3; i <= 6; ++i) {
        EXPECT_TRUE(queue.push(i));
    }
    
    // Verify all values
    for (int i = 3; i <= 6; ++i) {
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
}

TEST(MPMCQueue, MoveSemantics) {
    MPMCQueue<std::string, 8> queue;
    std::string str = "Hello, World!";
    EXPECT_TRUE(queue.push(std::move(str)));
    
    std::string result;
    EXPECT_TRUE(queue.pop(result));
    EXPECT_EQ(result, "Hello, World!");
}

// ============================================================================
// Multi Producer Tests
// ============================================================================

TEST(MPMCQueue, TwoProducersOneConsumer) {
    MPMCQueue<int, 128> queue;
    constexpr int ITEMS_PER_PRODUCER = 1000;
    std::atomic<int> total_consumed{0};
    
    auto producer = [&](int base) {
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            while (!queue.push(base + i)) {
                std::this_thread::yield();
            }
        }
    };
    
    std::thread p1(producer, 0);
    std::thread p2(producer, 100000);
    
    std::thread consumer([&]() {
        for (int i = 0; i < ITEMS_PER_PRODUCER * 2; ++i) {
            int value;
            while (!queue.pop(value)) {
                std::this_thread::yield();
            }
            total_consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });
    
    p1.join();
    p2.join();
    consumer.join();
    
    EXPECT_EQ(total_consumed.load(), ITEMS_PER_PRODUCER * 2);
}

// ============================================================================
// Multi Consumer Tests
// ============================================================================

TEST(MPMCQueue, OneProducerTwoConsumers) {
    MPMCQueue<int, 128> queue;
    constexpr int TOTAL_ITEMS = 2000;
    std::atomic<int> total_consumed{0};
    std::atomic<bool> producer_done{false};
    
    std::thread producer([&]() {
        for (int i = 0; i < TOTAL_ITEMS; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    auto consumer = [&]() {
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                total_consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    };
    
    std::thread c1(consumer);
    std::thread c2(consumer);
    
    producer.join();
    c1.join();
    c2.join();
    
    EXPECT_EQ(total_consumed.load(), TOTAL_ITEMS);
}

// ============================================================================
// Full MPMC Tests
// ============================================================================

TEST(MPMCQueue, MultiProducerMultiConsumer) {
    MPMCQueue<int, 1024> queue;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> producers_done{false};
    
    // N Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                while (!queue.push(p * ITEMS_PER_PRODUCER + i)) {
                    std::this_thread::yield();
                }
                total_pushed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // M Consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; ++c) {
        consumers.emplace_back([&]() {
            while (!producers_done.load(std::memory_order_acquire) || !queue.empty()) {
                int value;
                if (queue.pop(value)) {
                    total_popped.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& p : producers) p.join();
    producers_done.store(true, std::memory_order_release);
    for (auto& c : consumers) c.join();
    
    EXPECT_EQ(total_pushed.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(total_popped.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}

TEST(MPMCQueue, BalancedLoad) {
    MPMCQueue<int, 256> queue;
    constexpr int NUM_PAIRS = 8;
    constexpr int ITEMS_PER_PAIR = 5000;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;
    
    for (int i = 0; i < NUM_PAIRS; ++i) {
        // Producer
        threads.emplace_back([&]() {
            for (int j = 0; j < ITEMS_PER_PAIR; ++j) {
                while (!queue.push(j)) std::this_thread::yield();
                produced.fetch_add(1);
            }
        });
        
        // Consumer
        threads.emplace_back([&]() {
            for (int j = 0; j < ITEMS_PER_PAIR; ++j) {
                int val;
                while (!queue.pop(val)) std::this_thread::yield();
                consumed.fetch_add(1);
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(produced.load(), NUM_PAIRS * ITEMS_PER_PAIR);
    EXPECT_EQ(consumed.load(), NUM_PAIRS * ITEMS_PER_PAIR);
}

// ============================================================================
// Data Integrity Tests
// ============================================================================

TEST(MPMCQueue, DataIntegrity) {
    MPMCQueue<int, 512> queue;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 5000;
    constexpr int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    
    std::vector<std::atomic<int>> received(TOTAL_ITEMS);
    for (auto& r : received) r.store(0);
    
    std::atomic<bool> done{false};
    
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
                while (!queue.push(p * ITEMS_PER_PRODUCER + i)) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    std::vector<std::thread> consumers;
    for (int c = 0; c < 4; ++c) {
        consumers.emplace_back([&]() {
            while (!done.load(std::memory_order_acquire) || !queue.empty()) {
                int value;
                if (queue.pop(value)) {
                    if (value >= 0 && value < TOTAL_ITEMS) {
                        received[value].fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }
    
    for (auto& p : producers) p.join();
    done.store(true, std::memory_order_release);
    for (auto& c : consumers) c.join();
    
    // Verify each value was received exactly once
    for (int i = 0; i < TOTAL_ITEMS; ++i) {
        EXPECT_EQ(received[i].load(), 1) << "Value " << i << " received " 
                                          << received[i].load() << " times";
    }
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(MPMCQueue, HighContentionStress) {
    MPMCQueue<int, 64> queue;  // Small capacity for high contention
    constexpr int NUM_THREADS = 16;
    constexpr int OPS_PER_THREAD = 10000;
    
    std::atomic<int> push_success{0};
    std::atomic<int> pop_success{0};
    std::atomic<bool> stop{false};
    
    std::vector<std::thread> threads;
    
    // Half producers, half consumers
    for (int i = 0; i < NUM_THREADS / 2; ++i) {
        // Producer
        threads.emplace_back([&]() {
            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                while (!queue.push(j)) {
                    if (stop.load()) return;
                    std::this_thread::yield();
                }
                push_success.fetch_add(1);
            }
        });
        
        // Consumer
        threads.emplace_back([&]() {
            int count = 0;
            while (count < OPS_PER_THREAD) {
                int val;
                if (queue.pop(val)) {
                    pop_success.fetch_add(1);
                    ++count;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(push_success.load(), (NUM_THREADS / 2) * OPS_PER_THREAD);
    EXPECT_EQ(pop_success.load(), (NUM_THREADS / 2) * OPS_PER_THREAD);
}
