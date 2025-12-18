/**
 * Lock-Free Memory Pool (범용)
 * 
 * 게임 엔진, 서버 등에서 실제로 사용되는 형태의 Object Pool
 * 
 * 특징:
 *   - 런타임에 크기 결정
 *   - 동적 확장 지원 (선택)
 *   - Lock-Free 할당/해제
 *   - 캐시 라인 정렬
 * 
 * 사용 예:
 *   MemoryPool<Bullet> pool(1000);
 *   Bullet* b = pool.construct(x, y, speed);
 *   pool.destroy(b);
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │                     Memory Pool Architecture                 │
 * │                                                              │
 * │  Chunk 0            Chunk 1            Chunk 2               │
 * │  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐      │
 * │  │[B0][B1][B2] │    │[B0][B1][B2] │    │[B0][B1][B2] │ ...  │
 * │  └─────────────┘    └─────────────┘    └─────────────┘      │
 * │                                                              │
 * │  free_list_ ──► [Block] ──► [Block] ──► [Block] ──► nullptr │
 * └─────────────────────────────────────────────────────────────┘
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>
#include <memory>
#include <vector>
#include <cassert>

namespace lockfree {

/**
 * Lock-Free Memory Pool
 * 
 * @tparam T 저장할 타입
 */
template <typename T>
class MemoryPool {
public:
    // ========================================
    // 상수
    // ========================================
    
    // 캐시 라인 크기 (일반적으로 64바이트)
    static constexpr std::size_t CACHE_LINE_SIZE = 64;

private:
    // ========================================
    // 내부 구조체
    // ========================================
    
    /**
     * Free List 노드
     * 
     * Intrusive 방식: 미사용 블록을 FreeNode로 해석
     * - 사용 중: 블록은 T 데이터를 저장
     * T* ptr = reinterpret_cast<T*>(free_node);
     * - 미사용: 블록은 FreeNode로서 next 포인터 저장
     * 
     * 메모리 오버헤드 없음!
     */
    struct FreeNode {
        FreeNode* next;
    };
    
    /**
     * Tagged Pointer (ABA 문제 해결) - 64비트 버전
     * 
     * ┌─────────────────────────────────────────────────────────────┐
     * │  왜 64비트여야 하는가?                                        │
     * │                                                              │
     * │  문제: 16바이트(128비트) TaggedPtr                            │
     * │  ┌────────────────────────────────────────┐                 │
     * │  │ ptr (8바이트) │ tag (8바이트)           │ = 16바이트      │
     * │  └────────────────────────────────────────┘                 │
     * │                                                              │
     * │  - x86-64에서 128비트 CAS는 CMPXCHG16B 명령어 필요           │
     * │  - 모든 CPU가 지원하지 않음 → 내부적으로 mutex 사용!          │
     * │  - Lock-Free가 아니게 됨!                                     │
     * │                                                              │
     * │  해결: 64비트에 포인터 + 태그 패킹                            │
     * │  ┌────────────────────────────────────────┐                 │
     * │  │ tag (16비트) │ pointer (48비트)         │ = 8바이트       │
     * │  └────────────────────────────────────────┘                 │
     * │                                                              │
     * │  - x86-64 가상 주소는 48비트만 사용 (현재)                    │
     * │  - 상위 16비트를 태그로 활용 가능!                            │
     * │  - 64비트 CAS는 모든 x86-64에서 lock-free!                   │
     * └─────────────────────────────────────────────────────────────┘
     * 
     * 포인터 + 태그를 단일 64비트 값에 패킹:
     *   - 하위 48비트: 포인터 주소
     *   - 상위 16비트: 버전 태그 (0 ~ 65535)
     * 
     * 비트 레이아웃:
     *   [63 ─ 48][47 ─────────────────────────────── 0]
     *   │  tag   │              pointer               │
     *   └────────┴────────────────────────────────────┘
     * 
     * ABA 방지 원리:
     *   A(tag=0) → B → C
     *   A가 pop되고 다시 push되면 A(tag=1)
     *   CAS(A(tag=0), ...) 실패! (태그 불일치)
     * 
     * 주의사항:
     *   - x86-64 canonical address: 48비트 사용 (현재)
     *   - 5-level paging 활성화 시 57비트 사용 → 태그 비트 감소 필요
     *   - ARM64도 유사하게 48비트 또는 52비트 주소 사용
     */
    class TaggedPtr {
    private:
        // 64비트 단일 값으로 포인터와 태그를 함께 저장
        std::uint64_t value_;
        
