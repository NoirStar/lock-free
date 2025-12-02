# Phase 2: SPSC Queue (Single Producer Single Consumer)

이 문서는 가장 단순한 Lock-Free Queue인 SPSC Queue의 설계와 구현을 안내합니다.

---

## 1. SPSC Queue란?

### 1.1 정의

**SPSC (Single Producer Single Consumer) Queue**:
- 정확히 **1개의 스레드**만 데이터를 추가 (Producer)
- 정확히 **1개의 스레드**만 데이터를 제거 (Consumer)
- 이 제약 덕분에 가장 단순하고 효율적인 lock-free queue 가능

### 1.2 실제 사용 사례

```
게임 엔진 예시:

┌─────────────┐           ┌─────────────┐
│  Game Logic │           │   Render    │
│   Thread    │ ────────► │   Thread    │
│  (Producer) │   SPSC    │  (Consumer) │
│             │   Queue   │             │
└─────────────┘           └─────────────┘

- 게임 로직: 렌더 명령 생성 (위치, 회전, 텍스처 등)
- 렌더 스레드: 명령 소비 및 GPU 렌더링
```

다른 사용 사례:
- 오디오 버퍼링 (재생 스레드 ↔ 디코딩 스레드)
- 로그 수집 (애플리케이션 → 로그 작성 스레드)
- 네트워크 패킷 처리 (수신 스레드 → 처리 스레드)

---

## 2. Ring Buffer 구조

### 2.1 기본 개념

SPSC Queue는 보통 **Ring Buffer (Circular Buffer)**로 구현:

```
용량: 8

초기 상태:
┌───┬───┬───┬───┬───┬───┬───┬───┐
│   │   │   │   │   │   │   │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
  ↑
 head = tail = 0  (빈 상태)


push(A), push(B), push(C) 후:
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ A │ B │ C │   │   │   │   │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
  ↑           ↑
 tail=0      head=3


pop() → A, pop() → B 후:
┌───┬───┬───┬───┬───┬───┬───┬───┐
│   │   │ C │   │   │   │   │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
          ↑   ↑
        tail=2 head=3


더 많이 추가하면 (wrap around):
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ H │   │ C │ D │ E │ F │ G │   │
└───┴───┴───┴───┴───┴───┴───┴───┘
  ↑       ↑
head=1  tail=2
```

### 2.2 인덱스 계산

```
실제 배열 인덱스 = 논리적 인덱스 % 용량
```

**질문:** 왜 용량을 2의 거듭제곱으로 하면 좋을까요?

<details>
<summary>힌트</summary>

`% n`은 느리지만, n이 2의 거듭제곱이면 `& (n-1)`로 대체 가능!
```cpp
// 느림
index % 8

// 빠름 (8 = 2^3, 8-1 = 7 = 0b111)
index & 7
```
</details>

---

## 3. 설계

### 3.1 필요한 멤버 변수

```cpp
template<typename T, size_t Capacity>
class SPSCQueue {
private:
    // TODO: 어떤 멤버 변수가 필요한가?
    // 힌트: 버퍼, head, tail
};
```

**스스로 생각해볼 질문들:**

1. 버퍼는 어떤 타입이어야 하는가? (힌트: 원소가 아직 없는 슬롯도 있음)
2. head와 tail은 atomic이어야 하는가? 왜?
3. head와 tail의 초기값은?

### 3.2 공개 인터페이스

```cpp
template<typename T, size_t Capacity>
class SPSCQueue {
public:
    // 생성자
    SPSCQueue();
    
    // Producer가 호출
    bool push(const T& value);
    bool push(T&& value);  // 이동 시맨틱
    
    // Consumer가 호출
    bool pop(T& value);
    
    // 유틸리티
    bool empty() const;
    bool full() const;
    size_t size() const;  // 선택적
};
```

### 3.3 핵심 알고리즘 흐름

#### push() 흐름

