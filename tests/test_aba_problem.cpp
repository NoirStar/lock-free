/**
 * ABA Problem Test Suite
 * 
 * 이 테스트는 ABA 문제를 이해하고 재현하기 위한 학습용 코드입니다.
 * 
 * ═══════════════════════════════════════════════════════════════
 * 🎯 학습 목표:
 * 1. ABA 문제가 무엇인지 이해한다
 * 2. ABA 문제가 왜 위험한지 체감한다
 * 3. ABA 문제를 해결하는 방법을 고민한다
 * ═══════════════════════════════════════════════════════════════
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include "lockfree/aba_stack.hpp"

using namespace std::chrono_literals;

// ============================================
// Part 1: 기본 동작 확인
// ============================================

TEST(ABAStackTest, BasicPushPop) {
    lockfree::ABAProneStack<int> stack;
    
    stack.push(1);
    stack.push(2);
    stack.push(3);
    
    // LIFO 순서: 3, 2, 1
    EXPECT_EQ(stack.pop().value(), 3);
    EXPECT_EQ(stack.pop().value(), 2);
    EXPECT_EQ(stack.pop().value(), 1);
    EXPECT_FALSE(stack.pop().has_value());
}

TEST(ABAStackTest, PopFromEmpty) {
    lockfree::ABAProneStack<int> stack;
    EXPECT_FALSE(stack.pop().has_value());
}

// ============================================
// Part 2: ABA 문제 시각화
// ============================================

/**
 * 이 테스트는 ABA 문제가 발생하는 시나리오를 보여줍니다.
 * 실제로 crash가 발생하지 않을 수도 있지만,
 * 논리적으로 잘못된 상태가 됩니다.
 */
TEST(ABAStackTest, ABA_Scenario_Visualization) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "         ABA 문제 시나리오 시각화\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    lockfree::ABAProneStack<int> stack;
    
    // 초기 상태: [30] → [20] → [10] → nullptr
    stack.push(10);
    stack.push(20);
    stack.push(30);
    
    auto* node_30 = stack.get_head();
    auto* node_20 = node_30->next;
    auto* node_10 = node_20->next;
    
    std::cout << "초기 스택 상태:\n";
    std::cout << "  head → [30:" << node_30 << "] → [20:" << node_20 
              << "] → [10:" << node_10 << "] → nullptr\n\n";
    
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "Thread A: pop() 시작\n";
    std::cout << "  - old_head = " << node_30 << " (값: 30)\n";
    std::cout << "  - next = " << node_20 << " (값: 20)\n";
    std::cout << "  - CAS 실행 전에 일시 중단됨...\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    // Thread A가 중단된 동안 Thread B의 작업
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "Thread B: 여러 작업 수행\n";
    std::cout << "  1. pop() → 30 제거\n";
    
    auto popped_30 = stack.pop_node();  // 30 제거 (삭제 안 함)
    std::cout << "     현재: [20] → [10] → nullptr\n";
    
    std::cout << "  2. pop() → 20 제거\n";
    auto popped_20 = stack.pop_node();  // 20 제거
    std::cout << "     현재: [10] → nullptr\n";
    
    std::cout << "  3. push(40) → 새 노드 추가\n";
    stack.push(40);
    auto* node_40 = stack.get_head();
    std::cout << "     현재: [40:" << node_40 << "] → [10] → nullptr\n";
    
    std::cout << "  4. push(30) → 기존 노드 재사용!\n";
    // 실제 시나리오에서는 삭제된 node_30의 메모리가 재사용될 수 있음
    stack.push_node(popped_30);  // 같은 주소의 노드를 다시 push
    std::cout << "     현재: [30:" << popped_30 << "] → [40] → [10] → nullptr\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "Thread A: 재개 - CAS 실행 시도\n";
    std::cout << "  - expected: " << node_30 << "\n";
    std::cout << "  - current head: " << stack.get_head() << "\n";
    std::cout << "  - 주소가 같으므로 CAS 성공! ✓\n";
    std::cout << "  - head를 " << node_20 << " (옛날 next)로 변경\n\n";
    
    std::cout << "⚠️  문제 발생!\n";
    std::cout << "  - Thread A가 저장해둔 next(" << node_20 << ")는\n";
    std::cout << "    이미 pop된 노드의 주소입니다!\n";
    std::cout << "  - 스택이 손상되었습니다: [20:???] → ???\n";
    std::cout << "  - node_40과 node_10이 사라졌습니다!\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    // 정리
    delete popped_20;
    
    SUCCEED();
}