        // 비트 마스크 상수
        static constexpr std::uint64_t PTR_MASK = 0x0000FFFFFFFFFFFF; // 하위 48비트
        static constexpr std::uint64_t TAG_MASK = 0xFFFF000000000000; // 상위 16비트
        static constexpr int TAG_SHIFT = 48;
        
    public:
        // 기본 생성자
        TaggedPtr() : value_(0) {}
        
        // 포인터 + 태그로 생성
        TaggedPtr(FreeNode* ptr, std::uint16_t tag) {
            std::uint64_t ptr_val = reinterpret_cast<std::uint64_t>(ptr) & PTR_MASK;
            std::uint64_t tag_val = static_cast<std::uint64_t>(tag) << TAG_SHIFT;
            value_ = ptr_val | tag_val;
        }
        
        // 포인터 추출
        FreeNode* ptr() const {
            return reinterpret_cast<FreeNode*>(value_ & PTR_MASK);
        }
        
        // 태그 추출
        std::uint16_t tag() const {
            return static_cast<std::uint16_t>((value_ & TAG_MASK) >> TAG_SHIFT);
        }
        
        // 비교 연산자 (atomic CAS에서 사용)
        bool operator==(const TaggedPtr& other) const {
            return value_ == other.value_;
        }
        
        bool operator!=(const TaggedPtr& other) const {
            return value_ != other.value_;
        }
    };
    
    // 컴파일 타임 검증: TaggedPtr이 정확히 8바이트(64비트)인지 확인
    static_assert(sizeof(TaggedPtr) == 8, "TaggedPtr must be exactly 64 bits for lock-free CAS");
    
    /**
     * 블록 정렬 (먼저 계산)
     * 
     * T와 FreeNode 중 더 큰 정렬 요구사항 사용
     * - 정렬 안 맞으면: 성능 저하 또는 크래시!
     * char[50] a => alignof(a) == 1, sizeof(a) == 50
     */
    static constexpr std::size_t BLOCK_ALIGNMENT = 
        (alignof(T) > alignof(FreeNode)) ? alignof(T) : alignof(FreeNode);
    
    /**
     * 블록 크기 계산 (정렬의 배수로 올림)
     * 
     * 1. max(sizeof(T), sizeof(FreeNode)) - T가 포인터보다 작을 수 있음
     * 2. 정렬 경계로 올림 - 연속된 블록이 모두 정렬되도록!
     * 
     * 예: T=50바이트, 정렬=8 → BLOCK_SIZE=56 (8의 배수)
     */
    static constexpr std::size_t RAW_BLOCK_SIZE = 
        (sizeof(T) > sizeof(FreeNode)) ? sizeof(T) : sizeof(FreeNode);
    
