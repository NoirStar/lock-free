/**
 * ABA-Safe Lock-Free Stack using Packed Tagged Pointer
 * 
 * Packed Tagged Pointer 방식으로 ABA 문제를 해결한 Lock-Free Stack
 * 
 * 핵심 아이디어:
 *   x64에서 가상 주소는 48비트만 사용 → 상위 16비트를 태그로 활용
 *   8바이트 atomic으로 진짜 lock-free 보장!
 * 
 * ┌─────────────────────────────────────────────────────────┐
 * │  64-bit atomic value (std::uintptr_t)                   │
 * ├─────────────────┬───────────────────────────────────────┤
 * │  Tag (16 bits)  │  Pointer (48 bits)                    │
 * └─────────────────┴───────────────────────────────────────┘
 * 
 * 왜 8바이트여야 하는가?
 *   - x64 CPU는 8바이트 CAS를 하드웨어로 지원 (lock cmpxchg)
 *   - 16바이트는 CMPXCHG16B 필요 → 일부 플랫폼에서 미지원
 *   - std::atomic<16bytes>는 내부 mutex 사용할 수 있음!
 */

#pragma once

#include <atomic>
#include <optional>
#include <cstdint>
#include <cassert>

namespace lockfree {

/**
 * ABA-Safe Lock-Free Stack (Treiber Stack with Packed Tagged Pointer)
 * 
 * 진짜 Lock-Free를 보장하는 구현
 */
template <typename T>
class ABASafeStack {
public:
    struct Node {
        T data;
        Node* next;
        
        explicit Node(const T& value) : data(value), next(nullptr) {}
        explicit Node(T&& value) : data(std::move(value)), next(nullptr) {}
    };

private:
    /**
     * Packed Tagged Pointer
     * 
     * 64비트를 다음과 같이 분할:
     *   [63:48] = 16비트 태그 (0 ~ 65535, 오버플로우 시 wrap around)
     *   [47:0]  = 48비트 포인터 (x64 가상 주소 공간)
     * 
     * x64에서 canonical address:
     *   - 유저 공간: 0x0000_0000_0000_0000 ~ 0x0000_7FFF_FFFF_FFFF
     *   - 커널 공간: 0xFFFF_8000_0000_0000 ~ 0xFFFF_FFFF_FFFF_FFFF
     *   → 상위 16비트는 sign extension이므로 태그로 사용 가능!
     */
    static constexpr int TAG_BITS = 16;
    static constexpr int PTR_BITS = 48;
    static constexpr std::uintptr_t PTR_MASK = (1ULL << PTR_BITS) - 1;
    static constexpr std::uintptr_t TAG_MASK = ~PTR_MASK;
    
    // 포인터 → packed 값
    static std::uintptr_t pack(Node* ptr, std::uint16_t tag) {
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr) & PTR_MASK;
        std::uintptr_t t = static_cast<std::uintptr_t>(tag) << PTR_BITS;
        return p | t;
    }
    
    // packed 값 → 포인터
    static Node* get_ptr(std::uintptr_t packed) {
        return reinterpret_cast<Node*>(packed & PTR_MASK);
    }
    
    // packed 값 → 태그
    static std::uint16_t get_tag(std::uintptr_t packed) {
        return static_cast<std::uint16_t>(packed >> PTR_BITS);
    }

    // head를 8바이트 atomic으로 관리 → 진짜 lock-free!
    std::atomic<std::uintptr_t> head_;

public:
    ABASafeStack() : head_(pack(nullptr, 0)) {
        // Lock-free 보장 확인 (컴파일 타임)
        static_assert(sizeof(std::uintptr_t) == 8, "Requires 64-bit platform");
        static_assert(std::atomic<std::uintptr_t>::is_always_lock_free, 
                      "std::atomic<uintptr_t> must be lock-free");
    }
    
    ~ABASafeStack() {
        while (pop()) {}
    }
    
    // 복사/이동 금지
    ABASafeStack(const ABASafeStack&) = delete;
    ABASafeStack& operator=(const ABASafeStack&) = delete;
    ABASafeStack(ABASafeStack&&) = delete;
    ABASafeStack& operator=(ABASafeStack&&) = delete;

    /**
     * Push 연산 - 새 노드를 스택 top에 추가
     * 
     * 알고리즘:
     * 1. 새 노드 생성
     * 2. 현재 head(packed)를 읽음
     * 3. 새 노드의 next를 현재 head의 포인터로 설정
     * 4. CAS로 head를 pack(new_node, old_tag + 1)로 변경
     * 5. 실패하면 2번부터 재시도
     * 
     * @param value 추가할 값
     */
    void push(const T& value) {
        Node* new_node = new Node(value);
        std::uintptr_t old_head = head_.load(std::memory_order_relaxed);
        
        do {
            new_node->next = get_ptr(old_head);
        } while (!head_.compare_exchange_weak(
            old_head,
            pack(new_node, get_tag(old_head) + 1),
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }

    /**
     * Pop 연산 - 스택 top에서 노드 제거
     * 
     * 알고리즘:
     * 1. 현재 head(packed)를 읽음
     * 2. head의 포인터가 nullptr이면 실패
     * 3. CAS로 head를 pack(ptr->next, old_tag + 1)로 변경
     * 4. 실패하면 1번부터 재시도
     * 5. 성공하면 데이터 추출 후 노드 삭제
     * 
     * ABA 문제 해결:
     *   다른 스레드가 A를 pop하고 다시 push해도
     *   태그가 증가하므로 CAS가 실패함!
     * 
     * @return 제거된 값 (스택이 비었으면 nullopt)
     */
    std::optional<T> pop() {
        std::uintptr_t old_head = head_.load(std::memory_order_relaxed);
        
        do {
            Node* old_ptr = get_ptr(old_head);
            if (old_ptr == nullptr) {
                return std::nullopt;
            }
            
            // CAS: head를 ptr->next로 변경 (태그 증가)
        } while (!head_.compare_exchange_weak(
            old_head,
            pack(get_ptr(old_head)->next, get_tag(old_head) + 1),
            std::memory_order_acquire,
            std::memory_order_relaxed
        ));

        Node* old_ptr = get_ptr(old_head);
        T value = std::move(old_ptr->data);
        delete old_ptr;
        return value;
    }

    /**
     * 스택이 비어있는지 확인
     */
    bool empty() const {
        return get_ptr(head_.load(std::memory_order_acquire)) == nullptr;
    }
    
    /**
     * Lock-Free 여부 확인 (디버깅용)
     */
    static bool is_lock_free() {
        return std::atomic<std::uintptr_t>::is_always_lock_free;
    }
};

} // namespace lockfree
