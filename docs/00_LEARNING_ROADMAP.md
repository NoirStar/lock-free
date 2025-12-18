# 학습 로드맵: Lock-Free 동시성 자료구조

이 문서는 C++20 기반 Lock-Free 동시성 자료구조를 **0부터 학습**하기 위한 단계별 가이드입니다.

---

## 📚 전체 학습 순서

| Phase | 주제 | 목표 | 예상 시간 |
|-------|------|------|----------|
| 0 | 환경 구성 | CMake + GoogleTest 프로젝트 설정 | 1일 |
| 1 | 기초 개념 | 스레드, atomic, memory_order 이해 | 2-3일 |
| 2 | SPSC Queue | 가장 단순한 lock-free queue 구현 | 3-5일 |
| 3 | MPSC Queue | 다중 생산자 단일 소비자 구현 | 3-5일 |
| 4 | MPMC Queue | 완전한 다중 생산자/소비자 구현 | 5-7일 |
| 5 | Spinlock | 경량 동기화 프리미티브 | 1-2일 |
| 6 | ABA 문제 | ABA 문제 이해 및 Tagged Pointer 해결 | 2-3일 |
| 7 | 메모리 풀 | Lock-Free Object Pool 구현 | 3-5일 |
| 8 | **Job System** | 멀티코어 병렬 처리 프레임워크 | 5-7일 |

---

## Phase 1: 기초 개념 (필수 선행 지식)

### 1.1 스레드(Thread) 기초

**스레드란?**
- 프로세스 내에서 실행되는 독립적인 실행 흐름
- 같은 프로세스의 스레드들은 메모리 공간을 공유함
- C++11부터 `<thread>` 헤더로 표준 스레드 지원

**핵심 개념:**
```
┌─────────────────────────────────────────┐
│              프로세스                    │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
│  │ Thread1 │  │ Thread2 │  │ Thread3 │ │
│  │  Stack  │  │  Stack  │  │  Stack  │ │
│  └────┬────┘  └────┬────┘  └────┬────┘ │
│       │            │            │       │
│       └────────────┼────────────┘       │
│                    ▼                    │
│         ┌─────────────────────┐         │
│         │   공유 메모리 (Heap) │         │
│         └─────────────────────┘         │
└─────────────────────────────────────────┘
```

### 1.2 경쟁 조건(Race Condition)

**정의:** 두 개 이상의 스레드가 공유 데이터에 동시 접근하며, 그 중 최소 하나가 쓰기 작업을 할 때 발생

**예시 시나리오:**
```
초기값: counter = 0

Thread A                    Thread B
─────────                   ─────────
1. 읽기: temp = 0          
                            2. 읽기: temp = 0
3. 증가: temp = 1          
                            4. 증가: temp = 1
5. 쓰기: counter = 1       
                            6. 쓰기: counter = 1

결과: counter = 1 (예상: 2)
```

### 1.3 Atomic 연산

**정의:** 중간 상태 없이 한 번에 완료되는 연산 (분할 불가능)

**C++20 std::atomic:**
- `std::atomic<T>`: 타입 T에 대한 원자적 연산 제공
- `load()`: 원자적 읽기
- `store()`: 원자적 쓰기
- `exchange()`: 원자적 교환
- `compare_exchange_weak/strong()`: CAS (Compare-And-Swap)

### 1.4 Memory Order (메모리 순서)

**왜 필요한가?**
- CPU와 컴파일러는 성능을 위해 명령어를 재배치함
- 단일 스레드에서는 문제없지만, 멀티스레드에서는 예상치 못한 결과 발생

**주요 Memory Order:**

| Memory Order | 설명 | 사용 상황 |
|-------------|------|----------|
| `relaxed` | 원자성만 보장, 순서 보장 없음 | 카운터, 통계 |
| `acquire` | 이 연산 이후의 읽기/쓰기가 앞으로 이동 불가 | 데이터 읽기 전 동기화 |
| `release` | 이 연산 이전의 읽기/쓰기가 뒤로 이동 불가 | 데이터 쓰기 후 동기화 |
| `acq_rel` | acquire + release | CAS 연산 |
| `seq_cst` | 전역 순서 보장 (가장 강력, 가장 느림) | 기본값, 디버깅 |