```
1. 현재 head 위치 확인
2. 큐가 가득 찼는지 확인 (어떻게?)
3. 가득 찼으면 → false 반환
4. 버퍼[head]에 값 저장
5. head 증가 (어떤 memory_order?)
6. true 반환
```

#### pop() 흐름

```
1. 현재 tail 위치 확인
2. 큐가 비었는지 확인 (어떻게?)
3. 비었으면 → false 반환
4. 버퍼[tail]에서 값 읽기
5. tail 증가 (어떤 memory_order?)
6. true 반환
```

### 3.4 핵심 질문들

**Q1: 가득 찬 상태 vs 빈 상태 구분**

head == tail일 때:
- 큐가 비어있는가?
- 아니면 가득 찬 것인가?

```
빈 상태:
┌───┬───┬───┬───┐
│   │   │   │   │
└───┴───┴───┴───┘
  ↑
head=tail=0


가득 찬 상태?
┌───┬───┬───┬───┐
│ A │ B │ C │ D │
└───┴───┴───┴───┘
  ↑
head=tail=4 (또는 0)?
```

<details>
<summary>힌트: 두 가지 해결책</summary>

1. **항상 하나의 슬롯을 비워둠**: 실제 용량 = Capacity - 1
   - 가득 참: `(head + 1) % Capacity == tail`
   - 빔: `head == tail`

2. **별도의 카운터 유지**: size 변수 사용
   - 추가적인 동기화 필요

첫 번째 방법이 더 간단하고 효율적!
</details>

**Q2: Memory Order 선택**

```cpp
// Producer
buffer[head] = value;
head.store(new_head, ???);  // 어떤 memory_order?

// Consumer
T value = buffer[tail];
tail.store(new_tail, ???);  // 어떤 memory_order?
```

<details>
<summary>힌트</summary>

- Producer: 버퍼 쓰기가 head 업데이트 전에 완료되어야 함 → **release**
- Consumer: tail 읽기가 버퍼 읽기 전에 완료되어야 함 → **acquire**

하지만 SPSC에서는 더 relaxed해도 되는 경우가 있음... 왜?
</details>

**Q3: False Sharing 방지**

```cpp
std::atomic<size_t> head;  // Producer가 수정
std::atomic<size_t> tail;  // Consumer가 수정
// 이 둘이 같은 캐시 라인에 있으면?
```

---

## 4. TDD 테스트 시나리오

구현 전에 다음 테스트들을 먼저 작성하세요.

### 4.1 기본 기능 테스트

```cpp
#include <gtest/gtest.h>
#include "lockfree/spsc_queue.hpp"

// 테스트 1: 생성 및 초기 상태
TEST(SPSCQueue, InitialState) {
    // 새로 생성된 큐는 비어있어야 함
    // empty()가 true를 반환해야 함
    // full()이 false를 반환해야 함
}

// 테스트 2: 단일 push/pop
TEST(SPSCQueue, SinglePushPop) {
    // 하나의 값을 push
    // pop하면 같은 값이 나와야 함
    // pop 후 큐는 다시 비어있어야 함
}

// 테스트 3: 빈 큐에서 pop 시도
TEST(SPSCQueue, PopFromEmpty) {
    // 빈 큐에서 pop하면 false 반환해야 함
    // 전달된 값은 변경되지 않아야 함
}

// 테스트 4: 가득 찬 큐에 push 시도
TEST(SPSCQueue, PushToFull) {
    // 용량만큼 push한 후
    // 추가 push는 false 반환해야 함
}

// 테스트 5: FIFO 순서 확인
TEST(SPSCQueue, FIFOOrder) {
    // 1, 2, 3, 4, 5를 순서대로 push
    // pop하면 1, 2, 3, 4, 5 순서로 나와야 함
}

// 테스트 6: Wrap-around 동작
TEST(SPSCQueue, WrapAround) {
    // 용량의 절반 push
    // 전부 pop
    // 다시 용량만큼 push
    // wrap-around가 제대로 동작하는지 확인
}
```

