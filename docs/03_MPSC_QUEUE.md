# Phase 3: MPSC Queue (Multi Producer Single Consumer)

이 문서는 여러 생산자가 동시에 데이터를 추가할 수 있는 Lock-Free Queue의 설계와 구현을 안내합니다.

---

## 1. MPSC Queue란?

### 1.1 정의

**MPSC (Multi Producer Single Consumer) Queue**:
- **여러 스레드**가 동시에 데이터를 추가 (Producer)
- **1개의 스레드**만 데이터를 제거 (Consumer)
- SPSC보다 복잡: Producer 간의 충돌 해결 필요

### 1.2 실제 사용 사례

```
┌─────────────┐
│  Thread 1   │──┐
│ (Producer)  │  │
└─────────────┘  │
                 │    ┌─────────────────┐    ┌─────────────┐
┌─────────────┐  ├───►│   MPSC Queue    │───►│   Logger    │
│  Thread 2   │──┤    │                 │    │  (Consumer) │
│ (Producer)  │  │    └─────────────────┘    └─────────────┘
└─────────────┘  │
                 │
┌─────────────┐  │
│  Thread N   │──┘
│ (Producer)  │
└─────────────┘
```

**사용 사례:**
- **로깅 시스템**: 여러 스레드가 로그 메시지 생성 → 1개의 로거가 파일에 기록
- **이벤트 수집**: 여러 소스에서 이벤트 발생 → 1개의 핸들러가 처리
- **메트릭 수집**: 여러 컴포넌트에서 통계 → 1개의 수집기가 집계
- **작업 제출**: 여러 클라이언트가 작업 요청 → 1개의 워커가 처리

---

## 2. SPSC vs MPSC 차이점

### 2.1 SPSC의 단순함

```
SPSC: Producer가 1개이므로 head 충돌 없음

Producer (1개)                    Consumer (1개)
     │                                 │
     ▼                                 ▼
   head ──────────────────────────── tail
     │                                 │
     │  (나만 수정)                     │  (나만 수정)
     ▼                                 ▼
   버퍼에 쓰기                        버퍼에서 읽기
```

### 2.2 MPSC의 복잡함

```
MPSC: 여러 Producer가 동시에 같은 head를 수정하려 함!

Producer 1 ─┐
            │
Producer 2 ─┼──► head ← 충돌 발생!
            │      │
Producer 3 ─┘      ▼
                 버퍼에 쓰기
```

**핵심 문제:** 
- 두 Producer가 동시에 `head` 위치를 읽음
- 둘 다 같은 슬롯에 쓰려고 함 → 데이터 덮어쓰기!

---

## 3. 해결 방법: CAS (Compare-And-Swap)

### 3.1 CAS 기반 head 업데이트

```cpp
bool push(const T& value) {
    size_t head;
    do {
        head = head_.load(std::memory_order_relaxed);
        
        // 큐가 가득 찼는지 확인
        if (head - tail_.load(std::memory_order_acquire) >= capacity()) {
            return false;
        }
        
        // CAS: head가 아직 같은 값이면 head+1로 업데이트
    } while (!head_.compare_exchange_weak(
        head,           // expected
        head + 1,       // desired
        std::memory_order_acq_rel
    ));
    
    // 이제 head 슬롯은 "나만" 사용
    buffer_[head & (Capacity - 1)] = value;
    
    return true;
}
```

### 3.2 CAS 동작 시나리오

```
초기 상태: head = 5

Producer A                          Producer B
──────────                          ──────────
1. head 읽기: 5                     
                                    2. head 읽기: 5
3. CAS(5→6) 시도                    
   → 성공! head = 6                 
   → 슬롯 5 확보                    
                                    4. CAS(5→6) 시도
                                       → 실패! (현재 head=6)
                                       → head 다시 읽기: 6
                                    5. CAS(6→7) 시도
                                       → 성공! head = 7
                                       → 슬롯 6 확보
```

**핵심:** CAS가 실패하면 루프를 돌며 재시도 → Lock-Free 보장

---

## 4. 설계 접근법

MPSC Queue를 구현하는 여러 방법이 있습니다:

