/**
 * ABA-Safe Stack Test
 * 
 * Tagged Pointer 방식으로 ABA 문제를 해결한 스택 테스트
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <iostream>
#include "lockfree/aba_safe_stack.hpp"

// ============================================
// Part 0: Lock-Free 여부 확인
// ============================================

TEST(ABASafeStackTest, CheckLockFree) {
    std::cout << "\n=== Lock-Free Check ===" << std::endl;
    
    // 우리 구현은 8바이트 atomic 사용
    std::cout << "sizeof(std::uintptr_t): " << sizeof(std::uintptr_t) << " bytes" << std::endl;
    std::cout << "is_always_lock_free: " << std::atomic<std::uintptr_t>::is_always_lock_free << std::endl;
    std::cout << "ABASafeStack::is_lock_free(): " << lockfree::ABASafeStack<int>::is_lock_free() << std::endl;
    
    if (lockfree::ABASafeStack<int>::is_lock_free()) {
        std::cout << "ABASafeStack is lock-free!" << std::endl;
    } else {
        std::cout << "ABASafeStack is NOT lock-free" << std::endl;
    }
    std::cout << "========================\n" << std::endl;
    
    // Lock-free여야 통과
    EXPECT_TRUE(lockfree::ABASafeStack<int>::is_lock_free());
}

// ============================================
// Part 1: 기본 동작 확인
// ============================================

TEST(ABASafeStackTest, BasicPushPop) {
    lockfree::ABASafeStack<int> stack;
    
    stack.push(1);
    stack.push(2);
    stack.push(3);
    
    // LIFO 순서: 3, 2, 1
    EXPECT_EQ(stack.pop().value(), 3);
    EXPECT_EQ(stack.pop().value(), 2);
    EXPECT_EQ(stack.pop().value(), 1);
    EXPECT_FALSE(stack.pop().has_value());
}

TEST(ABASafeStackTest, PopFromEmpty) {
    lockfree::ABASafeStack<int> stack;
    EXPECT_FALSE(stack.pop().has_value());
}

TEST(ABASafeStackTest, EmptyCheck) {
    lockfree::ABASafeStack<int> stack;
    EXPECT_TRUE(stack.empty());
    
    stack.push(42);
    EXPECT_FALSE(stack.empty());
    
    stack.pop();
    EXPECT_TRUE(stack.empty());
}

// ============================================
// Part 2: 멀티스레드 테스트
// ============================================

TEST(ABASafeStackTest, ConcurrentPushPop) {
    lockfree::ABASafeStack<int> stack;
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;
    
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    
    std::vector<std::thread> threads;
    
    // Push 스레드
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                stack.push(t * OPS_PER_THREAD + i);
                push_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // Pop 스레드
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                if (stack.pop().has_value()) {
                    pop_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 남은 요소 모두 pop
    while (stack.pop().has_value()) {
        pop_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    std::cout << "Push: " << push_count.load() << ", Pop: " << pop_count.load() << std::endl;
    EXPECT_EQ(push_count.load(), pop_count.load());
}

// ============================================
// Part 3: ABA 시뮬레이션 (안전성 확인)
// ============================================

TEST(ABASafeStackTest, ABA_Stress_Test) {
    lockfree::ABASafeStack<int> stack;
    constexpr int NUM_THREADS = 8;
    constexpr int ITERATIONS = 10000;
    
    std::atomic<bool> stop{false};
    std::atomic<int> total_ops{0};
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            int local_ops = 0;
            for (int i = 0; i < ITERATIONS && !stop.load(); ++i) {
                // push-pop-push-pop 패턴 (ABA 유발 패턴)
                stack.push(t * 1000 + i);
                stack.pop();
                stack.push(t * 1000 + i + 1);
                stack.pop();
                local_ops += 4;
            }
            total_ops.fetch_add(local_ops, std::memory_order_relaxed);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Total operations: " << total_ops.load() << std::endl;
    std::cout << "Stack empty: " << (stack.empty() ? "Yes" : "No") << std::endl;
    
    // 크래시 없이 완료되면 성공
    SUCCEED();
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