**Acquire-Release 동기화 패턴:**
```
Thread A (Producer)           Thread B (Consumer)
─────────────────            ─────────────────
data = 42;                   
ready.store(true, release);  while(!ready.load(acquire));
                             assert(data == 42); // 보장됨!
```

### 1.5 False Sharing

**정의:** 서로 다른 변수가 같은 캐시 라인에 위치하여, 한 스레드의 쓰기가 다른 스레드의 캐시를 무효화하는 현상

**캐시 라인 구조:**
```
┌────────────────────────────────────────────────────────────────┐
│                    캐시 라인 (보통 64 bytes)                    │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐ │
│  │   변수 A     │   변수 B     │   변수 C     │   변수 D     │ │
│  │  (Thread 1)  │  (Thread 2)  │  (Thread 3)  │  (Thread 4)  │ │
│  └──────────────┴──────────────┴──────────────┴──────────────┘ │
└────────────────────────────────────────────────────────────────┘

Thread 1이 변수 A를 수정하면 → 전체 캐시 라인이 무효화됨
→ Thread 2,3,4도 캐시 미스 발생!
```

**해결책: 패딩**
```cpp
struct alignas(64) PaddedCounter {
    std::atomic<int> value;
    // 64바이트 정렬로 각 카운터가 별도 캐시 라인에 위치
};
```

---

## Phase 2: SPSC Queue (Single Producer Single Consumer)

### 2.1 개념

**SPSC Queue란?**
- 정확히 1개의 생산자(Producer) 스레드와 1개의 소비자(Consumer) 스레드만 사용
- 가장 단순하고 효율적인 lock-free queue
- 게임 엔진의 렌더 스레드 ↔ 로직 스레드 통신 등에 활용

**구조:**
```
Producer                                          Consumer
   │                                                  │
   ▼                                                  ▼
┌──────────────────────────────────────────────────────────┐
│  [ ][ ][X][X][X][X][ ][ ][ ][ ]                          │
│        ↑           ↑                                     │
│       head        tail                                   │
│     (쓰기 위치)   (읽기 위치)                              │
└──────────────────────────────────────────────────────────┘
      Ring Buffer (고정 크기)
```

### 2.2 설계 힌트

**필요한 멤버:**
- 고정 크기 배열 (ring buffer)
- `head`: 다음 쓰기 위치 (Producer가 수정)
- `tail`: 다음 읽기 위치 (Consumer가 수정)

**핵심 질문 (스스로 답해보세요):**
1. head와 tail은 왜 atomic이어야 하는가?
2. 큐가 가득 찼는지 어떻게 판단하는가?
3. 큐가 비었는지 어떻게 판단하는가?
4. head와 tail에 어떤 memory_order를 사용해야 하는가?

### 2.3 TDD 테스트 시나리오

구현 전에 다음 테스트들을 먼저 작성하세요:

```cpp
// 테스트 1: 기본 push/pop
TEST(SPSCQueue, BasicPushPop) {
    // 빈 큐에 하나 push하고 pop 했을 때 같은 값이 나오는가?
}

// 테스트 2: 빈 큐에서 pop
TEST(SPSCQueue, PopFromEmpty) {
    // 빈 큐에서 pop하면 실패해야 함
}

// 테스트 3: 가득 찬 큐에 push
TEST(SPSCQueue, PushToFull) {
    // 가득 찬 큐에 push하면 실패해야 함
}

// 테스트 4: FIFO 순서 보장
TEST(SPSCQueue, FIFOOrder) {
    // 1, 2, 3을 push하고 pop하면 1, 2, 3 순서로 나와야 함
}

// 테스트 5: 멀티스레드 동작
TEST(SPSCQueue, ConcurrentPushPop) {
    // Producer 스레드와 Consumer 스레드가 동시에 동작해도 데이터 손실 없음
}
```