### 4.1 접근법 1: 배열 기반 (Bounded)

```cpp
template<typename T, size_t Capacity>
class MPSCQueue {
private:
    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};  // 여러 Producer가 CAS로 경쟁
    alignas(64) std::atomic<size_t> tail_{0};  // Consumer만 수정
};
```

**장점:** 메모리 할당 없음, 캐시 친화적
**단점:** 고정 크기, 순서 보장 문제 (아래 설명)

### 4.2 접근법 2: 연결 리스트 기반 (Unbounded)

```cpp
template<typename T>
class MPSCQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
    };
    
    alignas(64) std::atomic<Node*> head_;  // Producer가 CAS로 추가
    alignas(64) Node* tail_;               // Consumer만 접근
};
```

**장점:** 무제한 크기, 순서 보장 용이
**단점:** 메모리 할당 필요, 캐시 미스 가능

### 4.3 접근법 3: 슬롯별 상태 추적 (권장)

```cpp
template<typename T, size_t Capacity>
class MPSCQueue {
private:
    struct Slot {
        T data;
        std::atomic<size_t> sequence;  // 슬롯 상태 추적
    };
    
    std::array<Slot, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};
```

**장점:** 순서 보장, 메모리 효율적
**이 방식을 권장합니다!**

---

## 5. 슬롯 시퀀스 방식 상세 설계

### 5.1 슬롯 상태 관리

각 슬롯에 `sequence` 번호를 부여:

```
초기화: slot[i].sequence = i (슬롯 번호와 동일)

┌─────────────────────────────────────────────────────┐
│ slot[0]  │ slot[1]  │ slot[2]  │ slot[3]  │ ...    │
│ seq=0    │ seq=1    │ seq=2    │ seq=3    │        │
│ (비어있음) │ (비어있음) │ (비어있음) │ (비어있음) │        │
└─────────────────────────────────────────────────────┘
```

### 5.2 Push 동작

```cpp
bool push(const T& value) {
    size_t pos = head_.load(std::memory_order_relaxed);
    
    for (;;) {
        Slot& slot = buffer_[pos & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;
        
        if (diff == 0) {
            // 슬롯이 비어있음 → CAS로 head 증가 시도
            if (head_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed)) {
                // 성공! 데이터 쓰기
                slot.data = value;
                slot.sequence.store(pos + 1, std::memory_order_release);
                return true;
            }
            // CAS 실패 → pos가 업데이트됨, 재시도
        } else if (diff < 0) {
            // 큐가 가득 참
            return false;
        } else {
            // 다른 Producer가 먼저 점유, 다음 위치로
            pos = head_.load(std::memory_order_relaxed);
        }
    }
}
```

### 5.3 Pop 동작

```cpp
bool pop(T& value) {
    Slot& slot = buffer_[tail_ & (Capacity - 1)];
    size_t seq = slot.sequence.load(std::memory_order_acquire);
    
    intptr_t diff = (intptr_t)seq - (intptr_t)(tail_ + 1);
    
    if (diff == 0) {
        // 데이터 있음!
        value = std::move(slot.data);
        slot.sequence.store(tail_ + Capacity, std::memory_order_release);
        ++tail_;
        return true;
    }
    
    // 큐가 비어있음
    return false;
}
```

**Consumer는 1개**이므로 CAS 불필요 (SPSC와 동일)

### 5.4 시퀀스 번호 의미

| sequence 값 | 의미 |
|-------------|------|
| `pos` | 슬롯이 비어있고, pos번째 push 가능 |
| `pos + 1` | 데이터가 쓰여있고, 읽기 가능 |
| `pos + Capacity` | 데이터가 읽혔고, 다음 라운드 push 가능 |

---

## 6. TDD 테스트 시나리오

### 6.1 기본 기능 테스트