    static constexpr std::size_t BLOCK_SIZE = 
        (RAW_BLOCK_SIZE + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
    
    /**
     * 메모리 청크 (동적 확장 단위)
     * 
     * 풀이 가득 차면 새 청크를 할당하여 확장
     */
    struct Chunk {
        std::unique_ptr<std::byte[]> memory;
        std::size_t block_count;
        
        /**
         * 청크 생성자
         * 
         * 1. block_count * BLOCK_SIZE + BLOCK_ALIGNMENT 크기 할당
         * 2. 여유 공간은 정렬을 위한 것
         * 
         * @param count 블록 수
         */
        explicit Chunk(std::size_t count) 
            : block_count(count) 
        {
            memory = std::make_unique<std::byte[]>(count * BLOCK_SIZE + BLOCK_ALIGNMENT);
        }
        
        /**
         * 정렬된 시작 주소
         * 
         * 메모리 주소를 BLOCK_ALIGNMENT 경계에 맞춤
         * 공식: aligned = (addr + alignment - 1) & ~(alignment - 1)
         * 
         * @return 정렬된 시작 주소
         */
        std::byte* aligned_start() const {
            std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(memory.get());
            std::uintptr_t aligned = (addr + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
            return reinterpret_cast<std::byte*>(aligned);
        }
        
        /**
         * i번째 블록 주소
         * 
         * @param index 블록 인덱스
         * @return 블록 주소
         */
        void* block_at(std::size_t index) const {
            if (index < block_count) {
                return aligned_start() + index * BLOCK_SIZE;
            }
            return nullptr;
        }
    };

    // ========================================
    // 멤버 변수
    // ========================================
    
    /**
     * Free List Head (Tagged Pointer)
     * 
     * Lock-Free push/pop을 위한 atomic 포인터
     * TaggedPtr로 ABA 문제 해결!
     * 
     * 왜 atomic이어야 하는가?
     * - 여러 스레드가 동시에 read/write
     * - CAS 연산을 위해 필수
     */
    std::atomic<TaggedPtr> free_list_{TaggedPtr{nullptr, 0}};
    
    /**
     * 메모리 청크 목록
     * 
     * 동적 확장 시 새 청크 추가
     */
    std::vector<Chunk> chunks_;
    
    /**
     * 청크 접근 보호용 스핀락
     * 
     * 청크 추가는 드물게 발생하므로 스핀락 사용
     * Free List 자체는 Lock-Free!
     */
    mutable std::atomic_flag chunks_lock_ = ATOMIC_FLAG_INIT;
    
    /**
     * 통계
     */
    std::atomic<std::size_t> total_blocks_{0};
    std::atomic<std::size_t> allocated_count_{0};
    
    /**
     * 설정
     */
    std::size_t chunk_size_;
    bool growable_;

public:
    // ========================================
    // 생성자 / 소멸자
    // ========================================
    
    /**
     * 생성자
     * 
     * @param initial_capacity 초기 블록 수 (기본: 1024)
     * @param growable         풀 확장 허용 여부 (기본: true)
     * @param chunk_size       확장 시 청크 크기 (기본: initial_capacity)
     */
    explicit MemoryPool(
        std::size_t initial_capacity = 1024,
        bool growable = true,
        std::size_t chunk_size = 0
    ) : chunk_size_(chunk_size > 0 ? chunk_size : initial_capacity),
        growable_(growable)
    {
        add_chunk(initial_capacity);
    }
    
    /**
     * 소멸자
     */
    ~MemoryPool() {
        // chunks_의 unique_ptr이 자동으로 메모리 해제
        // 디버그: 할당된 블록이 모두 반환되었는지 확인
        assert(allocated_count_.load() == 0 && "Memory leak: some blocks not deallocated");
    }
    
    // 복사/이동 금지
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    // ========================================
    // 핵심 API
    // ========================================
    
    /**
     * 메모리 할당
     * 
     * Free List에서 블록을 Lock-Free로 pop
     * 
     * 블록 크기는 컴파일 타임에 계산됨:
     *   BLOCK_SIZE = align(max(sizeof(T), sizeof(FreeNode)))
     * 따라서 T가 아무리 커도 블록이 충분한 공간을 가짐!
     * 
     * @return 할당된 메모리 포인터, 실패 시 nullptr
     */
    T* allocate() {
        FreeNode* node = pop_free_node();
        
        // 풀이 비었으면 확장 시도
        if (node == nullptr && growable_) {
            add_chunk(chunk_size_);
            node = pop_free_node();
        }
        
        if (node != nullptr) {
            allocated_count_.fetch_add(1, std::memory_order_relaxed);
            return reinterpret_cast<T*>(node);
        }
        
        return nullptr;  // 확장 불가 또는 확장 후에도 실패
    }
    
    /**
     * 메모리 해제
     * 
     * 블록을 Free List에 Lock-Free로 push
     * 
     * @param ptr 반환할 메모리 포인터
     */
    void deallocate(T* ptr) {
        if (ptr == nullptr) return;
        push_free_node(reinterpret_cast<FreeNode*>(ptr));
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    /**
     * 할당 + 생성자 호출
     * 
     * 게임 엔진 스타일: pool.construct(args...)
     * 
     * @param args 생성자 인자
     * @return 생성된 객체 포인터, 실패 시 nullptr
     */
    template <typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        if (ptr) {
            new (ptr) T(std::forward<Args>(args)...);  // placement new
        }
        return ptr;
    }
    
    /**
     * 소멸자 호출 + 해제
     * 
     * 게임 엔진 스타일: pool.destroy(obj)
     * 
     * @param ptr 해제할 객체 포인터
     */
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();        // 소멸자 명시적 호출
            deallocate(ptr);  // 메모리 반환
        }
    }

    // ========================================
    // 유틸리티
    // ========================================
    
    /**
     * 전체 용량
     */
    std::size_t capacity() const {
        return total_blocks_.load(std::memory_order_relaxed);
    }
    
    /**
     * 현재 할당된 블록 수
     */
    std::size_t allocated_count() const {
        return allocated_count_.load(std::memory_order_relaxed);
    }
    
    /**
     * 사용 가능한 블록 수
     */
    std::size_t available_count() const {
        return capacity() - allocated_count();
    }
    
    /**
     * 청크 수
     */
    std::size_t chunk_count() const {
        acquire_chunks_lock();
        std::size_t count = chunks_.size();
        release_chunks_lock();
        return count;
    }
    
