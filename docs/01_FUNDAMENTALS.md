# Phase 1: 동시성 프로그래밍 기초 개념

이 문서는 Lock-Free 자료구조를 이해하기 위한 필수 기초 개념을 다룹니다.

---

## 1. 멀티스레딩 기본

### 1.1 프로세스 vs 스레드

```
┌─────────────────────────────────────────────────────────────────┐
│                         운영체제                                 │
│  ┌─────────────────────┐        ┌─────────────────────┐         │
│  │     프로세스 A       │        │     프로세스 B       │         │
│  │  ┌───────────────┐  │        │  ┌───────────────┐  │         │
│  │  │   독립된 메모리  │  │        │  │   독립된 메모리  │  │         │
│  │  └───────────────┘  │        │  └───────────────┘  │         │
│  │  ┌─────┐ ┌─────┐   │        │  ┌─────┐           │         │
│  │  │ T1  │ │ T2  │   │        │  │ T1  │           │         │
│  │  └─────┘ └─────┘   │        │  └─────┘           │         │
│  └─────────────────────┘        └─────────────────────┘         │
└─────────────────────────────────────────────────────────────────┘
```

| 구분 | 프로세스 | 스레드 |
|------|---------|--------|
| 메모리 | 독립적 | 같은 프로세스 내 공유 |
| 생성 비용 | 높음 | 낮음 |
| 통신 | IPC 필요 | 공유 메모리로 직접 |
| 충돌 | 다른 프로세스에 영향 없음 | 전체 프로세스 영향 |

### 1.2 C++20 스레드 기본

```cpp
#include <thread>
#include <iostream>

void worker(int id) {
    std::cout << "Worker " << id << " started\n";
    // 작업 수행
}

int main() {
    std::jthread t1(worker, 1);  // C++20: jthread는 자동 join
    std::jthread t2(worker, 2);
    // 자동으로 join됨 (RAII)
}
```

**핵심 포인트:**
- `std::jthread` (C++20): 소멸자에서 자동 join, 취소 지원
- `std::thread` (C++11): 명시적 join/detach 필요

---

## 2. 경쟁 조건 (Race Condition)

### 2.1 문제 상황

```cpp
int counter = 0;

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter++;  // 이 연산은 원자적이지 않음!
    }
}

int main() {
    std::jthread t1(increment);
    std::jthread t2(increment);
    // t1, t2 종료 후...
    std::cout << counter;  // 200000이 아닐 수 있음!
}
```

### 2.2 왜 `counter++`가 문제인가?

`counter++`는 실제로 3개의 연산:
```
1. LOAD:  레지스터 ← 메모리[counter]
2. ADD:   레지스터 ← 레지스터 + 1
3. STORE: 메모리[counter] ← 레지스터
```

두 스레드가 동시에 실행되면:
```
        Thread 1              Thread 2            counter 값
        ────────              ────────            ──────────
초기                                              0
        LOAD (0)                                  0
                              LOAD (0)            0
        ADD (1)                                   0
                              ADD (1)             0
        STORE (1)                                 1
                              STORE (1)           1  ← 둘 다 1을 씀!
```

결과: 2가 되어야 하는데 1이 됨 → **데이터 손실**

---

## 3. Atomic 연산

### 3.1 std::atomic 기본

```cpp
#include <atomic>

std::atomic<int> counter{0};

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter++;  // 원자적 연산!
        // 또는: counter.fetch_add(1);
    }
}
```

### 3.2 주요 atomic 연산

| 연산 | 설명 | 예시 |
|------|------|------|
| `load()` | 원자적 읽기 | `int x = counter.load();` |
| `store(val)` | 원자적 쓰기 | `counter.store(42);` |
| `exchange(val)` | 원자적 교환 | `int old = counter.exchange(10);` |
| `fetch_add(val)` | 원자적 덧셈 | `int old = counter.fetch_add(1);` |
| `compare_exchange_*` | CAS | 아래 설명 |

### 3.3 CAS (Compare-And-Swap)

Lock-free 프로그래밍의 **핵심 연산**:

```cpp
bool compare_exchange_strong(T& expected, T desired);
```

동작:
```
if (현재값 == expected) {
    현재값 = desired;
    return true;
} else {
    expected = 현재값;  // expected를 현재값으로 업데이트
    return false;
}
```

**사용 예시:**
```cpp
std::atomic<int> value{5};

int expected = 5;
bool success = value.compare_exchange_strong(expected, 10);
// value가 5였으면 → 10으로 변경, success = true
// value가 5가 아니었으면 → 변경 없음, expected에 현재값 저장, success = false
```

**weak vs strong:**
- `compare_exchange_weak`: spurious failure 가능 (루프에서 사용)
- `compare_exchange_strong`: 확실한 실패만 (단일 시도에 적합)

---

## 4. Memory Order (메모리 순서)

### 4.1 왜 필요한가?

**컴파일러와 CPU의 재배치:**
```cpp
int a = 0;
bool ready = false;

// Thread 1
a = 42;
ready = true;

// Thread 2
if (ready) {
    assert(a == 42);  // 실패할 수 있음!
}
```

왜 실패? 컴파일러나 CPU가 명령어 순서를 바꿀 수 있음:
```cpp
// Thread 1 (재배치 후)
ready = true;  // 먼저 실행될 수 있음!
a = 42;
```

### 4.2 Memory Order 종류

#### memory_order_relaxed
- **보장**: 원자성만 (연산이 분할되지 않음)
- **보장 안 함**: 순서
- **사용처**: 카운터, 통계 (다른 데이터와 동기화 불필요)

```cpp
std::atomic<int> counter{0};
counter.fetch_add(1, std::memory_order_relaxed);  // 가장 빠름
```

#### memory_order_acquire
- **보장**: 이 연산 **이후**의 모든 읽기/쓰기가 이 연산 **앞으로** 이동 불가
- **사용처**: 데이터 읽기 전 동기화 (자물쇠 획득)

```cpp
if (ready.load(std::memory_order_acquire)) {
    // 이 시점에서 ready가 true로 설정되기 전의 모든 쓰기가 보임
    use(data);
}
```

#### memory_order_release
- **보장**: 이 연산 **이전**의 모든 읽기/쓰기가 이 연산 **뒤로** 이동 불가
- **사용처**: 데이터 쓰기 후 동기화 (자물쇠 해제)

```cpp
data = 42;
ready.store(true, std::memory_order_release);
// 이 시점에서 data = 42가 확실히 완료됨
```

#### memory_order_acq_rel
- **보장**: acquire + release 모두
- **사용처**: CAS 연산 등

#### memory_order_seq_cst (기본값)
- **보장**: 전역적으로 일관된 순서 (모든 스레드가 같은 순서로 봄)
- **사용처**: 디버깅, 간단한 코드, 성능보다 정확성이 중요할 때
- **비용**: 가장 느림

### 4.3 Acquire-Release 동기화 패턴

```
Thread 1 (Producer)              Thread 2 (Consumer)
───────────────────              ───────────────────
data = 42;                       
otherData = 100;                 
flag.store(true, release);  ───► while(!flag.load(acquire));
     │                                     │
     │ 동기화 지점                          │
     │                                     │
     └─────────────────────────────────────┘
                                 // 이 시점에서:
                                 // data == 42 보장!
                                 // otherData == 100 보장!
```

---

## 5. False Sharing

### 5.1 캐시 구조

```
CPU 0                           CPU 1
┌─────────────────┐             ┌─────────────────┐
│  Core           │             │  Core           │
│  ┌───────────┐  │             │  ┌───────────┐  │
│  │ L1 Cache  │  │             │  │ L1 Cache  │  │
│  └─────┬─────┘  │             │  └─────┬─────┘  │
│        │        │             │        │        │
│  ┌─────┴─────┐  │             │  ┌─────┴─────┐  │
│  │ L2 Cache  │  │             │  │ L2 Cache  │  │
│  └─────┬─────┘  │             │  └─────┬─────┘  │
└────────┼────────┘             └────────┼────────┘
         │                               │
         └───────────────┬───────────────┘
                         │
                   ┌─────┴─────┐
                   │ L3 Cache  │  (공유)
                   └─────┬─────┘
                         │
                   ┌─────┴─────┐
                   │   메모리   │
                   └───────────┘
```