// ============================================
// Part 3: ABA 문제 실제 재현 시도
// ============================================

/**
 * 이 테스트는 실제로 ABA 문제를 발생시키려고 시도합니다.
 * 타이밍에 따라 성공하거나 실패할 수 있습니다.
 */
TEST(ABAStackTest, ABA_RaceCondition) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "         ABA 문제 Race Condition 테스트\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    std::atomic<int> aba_detected{0};
    std::atomic<int> iterations_completed{0};
    
    constexpr int NUM_ITERATIONS = 100;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        lockfree::ABAProneStack<int> stack;
        
        // 초기 상태: [2] → [1] → nullptr
        stack.push(1);
        stack.push(2);
        
        auto* original_head = stack.get_head();
        auto* original_next = original_head->next;
        
        std::atomic<bool> thread_a_ready{false};
        std::atomic<bool> thread_b_done{false};
        
        // Thread A: slow pop (중간에 지연)
        std::thread thread_a([&]() {
            auto* old_head = stack.get_head();
            
            if (old_head == nullptr) return;
            
            [[maybe_unused]] auto* next = old_head->next;
            
            // Thread B에게 준비됐다고 알림
            thread_a_ready.store(true, std::memory_order_release);
            
            // Thread B가 작업 완료할 때까지 대기
            while (!thread_b_done.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            // 이제 CAS 시도 - old_head 주소가 재사용되었다면 ABA!
            // 하지만 next가 잘못된 값을 가리키게 됨
        });
        
        // Thread B: 빠른 조작
        std::thread thread_b([&]() {
            // Thread A가 준비될 때까지 대기
            while (!thread_a_ready.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            
            // pop 2번 + push로 ABA 상황 만들기 시도
            auto* node1 = stack.pop_node();  // [2] 제거
            if (node1) {
                auto* node2 = stack.pop_node();  // [1] 제거
                if (node2) {
                    stack.push(99);  // 새 노드 추가
                    stack.push_node(node1);  // [2] 다시 추가 (같은 주소)
                    // node2는 의도적으로 누수 (테스트용)
                }
            }
            
            thread_b_done.store(true, std::memory_order_release);
        });
        
        thread_a.join();
        thread_b.join();
        
        // ABA가 발생했는지 확인
        auto* new_head = stack.get_head();
        if (new_head == original_head && new_head->next != original_next) {
            aba_detected.fetch_add(1);
        }
        
        iterations_completed.fetch_add(1);
    }
    
    std::cout << "테스트 완료: " << iterations_completed.load() << " 반복\n";
    std::cout << "ABA 상황 탐지: " << aba_detected.load() << " 회\n\n";
    
    // ABA 문제는 타이밍에 크게 의존하므로 항상 발생하지는 않음
    SUCCEED();
}

// ============================================
// Part 4: 메모리 재사용으로 인한 ABA
// ============================================

/**
 * 실제 시스템에서 ABA 문제가 발생하는 주요 원인:
 * 메모리 할당자가 해제된 메모리를 재사용
 * 
 * 이 테스트는 그 상황을 시뮬레이션합니다.
 */
TEST(ABAStackTest, MemoryReuseABA) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════\n";
    std::cout << "         메모리 재사용 ABA 테스트\n";
    std::cout << "═══════════════════════════════════════════════════════\n\n";
    
    std::set<void*> seen_addresses;
    int reuse_count = 0;
    
    // 많은 push/pop을 반복하면서 주소 재사용 관찰
    lockfree::ABAProneStack<int> stack;
    
    for (int i = 0; i < 1000; ++i) {
        stack.push(i);
        auto* head = stack.get_head();
        
        if (seen_addresses.count(head) > 0) {
            reuse_count++;
        }
        seen_addresses.insert(head);
        
        stack.pop();
    }
    
    std::cout << "총 할당: 1000회\n";
    std::cout << "고유 주소: " << seen_addresses.size() << "개\n";
    std::cout << "주소 재사용: " << reuse_count << "회\n\n";
    
    // 메모리 할당자에 따라 재사용이 발생할 수 있음
    SUCCEED();
}