    /**
     * 확장 가능 여부
     */
    bool is_growable() const {
        return growable_;
    }
    
    /**
     * 블록 크기
     */
    static constexpr std::size_t block_size() {
        return BLOCK_SIZE;
    }
    
    /**
     * Lock-Free 여부
     * 
     * TaggedPtr이 64비트이므로:
     * - x86-64: 항상 Lock-Free (CMPXCHG8B/CMPXCHG 사용)
     * - ARM64: 항상 Lock-Free (LDXR/STXR 사용)
     * - 32비트 시스템: Lock-Free 보장 안 됨
     */
    static bool is_lock_free() {
        static_assert(sizeof(TaggedPtr) == 8, "TaggedPtr must be 64 bits");
        return std::atomic<TaggedPtr>::is_always_lock_free;
    }

private:
    // ========================================
    // 내부 구현
    // ========================================
    
    /**
     * 청크 락 획득 (스핀락)
     */
    void acquire_chunks_lock() const {
        while (chunks_lock_.test_and_set(std::memory_order_acquire)) {
            // spin-wait
        }
    }
    
    /**
     * 청크 락 해제
     */
    void release_chunks_lock() const {
        chunks_lock_.clear(std::memory_order_release);
    }
    
    /**
     * 새 청크 추가
     * 
     * 알고리즘:
     *   1. 스핀락으로 chunks_ 벡터 보호
     *   2. 새 Chunk 생성 및 추가
     *   3. 청크의 모든 블록을 free list에 push
     *   4. total_blocks_ 업데이트
     * 
     * @param block_count 청크의 블록 수
     * 
     */
    void add_chunk(std::size_t block_count) {
        acquire_chunks_lock();
        chunks_.emplace_back(block_count);
        Chunk& new_chunk = chunks_.back();
        release_chunks_lock();
        
        for (std::size_t i = 0; i < block_count; ++i) {
            void* block_ptr = new_chunk.block_at(i);
            push_free_node(reinterpret_cast<FreeNode*>(block_ptr));
        }
        
        total_blocks_.fetch_add(block_count, std::memory_order_relaxed);
    }
    
    /**
     * Free List에서 노드 pop (Lock-Free, ABA-Safe)
     * 
     * Tagged Pointer로 ABA 문제 해결:
     *   - old_head의 tag와 현재 tag가 다르면 CAS 실패
     *   - 매 pop마다 tag 증가
     * 
     * @return 꺼낸 노드, 비어있으면 nullptr
     */
    FreeNode* pop_free_node() {
        TaggedPtr old_head = free_list_.load(std::memory_order_relaxed);
        
        while (old_head.ptr() != nullptr) {
            // 새 head: 다음 노드 + 태그 증가
            TaggedPtr new_head{old_head.ptr()->next, 
                              static_cast<std::uint16_t>(old_head.tag() + 1)};
            
            if (free_list_.compare_exchange_weak(
                old_head,   // expected (64비트 전체가 일치해야 성공)
                new_head,   // desired
                std::memory_order_acquire,
                std::memory_order_relaxed
            )) {
                return old_head.ptr();
            }
            // 실패 시 old_head가 현재 값으로 갱신됨
        }
        return nullptr;
    }
    
    /**
     * Free List에 노드 push (Lock-Free, ABA-Safe)
     * 
     * Tagged Pointer로 ABA 문제 해결:
     *   - 매 push마다 tag 증가
     *   - 일관성 유지
     * 
     * @param node 추가할 노드
     */
    void push_free_node(FreeNode* node) {
        TaggedPtr old_head = free_list_.load(std::memory_order_relaxed);
        TaggedPtr new_head;
        
        do {
            node->next = old_head.ptr();
            new_head = TaggedPtr{node, static_cast<std::uint16_t>(old_head.tag() + 1)};
        } while (!free_list_.compare_exchange_weak(
            old_head,   // expected (64비트 전체가 일치해야 성공)
            new_head,   // desired
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }
};

// ========================================
// 편의 타입 별칭
// ========================================

/**
 * 고정 크기 메모리 풀 (확장 불가)
 */
template <typename T>
class FixedMemoryPool : public MemoryPool<T> {
public:
    explicit FixedMemoryPool(std::size_t capacity)
        : MemoryPool<T>(capacity, false) {}
};

/**
 * 캐시라인 정렬 풀 (False Sharing 방지)
 */
template <typename T>
using CacheAlignedPool = MemoryPool<T>;

} // namespace lockfree