**캐시 라인**: 캐시의 최소 단위 (보통 64 bytes)

### 5.2 False Sharing 문제

```cpp
struct Counters {
    std::atomic<int> a;  // Thread 1이 사용
    std::atomic<int> b;  // Thread 2가 사용
};

Counters counters;
```

메모리 레이아웃:
```
┌────────────────────────────────────────────────────────────────┐
│                    캐시 라인 (64 bytes)                         │
│  ┌──────────────────────┬──────────────────────┬─────────────┐ │
│  │   counters.a (4B)    │   counters.b (4B)    │   패딩      │ │
│  │    (Thread 1 수정)    │    (Thread 2 수정)   │             │ │
│  └──────────────────────┴──────────────────────┴─────────────┘ │
└────────────────────────────────────────────────────────────────┘
```

**문제:**
1. Thread 1이 `a`를 수정 → 캐시 라인 전체가 "dirty"
2. Thread 2가 `b`를 읽으려면 → 캐시 미스! (CPU 0에서 가져와야 함)
3. Thread 2가 `b`를 수정 → 캐시 라인 전체가 "dirty"
4. Thread 1이 `a`를 읽으려면 → 캐시 미스!

→ **핑퐁 현상**: 두 CPU가 캐시 라인을 계속 주고받음

### 5.3 해결책: 패딩

```cpp
struct alignas(64) PaddedCounter {
    std::atomic<int> value;
    // 64바이트 정렬로 각 인스턴스가 별도 캐시 라인에 위치
};

// 또는 C++17 std::hardware_destructive_interference_size 사용
struct alignas(std::hardware_destructive_interference_size) PaddedCounter {
    std::atomic<int> value;
};
```

---

## 6. Lock-Free vs Wait-Free

### 6.1 정의

| 용어 | 정의 | 보장 |
|------|------|------|
| **Blocking** | 락 사용, 스레드 대기 가능 | 없음 |
| **Lock-Free** | 최소 하나의 스레드가 진행 보장 | 시스템 전체 진행 |
| **Wait-Free** | 모든 스레드가 유한 단계 내 완료 | 개별 스레드 진행 |

### 6.2 Lock-Free 특성

```
┌─────────────────────────────────────────────┐
│              Lock-Free 보장                  │
│                                             │
│  Thread A: ───────────────────────────►     │
│  Thread B: ─────────────►                   │ ← 일부 실패 가능
│  Thread C: ─────────────────────►           │
│                                             │
│  → 항상 최소 하나는 성공!                     │
└─────────────────────────────────────────────┘
```

**CAS 루프 패턴:**
```cpp
do {
    old_value = current.load();
    new_value = compute_new_value(old_value);
} while (!current.compare_exchange_weak(old_value, new_value));
// 무한 루프처럼 보이지만, 다른 스레드가 성공했다는 의미
// → 시스템 전체로는 진행 중
```

---

## 7. 실습 과제

### 과제 1: Race Condition 재현
일반 int로 카운터를 만들고 경쟁 조건을 직접 확인하세요.

### 과제 2: Atomic으로 해결
std::atomic으로 같은 코드를 수정하고 결과를 비교하세요.

### 과제 3: Memory Order 실험
relaxed vs seq_cst 성능 차이를 측정해보세요.

### 과제 4: False Sharing 측정
패딩 있/없는 경우의 성능 차이를 측정해보세요.

---

## 다음 단계

기초 개념을 충분히 이해했다면, [Phase 2: SPSC Queue](02_SPSC_QUEUE.md)로 진행하세요!

**준비 체크:**
- [ ] atomic의 load/store/CAS를 이해했는가?
- [ ] acquire/release 동기화가 왜 필요한지 이해했는가?
- [ ] false sharing이 무엇인지 설명할 수 있는가?