### 4.2 멀티스레드 테스트

```cpp
// 테스트 7: 동시성 기본 테스트
TEST(SPSCQueue, ConcurrentBasic) {
    // Producer 스레드: N개의 값을 push
    // Consumer 스레드: N개의 값을 pop
    // 모든 값이 손실 없이 전달되어야 함
}

// 테스트 8: 고부하 동시성 테스트
TEST(SPSCQueue, ConcurrentStress) {
    // 많은 수의 값(예: 1,000,000개)을 전송
    // 모든 값이 올바른 순서로 수신되어야 함
    // 중복이나 손실이 없어야 함
}

// 테스트 9: Producer가 빠른 경우
TEST(SPSCQueue, FastProducer) {
    // Producer가 빠르게 push, Consumer가 느리게 pop
    // 큐가 가득 차면 Producer가 대기해야 함
    // 데이터 손실 없어야 함
}

// 테스트 10: Consumer가 빠른 경우
TEST(SPSCQueue, FastConsumer) {
    // Consumer가 빠르게 pop 시도, Producer가 느리게 push
    // 큐가 비면 Consumer가 대기해야 함
    // 데이터 손실 없어야 함
}
```

### 4.3 엣지 케이스 테스트

```cpp
// 테스트 11: 용량이 1인 큐
TEST(SPSCQueue, CapacityOne) {
    // 용량이 1인 큐도 제대로 동작해야 함
}

// 테스트 12: 다양한 타입
TEST(SPSCQueue, DifferentTypes) {
    // int, double, std::string, 사용자 정의 타입
    // 모두 제대로 동작해야 함
}

// 테스트 13: 이동 전용 타입
TEST(SPSCQueue, MoveOnlyType) {
    // std::unique_ptr 같은 이동 전용 타입도 지원해야 함
}
```

---

## 5. 구현 체크리스트

구현할 때 다음을 확인하세요:

- [ ] 템플릿 매개변수: `typename T, size_t Capacity`
- [ ] 버퍼 저장소: `std::array` 또는 고정 배열
- [ ] atomic head와 tail
- [ ] head/tail 사이 false sharing 방지 (alignas)
- [ ] push에서 full 체크
- [ ] pop에서 empty 체크
- [ ] 올바른 memory_order 사용
- [ ] 이동 시맨틱 지원

---

## 6. 흔한 실수들

### 실수 1: 잘못된 full/empty 판단
```cpp
// 틀림
bool empty() { return head == tail; }
bool full() { return head == tail; }  // 같은 조건!
```

### 실수 2: Memory Order 과다 사용
```cpp
// 비효율적 (불필요하게 강한 순서)
head.store(new_head, std::memory_order_seq_cst);
tail.load(std::memory_order_seq_cst);
```

### 실수 3: 버퍼 접근과 인덱스 업데이트 순서
```cpp
// 틀림: 인덱스 먼저 업데이트하면 다른 스레드가 쓰레기 값을 읽을 수 있음
head.store(new_head);
buffer[old_head] = value;  // 순서 틀림!
```

### 실수 4: Wrap-around 처리
```cpp
// 틀림: 오버플로우 고려 안 함
size_t new_head = head + 1;  // 매우 큰 값이 될 수 있음
buffer[new_head] = value;    // 범위 초과!

// 맞음
size_t new_head = (head + 1) % Capacity;
```

---

## 7. 다음 단계

1. **테스트 먼저 작성**: 위의 테스트 시나리오를 `tests/test_spsc_queue.cpp`에 구현
2. **구현 시작**: `include/lockfree/spsc_queue.hpp`에 클래스 구현
3. **테스트 통과**: 모든 테스트가 통과할 때까지 반복
4. **코드 리뷰 요청**: 구현 완료 후 리뷰 받기

**질문이 있으면 언제든 물어보세요!**
(단, 정답 코드는 제공하지 않습니다 - 스스로 구현해야 학습 효과가 있습니다)
