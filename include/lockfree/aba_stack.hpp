/**
 * ABA Problem Demonstration - Lock-Free Stack
 * 
 * 이 코드는 의도적으로 ABA 문제가 발생하도록 설계되었습니다.
 * 학습 목적으로만 사용하세요!
 * 
 * ABA 문제란?
 * - CAS(Compare-And-Swap) 연산에서 발생하는 미묘한 버그
 * - 값이 A → B → A로 변경되면, CAS는 "변경 없음"으로 잘못 판단
 */

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <chrono>

namespace lockfree {

/**
 * ABA 문제가 발생할 수 있는 Lock-Free Stack (Treiber Stack)
 * 
 * 구조:
 *   head → [Node A] → [Node B] → [Node C] → nullptr
 * 
 * Push: 새 노드를 head 앞에 삽입
 * Pop:  head 노드를 제거하고 반환
 */
template <typename T>
class ABAProneStack {
public:
    struct Node {
        T data;
        Node* next;
        
        explicit Node(const T& value) : data(value), next(nullptr) {}
        explicit Node(T&& value) : data(std::move(value)), next(nullptr) {}
    };

    ABAProneStack() : head_(nullptr) {}
    
    ~ABAProneStack() {
        // 남은 노드 정리
        while (pop()) {}
    }

    /**
     * Push 연산 - 새 노드를 스택 top에 추가
     * 
     * 알고리즘:
     * 1. 새 노드 생성
     * 2. 새 노드의 next를 현재 head로 설정
     * 3. CAS로 head를 새 노드로 변경
     * 4. 실패하면 2번부터 재시도
     */
    void push(const T& value) {
        Node* new_node = new Node(value);
        push_node(new_node);
    }
    
    void push_node(Node* new_node) {
        Node* old_head = head_.load(std::memory_order_relaxed);
        do {
            new_node->next = old_head;
        } while (!head_.compare_exchange_weak(
            old_head, 
            new_node,
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }

    /**
     * Pop 연산 - 스택 top에서 노드 제거
     * 
     * 알고리즘:
     * 1. 현재 head를 읽음
     * 2. head가 nullptr이면 실패
     * 3. CAS로 head를 head->next로 변경
     * 4. 실패하면 1번부터 재시도
     * 
     * ⚠️ ABA 문제 발생 지점:
     *    1번에서 head를 읽은 후, 3번 CAS 실행 전에
     *    다른 스레드가 head를 변경했다가 다시 원래대로 돌리면?
     */
    std::optional<T> pop() {
        Node* old_head = head_.load(std::memory_order_relaxed);
        
        do {
            if (old_head == nullptr) {
                return std::nullopt;
            }
            
            // ⚠️ 위험 지점: old_head->next 접근
            // old_head가 이미 삭제되고 메모리가 재사용되었다면?
            // → Dangling pointer 접근 = Undefined Behavior!
            
        } while (!head_.compare_exchange_weak(
            old_head,
            old_head->next,  // ⚠️ old_head가 유효하다고 가정
            std::memory_order_acquire,
            std::memory_order_relaxed
        ));
        
        T result = std::move(old_head->data);
        delete old_head;  // 메모리 해제 → 다른 곳에서 재사용 가능!
        return result;
    }

    /**
     * Pop 연산 (노드 반환 버전) - 테스트용
     * 노드를 삭제하지 않고 반환 (ABA 재현용)
     */
    Node* pop_node() {
        Node* old_head = head_.load(std::memory_order_relaxed);
        
        do {
            if (old_head == nullptr) {
                return nullptr;
            }
        } while (!head_.compare_exchange_weak(
            old_head,
            old_head->next,
            std::memory_order_acquire,
            std::memory_order_relaxed
        ));
        
        return old_head;  // 삭제하지 않고 반환
    }

    /**
     * ABA 문제 재현을 위한 특수 Pop
     * 
     * 중간에 인위적인 지연을 넣어 ABA 상황을 만듦
     */
    std::optional<T> pop_with_delay(std::chrono::milliseconds delay) {
        Node* old_head = head_.load(std::memory_order_relaxed);
        
        if (old_head == nullptr) {
            return std::nullopt;
        }
        
        // 여기서 head와 head->next를 읽음
        Node* next = old_head->next;
        
        // ═══════════════════════════════════════════
        // ⚠️ ABA 문제 발생 구간 시작
        // ═══════════════════════════════════════════
        // 이 지연 동안 다른 스레드가:
        // 1. head (Node A)를 pop
        // 2. 다른 노드들도 pop
        // 3. 새 노드를 push (메모리 재사용으로 같은 주소!)
        // 4. Node A를 다시 push (주소가 같음)
        // 
        // 결과: head 주소는 여전히 old_head와 같지만,
        //       next 포인터는 완전히 다른 값을 가리킴!
        // ═══════════════════════════════════════════
        
        std::this_thread::sleep_for(delay);
        
        // ═══════════════════════════════════════════
        // ⚠️ ABA 문제 발생 구간 끝
        // ═══════════════════════════════════════════
        
        // CAS 시도: "head가 아직 old_head와 같으니 변경 없음!"
        // 하지만 실제로는 스택 구조가 완전히 바뀌었을 수 있음
        if (head_.compare_exchange_strong(
            old_head,
            next,  // ⚠️ 오래된 next 값 사용 - 잘못된 포인터일 수 있음!
            std::memory_order_acquire,
            std::memory_order_relaxed
        )) {
            T result = std::move(old_head->data);
            // delete old_head;  // 테스트를 위해 삭제 안 함
            return result;
        }
        
        return std::nullopt;  // CAS 실패
    }

    Node* get_head() const {
        return head_.load(std::memory_order_acquire);
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == nullptr;
    }

private:
    std::atomic<Node*> head_;
};

} // namespace lockfree