// ============================================
// Part 5: 당신의 과제! 🎯
// ============================================

/**
 * ═══════════════════════════════════════════════════════════════
 * 🎯 과제: ABA 문제를 해결하세요!
 * ═══════════════════════════════════════════════════════════════
 * 
 * 힌트 1: Tagged Pointer (태그가 붙은 포인터)
 * ─────────────────────────────────────────────
 * 포인터에 버전 번호(tag)를 함께 저장하면?
 * 
 *   현재:  head = 0x12345678 (순수 포인터)
 *   
 *   개선:  head = 0x12345678 | (tag << 48)
 *          하위 48비트: 포인터
 *          상위 16비트: 버전 번호
 * 
 * 매번 수정할 때마다 tag를 증가시키면,
 * 같은 주소라도 tag가 다르므로 CAS가 실패!
 * 
 * 
 * 힌트 2: Double-Width CAS
 * ─────────────────────────
 * 128비트 CAS를 사용하면?
 * 
 *   struct TaggedPointer {
 *       Node* ptr;      // 64비트
 *       uint64_t tag;   // 64비트
 *   };
 * 
 * std::atomic<TaggedPointer>로 두 값을 동시에 비교!
 * (단, 플랫폼 지원 필요: x86-64의 CMPXCHG16B)
 * 
 * 
 * 힌트 3: Hazard Pointer
 * ─────────────────────────
 * "이 포인터 사용 중!" 표시를 하면?
 * 
 * 1. pop 전에 head를 "hazard"로 등록
 * 2. 다른 스레드는 hazard 포인터를 삭제 못함
 * 3. pop 완료 후 hazard 해제
 * 4. 삭제는 아무도 사용 안 할 때만!
 * 
 * 
 * 힌트 4: Epoch-based Reclamation
 * ─────────────────────────────────
 * 시간(epoch) 기반으로 안전하게 삭제:
 * 
 * 1. 전역 epoch 카운터 유지
 * 2. 스레드가 작업 시작할 때 현재 epoch 기록
 * 3. 삭제할 노드는 "은퇴 목록"에 추가
 * 4. 모든 스레드가 그 epoch을 지나면 안전하게 삭제
 * 
 * ═══════════════════════════════════════════════════════════════
 * 
 * 아래 테스트를 통과하도록 ABA-safe 스택을 구현해보세요!
 * 
 * 파일: include/lockfree/aba_safe_stack.hpp
 */

// TODO: 이 테스트가 통과하도록 aba_safe_stack.hpp를 구현하세요!
/*
TEST(ABASafeStackTest, NoABAProblem) {
    lockfree::ABASafeStack<int> stack;
    
    std::atomic<bool> aba_detected{false};
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 10000;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&stack, &aba_detected, i]() {
            for (int j = 0; j < OPS_PER_THREAD; ++j) {
                stack.push(i * OPS_PER_THREAD + j);
                auto val = stack.pop();
                // 값이 corruption 없이 정상적이어야 함
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_FALSE(aba_detected.load());
}
*/

// ============================================
// Part 6: 생각해볼 질문들
// ============================================

/**
 * Q1: 왜 일반적인 mutex를 쓰면 ABA 문제가 없을까?
 * 
 * Q2: SPSC Queue에서는 ABA 문제가 발생할까? 왜?
 * 
 * Q3: Tagged Pointer 방식의 단점은 무엇일까?
 *     (힌트: tag 오버플로우, 포인터 크기 제한)
 * 
 * Q4: Hazard Pointer vs Epoch-based: 어떤 상황에서 어떤 게 좋을까?
 * 
 * Q5: Java나 Go 같은 GC 언어에서는 ABA 문제가 없을까?
 */