```cpp
// 테스트 1: 초기 상태
TEST(MPSCQueue, InitialState) {
    MPSCQueue<int, 16> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
}

// 테스트 2: 단일 Producer push/pop
TEST(MPSCQueue, SingleProducerPushPop) {
    MPSCQueue<int, 16> queue;
    EXPECT_TRUE(queue.push(42));
    
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
}

// 테스트 3: 빈 큐에서 pop
TEST(MPSCQueue, PopFromEmpty) {
    MPSCQueue<int, 16> queue;
    int value;
    EXPECT_FALSE(queue.pop(value));
}

// 테스트 4: 가득 찬 큐에 push
TEST(MPSCQueue, PushToFull) {
    MPSCQueue<int, 4> queue;  // capacity = 3
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_FALSE(queue.push(4));  // full
}
```

### 6.2 멀티 Producer 테스트

```cpp
// 테스트 5: 2개 Producer 동시 push
TEST(MPSCQueue, TwoProducers) {
    MPSCQueue<int, 128> queue;
    constexpr int ITEMS_PER_PRODUCER = 1000;
    
    std::thread p1([&]() {
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            while (!queue.push(i)) std::this_thread::yield();
        }
    });
    
    std::thread p2([&]() {
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            while (!queue.push(i + 10000)) std::this_thread::yield();
        }
    });
    
    std::vector<int> results;
    std::thread consumer([&]() {
        for (int i = 0; i < ITEMS_PER_PRODUCER * 2; ++i) {
            int value;
            while (!queue.pop(value)) std::this_thread::yield();
            results.push_back(value);
        }
    });
    
    p1.join();
    p2.join();
    consumer.join();
    
    EXPECT_EQ(results.size(), ITEMS_PER_PRODUCER * 2);
    // 모든 값이 수신되었는지 확인
}

// 테스트 6: N개 Producer 스트레스 테스트
TEST(MPSCQueue, MultiProducerStress) {
    MPSCQueue<int, 1024> queue;
    constexpr int NUM_PRODUCERS = 8;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> done{false};
    
    // N개의 Producer
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
    
    // 1개의 Consumer
    std::thread consumer([&]() {
        while (!done.load() || !queue.empty()) {
            int value;
            if (queue.pop(value)) {
                total_popped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    
    for (auto& p : producers) p.join();
    done.store(true);
    consumer.join();
    
    EXPECT_EQ(total_pushed.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(total_popped.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}
```

---

## 7. 구현 체크리스트

- [ ] 템플릿 파라미터: `typename T, size_t Capacity`
- [ ] Capacity는 2의 거듭제곱 검증 (`static_assert`)
- [ ] Slot 구조체: `data` + `sequence`
- [ ] `push()`: CAS 루프로 head 경쟁
- [ ] `pop()`: Consumer 전용 (CAS 불필요)
- [ ] `empty()`, `full()`, `size()` 유틸리티
- [ ] False sharing 방지: `alignas(64)`
- [ ] 올바른 memory_order 사용

---

## 8. 흔한 실수들

### 실수 1: CAS 실패 시 재시도 안 함
```cpp
// 틀림
if (!head_.compare_exchange_weak(pos, pos + 1)) {
    return false;  // 재시도 해야 함!
}

// 맞음
while (!head_.compare_exchange_weak(pos, pos + 1)) {
    // pos가 자동 업데이트됨, 계속 시도
}
```

### 실수 2: 데이터 쓰기 전 sequence 업데이트
```cpp
// 틀림: Consumer가 쓰레기 값을 읽을 수 있음
slot.sequence.store(pos + 1, ...);
slot.data = value;

// 맞음: 데이터 먼저, 그 다음 sequence
slot.data = value;
slot.sequence.store(pos + 1, std::memory_order_release);
```

### 실수 3: Consumer에서 CAS 사용
```cpp
// 불필요: Consumer는 1개이므로
if (tail_.compare_exchange_weak(...))  // CAS 필요 없음

// 맞음: 단순 증가
++tail_;
```

---

## 9. 다음 단계

1. **테스트 먼저 작성**: `tests/test_mpsc_queue.cpp`
2. **구현**: `include/lockfree/mpsc_queue.hpp`
3. **테스트 통과**: 기본 + 멀티스레드 테스트
4. **코드 리뷰 요청**

**힌트가 필요하면 물어보세요!**
(정답 코드는 제공하지 않습니다 - 직접 구현해야 학습 효과가 있습니다)
