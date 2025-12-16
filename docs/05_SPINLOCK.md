# Phase 5: SpinLock

이 문서는 Lock-Free 프로그래밍의 기본 동기화 프리미티브인 SpinLock의 설계와 구현을 안내합니다.

---

## 1. SpinLock이란?

### 1.1 정의

**SpinLock**:
- 락을 획득할 때까지 **바쁜 대기(Busy Waiting)**를 수행하는 동기화 프리미티브
- 스레드가 락을 기다리는 동안 **CPU를 양보하지 않고 계속 시도**
- 매우 짧은 임계 영역(Critical Section)에 적합

### 1.2 Mutex vs SpinLock

```
Mutex (뮤텍스):
┌─────────────┐     락 획득 실패     ┌─────────────┐
│   Thread    │ ──────────────────► │   OS Sleep  │
│  (Running)  │                     │  (Blocked)  │
└─────────────┘                     └─────────────┘
                                           │
                                           │ 락 해제시 깨움
                                           ▼
                                    ┌─────────────┐
                                    │   Thread    │
                                    │  (Running)  │
                                    └─────────────┘

SpinLock (스핀락):
┌─────────────┐     락 획득 실패     ┌─────────────┐
│   Thread    │ ──────────────────► │    Spin     │
│  (Running)  │                     │  (Running)  │
└─────────────┘                     └─────────────┘
                                           │
                                           │ 계속 시도 (busy wait)
                                           ▼
                                    ┌─────────────┐
                                    │   Thread    │
                                    │  (Running)  │
                                    └─────────────┘
```

| 특성 | Mutex | SpinLock |
|------|-------|----------|
| 대기 방식 | Sleep (OS 스케줄러) | Busy Wait (CPU 사용) |
| 컨텍스트 스위치 | 발생함 | 발생하지 않음 |
| 짧은 임계 영역 | 오버헤드 큼 | 효율적 |
| 긴 임계 영역 | 효율적 | CPU 낭비 |
| 단일 코어 | 적합 | 비효율적 |

### 1.3 사용 사례

```
적합한 경우:
- 임계 영역이 매우 짧음 (수십~수백 나노초)
- 멀티코어 환경
- 컨텍스트 스위치 비용이 락 대기 시간보다 큼

부적합한 경우:
- 임계 영역이 김 (I/O 작업 등)
- 단일 코어 환경
- 우선순위 역전 문제가 중요한 경우
```

---

## 2. 기본 구현 원리

### 2.1 Test-And-Set (TAS)

가장 기본적인 SpinLock 구현:

```cpp
class TASSpinLock {
    std::atomic<bool> locked{false};
    
public:
    void lock() {
        // exchange: 값을 설정하고 이전 값을 반환
        while (locked.exchange(true, std::memory_order_acquire)) {
            // 이미 locked=true였으면 계속 시도
        }
    }
    
    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};
```

**동작 원리**:
```
Thread A: exchange(true) → 이전값 false → 락 획득!
Thread B: exchange(true) → 이전값 true  → 계속 시도
Thread B: exchange(true) → 이전값 true  → 계속 시도
Thread A: store(false)   → 락 해제
Thread B: exchange(true) → 이전값 false → 락 획득!
```

### 2.2 Test-And-Test-And-Set (TTAS)

TAS의 개선 버전 - 캐시 효율성 향상:

```cpp
class TTASSpinLock {
    std::atomic<bool> locked{false};
    
public:
    void lock() {
        while (true) {
            // 1단계: 먼저 읽기만 함 (캐시에서 읽음)
            while (locked.load(std::memory_order_relaxed)) {
                // 스핀 대기
            }
            
            // 2단계: 실제 교환 시도
            if (!locked.exchange(true, std::memory_order_acquire)) {
                return; // 락 획득 성공
            }
        }
    }
    
    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};
```

**TAS vs TTAS**:
```
TAS 문제점:
- exchange는 항상 캐시 라인을 Exclusive 상태로 가져옴
- 여러 스레드가 동시에 exchange하면 캐시 라인 핑퐁 발생

TTAS 개선:
- load는 캐시 라인을 Shared 상태로 유지
- exchange는 락이 해제될 때만 수행
- 캐시 일관성 트래픽 대폭 감소
```

---

## 3. 최적화 기법

### 3.1 Exponential Backoff

경쟁이 심할 때 점진적으로 대기 시간 증가:

```cpp
void lock() {
    int backoff = 1;
    const int MAX_BACKOFF = 1024;
    
    while (true) {
        while (locked.load(std::memory_order_relaxed)) {
            for (int i = 0; i < backoff; ++i) {
                // CPU pause 명령어
                _mm_pause(); // x86
            }
            backoff = std::min(backoff * 2, MAX_BACKOFF);
        }
        
        if (!locked.exchange(true, std::memory_order_acquire)) {
            return;
        }
    }
}
```

### 3.2 CPU Pause 명령어

