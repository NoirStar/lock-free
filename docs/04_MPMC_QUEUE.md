```markdown
# Phase 4: MPMC Queue (Multi Producer Multi Consumer)

이 문서는 여러 생산자와 여러 소비자가 동시에 접근할 수 있는 Lock-Free Queue의 설계와 구현을 안내합니다.

---

## 1. MPMC Queue란?

### 1.1 정의

**MPMC (Multi Producer Multi Consumer) Queue**:
- **여러 스레드**가 동시에 데이터를 추가 (Producer)
- **여러 스레드**가 동시에 데이터를 제거 (Consumer)
- 가장 복잡한 Lock-Free Queue: Producer 간, Consumer 간, Producer-Consumer 간 충돌 해결 필요

### 1.2 실제 사용 사례

```
┌─────────────┐                                    ┌─────────────┐
│  Producer 1 │──┐                           ┌────►│  Consumer 1 │
└─────────────┘  │                           │     └─────────────┘
                 │                           │
┌─────────────┐  │    ┌─────────────────┐    │     ┌─────────────┐
│  Producer 2 │──┼───►│   MPMC Queue    │────┼────►│  Consumer 2 │
└─────────────┘  │    └─────────────────┘    │     └─────────────┘
                 │                           │
┌─────────────┐  │                           │     ┌─────────────┐
│  Producer N │──┘                           └────►│  Consumer M │
└─────────────┘                                    └─────────────┘
```

**사용 사례:**
- **Thread Pool Work Queue**: 여러 스레드가 작업을 제출하고, 여러 워커가 처리
- **메시지 브로커**: 여러 발행자와 여러 구독자
- **분산 처리**: 여러 소스에서 데이터 수집, 여러 처리기가 소비
- **이벤트 버스**: 여러 컴포넌트가 이벤트 발행/구독

---

## 2. 복잡성 분석

### 2.1 각 Queue 타입별 동기화 요구사항

| Queue 타입 | head 경쟁 | tail 경쟁 | 복잡도 |
|-----------|----------|----------|--------|
| SPSC | ❌ (1 Producer) | ❌ (1 Consumer) | 가장 단순 |
| MPSC | ✅ (N Producers) | ❌ (1 Consumer) | 중간 |
| SPMC | ❌ (1 Producer) | ✅ (M Consumers) | 중간 |
| MPMC | ✅ (N Producers) | ✅ (M Consumers) | 가장 복잡 |

### 2.2 MPMC의 핵심 도전

```
동시에 발생할 수 있는 충돌:

Producer 1 ─┐                        ┌─ Consumer 1
            ├──► head 경쟁    tail 경쟁 ◄──┤
Producer 2 ─┘                        └─ Consumer 2

+ 동시에 Producer가 push하고 Consumer가 pop할 때
  같은 슬롯을 건드릴 수 있음!