---

## Phase 3: MPSC Queue (Multi Producer Single Consumer)

### 3.1 개념

**MPSC Queue란?**
- 여러 생산자 스레드 + 1개의 소비자 스레드
- 로깅 시스템, 이벤트 수집 등에 활용
- SPSC보다 복잡: 여러 Producer가 동시에 push할 때 충돌 방지 필요

### 3.2 도전 과제

- 여러 Producer가 같은 위치에 동시에 쓰려고 할 때?
- CAS (Compare-And-Swap) 연산 필요
- head 업데이트의 원자성 보장

*상세 내용은 Phase 2 완료 후 제공*

---

## Phase 4: MPMC Queue (Multi Producer Multi Consumer)

### 4.1 개념

**MPMC Queue란?**
- 여러 생산자 + 여러 소비자
- 가장 일반적이지만 가장 복잡
- 스레드 풀, 작업 큐 등에 활용

*상세 내용은 Phase 3 완료 후 제공*

---

## Phase 5: ABA 문제

### 5.1 개념

**ABA 문제란?**
```
1. 스레드 A가 값 'A'를 읽음
2. 스레드 A가 잠시 중단됨
3. 스레드 B가 값을 'A' → 'B' → 'A'로 변경
4. 스레드 A가 재개되어 CAS 수행: "값이 여전히 'A'니까 성공!"
   → 하지만 실제로는 중간에 상태가 변경되었음!
```

**왜 문제인가?**
- 포인터 기반 자료구조에서 메모리가 재사용되면 치명적
- 삭제된 노드의 주소가 새 노드에 재할당될 수 있음

*상세 내용은 Phase 4 완료 후 제공*

---

## Phase 6: 메모리 회수 (Memory Reclamation)

### 6.1 개념

**문제:** Lock-free 자료구조에서 노드를 삭제할 때, 다른 스레드가 아직 접근 중일 수 있음

**해결책:**
1. **Hazard Pointer**: 각 스레드가 "이 포인터 사용 중" 표시
2. **Epoch-based Reclamation**: 시간 기반 안전한 삭제
3. **RCU (Read-Copy-Update)**: Linux 커널에서 사용

*상세 내용은 Phase 5 완료 후 제공*

---

## 📋 진행 체크리스트

### Phase 0: 환경 구성
- [ ] CMake 설치 확인
- [ ] 빌드 테스트 (빈 프로젝트 빌드)
- [ ] GoogleTest 동작 확인

### Phase 1: 기초 개념
- [ ] std::thread 사용해보기
- [ ] std::atomic 사용해보기
- [ ] memory_order 실험해보기
- [ ] false sharing 성능 차이 측정해보기

### Phase 2: SPSC Queue
- [ ] 테스트 시나리오 작성
- [ ] 기본 구조 설계
- [ ] 구현
- [ ] 코드 리뷰 및 개선

### Phase 3-7: (Phase 2 완료 후 진행)

---

## 🎯 학습 원칙

1. **테스트 먼저**: 구현 전에 항상 테스트 시나리오를 먼저 작성
2. **스스로 구현**: 정답 코드를 보지 말고 직접 작성
3. **이해 후 진행**: 이전 단계를 완전히 이해한 후 다음 단계로
4. **질문하기**: 막히면 코파일럿에게 힌트 요청 (정답 코드 X)
5. **반복 리뷰**: 작성한 코드를 여러 번 개선

---

## 다음 단계

**Phase 1을 시작할 준비가 되셨나요?**

다음 명령으로 빌드 환경을 먼저 확인하세요:

```powershell
cd h:\dev\lock-free
mkdir build
cd build
cmake ..
cmake --build .
```

빌드가 성공하면, 첫 번째 학습 단계로 진행합니다!