스핀 루프에서 전력 소비와 파이프라인 효율 개선:

```cpp
#if defined(_MSC_VER)
    #include <intrin.h>
    #define cpu_pause() _mm_pause()
#elif defined(__x86_64__) || defined(__i386__)
    #define cpu_pause() __builtin_ia32_pause()
#elif defined(__aarch64__)
    #define cpu_pause() __asm__ __volatile__("yield")
#else
    #define cpu_pause() ((void)0)
#endif
```

### 3.3 Ticket Lock (공정성 보장)

FIFO 순서로 락 획득을 보장:

```cpp
class TicketLock {
    std::atomic<size_t> next_ticket{0};
    std::atomic<size_t> now_serving{0};
    
public:
    void lock() {
        size_t my_ticket = next_ticket.fetch_add(1, std::memory_order_relaxed);
        while (now_serving.load(std::memory_order_acquire) != my_ticket) {
            cpu_pause();
        }
    }
    
    void unlock() {
        now_serving.fetch_add(1, std::memory_order_release);
    }
};
```

---

## 4. Memory Order 이해

### 4.1 lock()에서 acquire가 필요한 이유

```cpp
void lock() {
    while (locked.exchange(true, std::memory_order_acquire)) {}
    //                           ^^^^^^^^^^^^^^^^^^^^^^
    // 락 획득 후의 모든 읽기/쓰기가 이 지점 이전으로 재배치되지 않음
}

// 임계 영역의 코드가 락 획득 이전으로 이동하면 안됨!
```

### 4.2 unlock()에서 release가 필요한 이유

```cpp
void unlock() {
    locked.store(false, std::memory_order_release);
    //                  ^^^^^^^^^^^^^^^^^^^^^^
    // 임계 영역의 모든 읽기/쓰기가 이 지점 이후로 재배치되지 않음
}

// 임계 영역의 코드가 락 해제 이후로 이동하면 안됨!
```

### 4.3 Acquire-Release 동기화

```
Thread A (unlock)                Thread B (lock)
─────────────────                ──────────────────
[임계 영역 작업]
      │
      ▼
store(false, release) ─────────► exchange(true, acquire)
                                       │
                                       ▼
                                 [임계 영역 작업]
                                 (Thread A의 쓰기가 보임)
```

---

## 5. API 설명

### 5.1 SpinLock 클래스

```cpp
namespace lockfree {

class SpinLock {
public:
    SpinLock();
    
    // 락 획득 (블로킹)
    void lock();
    
    // 락 획득 시도 (논블로킹)
    // 성공시 true, 실패시 false
    bool try_lock();
    
    // 락 해제
    void unlock();
};

}
```

### 5.2 SpinLockGuard (RAII)

```cpp
namespace lockfree {

class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock);
    ~SpinLockGuard();
    
    // 복사/이동 불가
};

}
```

**사용 예시**:
```cpp
lockfree::SpinLock lock;

void critical_section() {
    lockfree::SpinLockGuard guard(lock);
    // 임계 영역 코드
    // guard 소멸시 자동으로 unlock
}
```

---

## 6. 주의사항

### 6.1 재귀 호출 금지

기본 SpinLock은 재귀적으로 lock을 호출하면 교착 상태(Deadlock):

```cpp
SpinLock lock;

void foo() {
    SpinLockGuard guard(lock);
    bar(); // ❌ 교착 상태!
}

void bar() {
    SpinLockGuard guard(lock); // 같은 스레드가 다시 lock 시도
}
```

### 6.2 unlock 보장

예외 발생시에도 unlock이 호출되도록 RAII 사용 권장:

```cpp
// 나쁜 예
lock.lock();
do_something(); // 예외 발생시 unlock 안됨!
lock.unlock();

// 좋은 예
{
    SpinLockGuard guard(lock);
    do_something(); // 예외 발생해도 unlock 보장
}
```

### 6.3 임계 영역 최소화

```cpp
// 나쁜 예
{
    SpinLockGuard guard(lock);
    auto data = load_from_disk(); // I/O 작업 - 매우 느림!
    process(data);
}

// 좋은 예
auto data = load_from_disk(); // 락 없이 I/O
{
    SpinLockGuard guard(lock);
    process(data); // 짧은 임계 영역만
}
```

---

## 7. 다음 단계

- [ ] 기본 TAS SpinLock 구현
- [ ] TTAS 최적화 적용
- [ ] Backoff 전략 추가
- [ ] 벤치마크 작성 및 성능 측정
- [ ] Ticket Lock 변형 구현 (선택)

---

## 참고 자료

- [The Art of Multiprocessor Programming - SpinLock](https://www.amazon.com/Art-Multiprocessor-Programming-Revised-Reprint/dp/0123973376)
- [Intel® 64 and IA-32 Architectures Optimization Reference Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [C++ Memory Model](https://en.cppreference.com/w/cpp/atomic/memory_order)