```

---

## 3. MPSC에서 MPMC로 확장

### 3.1 MPSC와의 차이점

**MPSC (이미 구현됨):**
```cpp
bool pop(T& value) {
    // Consumer가 1개이므로 CAS 불필요
    size_t tail = tail_.load(std::memory_order_relaxed);
    Slot& slot = buffer_[tail & (Capacity - 1)];
    // ...
    tail_.store(tail + 1, std::memory_order_release);  // 단순 store
    return true;
}
```

**MPMC (구현 필요):**
```cpp
bool pop(T& value) {
    size_t pos = tail_.load(std::memory_order_relaxed);
    
    for (;;) {
        Slot& slot = buffer_[pos & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        
        if (diff == 0) {
            // CAS로 tail 경쟁!
            if (tail_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed)) {
                value = std::move(slot.data);
                slot.sequence.store(pos + Capacity, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;  // 큐가 비어있음
        } else {
            pos = tail_.load(std::memory_order_relaxed);
        }
    }
}
```

### 3.2 핵심 변경점

| 부분 | MPSC | MPMC |
|------|------|------|
| push() | CAS 사용 | CAS 사용 (동일) |
| pop() | 단순 store | **CAS 사용** |
| tail 접근 | Consumer 1개만 | **여러 Consumer 경쟁** |

---

## 4. 슬롯 시퀀스 방식 (양방향)

### 4.1 시퀀스 상태 전이

```
┌──────────────────────────────────────────────────────────┐
│                                                          │
│    pos          pos+1         pos+Capacity               │
│     │             │               │                      │
│     ▼             ▼               ▼                      │
│  [비어있음] ──► [데이터있음] ──► [다음 라운드 준비]      │
│     │             │               │                      │
│  Producer      Consumer        Producer                  │
│  가 획득       가 소비          가 재사용               │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

### 4.2 Push 동작 (MPSC와 동일)

```cpp
bool push(const T& value) {
    size_t pos = head_.load(std::memory_order_relaxed);
    
    for (;;) {
        Slot& slot = buffer_[pos & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;
        
        if (diff == 0) {
            // 슬롯이 비어있음 → CAS로 head 증가
            if (head_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed)) {
                slot.data = value;
                slot.sequence.store(pos + 1, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;  // 큐가 가득 참
        } else {
            pos = head_.load(std::memory_order_relaxed);
        }
    }
}
```

### 4.3 Pop 동작 (CAS 추가!)

```cpp
bool pop(T& value) {
    size_t pos = tail_.load(std::memory_order_relaxed);
    
    for (;;) {
        Slot& slot = buffer_[pos & (Capacity - 1)];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        
        if (diff == 0) {
            // 데이터가 준비됨 → CAS로 tail 증가
            if (tail_.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed)) {
                value = std::move(slot.data);
                slot.sequence.store(pos + Capacity, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;  // 큐가 비어있음
        } else {
            pos = tail_.load(std::memory_order_relaxed);
        }
    }
}
```

---

## 5. CAS 경쟁 시나리오

### 5.1 Consumer 간 경쟁

```
초기 상태: tail = 3, slot[3].sequence = 4 (데이터 있음)

Consumer A                          Consumer B
──────────                          ──────────
1. tail 읽기: 3
                                    2. tail 읽기: 3
3. seq 확인: 4 (= 3+1, 읽기 가능)
                                    4. seq 확인: 4 (읽기 가능)
5. CAS(3→4) 시도
   → 성공! tail = 4
   → slot[3] 데이터 획득
                                    6. CAS(3→4) 시도
                                       → 실패! (현재 tail=4)
                                       → tail 다시 읽기: 4
                                    7. slot[4] 확인하고 재시도
```

### 5.2 Producer-Consumer 동시 접근

```
극단적 케이스: 용량 4인 큐

상태: head=5, tail=2
┌───┬───┬───┬───┐
│ 5 │   │ 2 │ 3 │
└───┴───┴───┴───┘
  ↑       ↑
head=5  tail=2

Producer는 slot[1] (5 & 3)에 쓰려 함
Consumer는 slot[2] (2 & 3)에서 읽으려 함
→ 서로 다른 슬롯이므로 안전!

시퀀스 번호로 상태 추적:
- slot[1].seq = 5 → Producer 사용 가능
- slot[2].seq = 3 → Consumer가 읽은 후 6으로 업데이트
```

---

## 6. TDD 테스트 시나리오

### 6.1 기본 기능 테스트

```cpp
// 테스트 1: 초기 상태
TEST(MPMCQueue, InitialState) {
    MPMCQueue<int, 16> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.full());
}

// 테스트 2: 단일 스레드 push/pop
TEST(MPMCQueue, SingleThreadPushPop) {
    MPMCQueue<int, 16> queue;
    EXPECT_TRUE(queue.push(42));
    
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, 42);
}

// 테스트 3: 빈 큐에서 pop
TEST(MPMCQueue, PopFromEmpty) {
    MPMCQueue<int, 16> queue;
    int value;
    EXPECT_FALSE(queue.pop(value));
}

// 테스트 4: 가득 찬 큐에 push
TEST(MPMCQueue, PushToFull) {
    MPMCQueue<int, 4> queue;
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_TRUE(queue.push(4));
    EXPECT_FALSE(queue.push(5));  // full
}

// 테스트 5: FIFO 순서 확인 (단일 스레드)
TEST(MPMCQueue, FIFOOrderSingleThread) {
    MPMCQueue<int, 16> queue;
    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }
    for (int i = 0; i < 10; ++i) {
        int value;
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(value, i);
    }
}
```

### 6.2 Multi Producer 테스트

```cpp
// 테스트 6: 2개 Producer, 1개 Consumer
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
```

### 6.3 Multi Consumer 테스트

```cpp
// 테스트 7: 1개 Producer, 2개 Consumer
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
```

### 6.4 Full MPMC 테스트

```cpp
// 테스트 8: N Producer, M Consumer 스트레스
TEST(MPMCQueue, MultiProducerMultiConsumer) {
    MPMCQueue<int, 1024> queue;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    
    std::atomic<int> total_pushed{0};
    std::atomic<int> total_popped{0};
    std::atomic<bool> producers_done{false};
    
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
    
    // M개의 Consumer
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

// 테스트 9: 균형 잡힌 부하 (Producer == Consumer 수)
TEST(MPMCQueue, BalancedLoad) {
    MPMCQueue<int, 256> queue;
    constexpr int NUM_PAIRS = 8;
    constexpr int ITEMS_PER_PAIR = 5000;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;
    
    // 각 쌍의 producer/consumer
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
```

### 6.5 데이터 무결성 테스트

```cpp
// 테스트 10: 모든 데이터가 정확히 한 번만 소비됨
TEST(MPMCQueue, DataIntegrity) {
    MPMCQueue<int, 512> queue;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 5000;
    
    std::vector<std::atomic<int>> received(NUM_PRODUCERS * ITEMS_PER_PRODUCER);
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
            while (!done.load() || !queue.empty()) {
                int value;
                if (queue.pop(value)) {
                    received[value].fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    for (auto& p : producers) p.join();
    done.store(true);
    for (auto& c : consumers) c.join();
    
    // 모든 값이 정확히 1번 수신되었는지 확인
    for (int i = 0; i < NUM_PRODUCERS * ITEMS_PER_PRODUCER; ++i) {
        EXPECT_EQ(received[i].load(), 1) << "Value " << i << " received " 
                                          << received[i].load() << " times";
    }
}
```

---

## 7. 구현 체크리스트

- [ ] 템플릿 파라미터: `typename T, size_t Capacity`
- [ ] Capacity는 2의 거듭제곱 검증 (`static_assert`)
- [ ] Slot 구조체: `data` + `sequence`
- [ ] `push()`: CAS 루프로 head 경쟁 (MPSC와 동일)
- [ ] `pop()`: **CAS 루프로 tail 경쟁** (핵심 변경!)
- [ ] `empty()`, `full()`, `size()` 유틸리티
- [ ] False sharing 방지: `alignas(64)`
- [ ] 올바른 memory_order 사용
- [ ] Non-copyable, Non-movable

---

## 8. MPSC 코드 재사용

MPMC는 MPSC에서 `pop()` 함수만 수정하면 됩니다:

```cpp
// MPSC pop(): Consumer 1개 → CAS 불필요
bool pop(T& value) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    Slot& slot = buffer_[tail & (Capacity - 1)];
    // ...
    tail_.store(tail + 1, std::memory_order_release);
}

// MPMC pop(): Consumer 여러 개 → CAS 필요!
bool pop(T& value) {
    size_t pos = tail_.load(std::memory_order_relaxed);
    for (;;) {
        // ...
        if (tail_.compare_exchange_weak(pos, pos + 1, ...)) {
            // 성공
        }
        // ...
    }
}
```

---

## 9. 흔한 실수들

### 실수 1: Pop에서 CAS 빠뜨림
```cpp
// 틀림: MPSC 코드 그대로 사용
bool pop(T& value) {
    size_t tail = tail_.load(...);
    // ...
    tail_.store(tail + 1, ...);  // 여러 Consumer가 같은 값을 읽음!
}

// 맞음: CAS 사용
if (tail_.compare_exchange_weak(pos, pos + 1, ...)) {
    // 이 Consumer만 이 슬롯 획득
}
```

### 실수 2: 시퀀스 확인 안 함
```cpp
// 틀림: 시퀀스 확인 없이 바로 접근
bool pop(T& value) {
    size_t pos = tail_.load(...);
    if (tail_.compare_exchange_weak(pos, pos + 1, ...)) {
        value = buffer_[pos].data;  // 아직 쓰기 중일 수 있음!
    }
}

// 맞음: 시퀀스로 데이터 준비 여부 확인
size_t seq = slot.sequence.load(std::memory_order_acquire);
if (seq == pos + 1) {  // 데이터가 준비됨
    // 이제 안전하게 읽기
}
```

### 실수 3: 루프에서 pos 갱신 누락
```cpp
// 틀림: CAS 실패 시 pos가 그대로
for (;;) {
    if (diff == 0) {
        if (!tail_.compare_exchange_weak(pos, pos + 1, ...)) {
            continue;  // pos가 오래된 값!
        }
    }
}

// 맞음: compare_exchange_weak는 자동으로 pos 업데이트
// 또는 명시적으로:
pos = tail_.load(std::memory_order_relaxed);
```

---

## 10. 성능 고려사항

### 10.1 경쟁 상황에서의 성능

| 시나리오 | 예상 성능 |
|----------|-----------|
| 낮은 경쟁 (Producer/Consumer 적음) | 우수 |
| 높은 경쟁 (많은 스레드) | CAS 실패로 인한 재시도 증가 |
| Producer와 Consumer 수 불균형 | 병목 발생 가능 |

### 10.2 최적화 힌트

1. **배치 처리**: 한 번에 여러 항목 push/pop
2. **백오프 전략**: CAS 실패 시 지수 백오프
3. **캐시 라인 크기**: 64바이트 정렬 유지
4. **용량 선택**: 예상 최대 대기 항목 수의 2배

---

## 11. 다음 단계

1. **테스트 먼저 작성**: `tests/test_mpmc_queue.cpp`
2. **MPSC 코드 복사 후 수정**: `pop()` 함수에 CAS 추가
3. **테스트 통과**: 기본 + 멀티스레드 테스트
4. **벤치마크**: SPSC/MPSC/MPMC 성능 비교

**힌트가 필요하면 물어보세요!**
(정답 코드는 제공하지 않습니다 - 직접 구현해야 학습 효과가 있습니다)
```
