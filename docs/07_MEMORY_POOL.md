# Phase 7: Lock-Free 메모리 풀 (Memory Pool)

## 📚 개요

메모리 풀(Object Pool)은 **게임 엔진, 서버, 실시간 시스템**에서 필수적인 메모리 관리 기법입니다.
`new`/`delete`의 오버헤드를 제거하고, 메모리 단편화를 방지하며, Lock-Free 자료구조의 **ABA 문제를 근본적으로 해결**합니다.

---

## 🎮 실제 사용 사례

### 게임 엔진

```
┌─────────────────────────────────────────────────────────────┐
│  Unity, Unreal Engine의 Object Pooling                     │
│                                                             │
│  • 총알, 파티클, 적 캐릭터 등 자주 생성/삭제되는 객체       │
│  • 매 프레임 new/delete 호출 → GC 스파이크 유발            │
│  • 해결: 미리 할당해두고 재사용!                            │
│                                                             │
│  Pool<Bullet> bulletPool(1000);                            │
│  Bullet* b = bulletPool.acquire();  // new 대신            │
│  bulletPool.release(b);              // delete 대신        │
└─────────────────────────────────────────────────────────────┘
```

### 서버 / 네트워크

```
┌─────────────────────────────────────────────────────────────┐
│  고성능 서버의 Connection Pool / Buffer Pool               │
│                                                             │
│  • 수천 개의 동시 연결 처리                                 │
│  • 연결마다 버퍼 할당 → 메모리 단편화                       │
│  • 해결: 고정 크기 버퍼 풀에서 할당/반환                    │
└─────────────────────────────────────────────────────────────┘
```

### Lock-Free 자료구조

```
┌─────────────────────────────────────────────────────────────┐
│  ABA 문제 해결                                              │
│                                                             │
│  문제: delete 후 같은 주소가 재할당되면 ABA 발생!          │
│                                                             │
│  Thread 1: pop() → old = 0x1000                            │
│  Thread 2: pop() → delete 0x1000, push() → new = 0x1000    │
│  Thread 1: CAS(head, 0x1000, ...) → 성공! (하지만 위험)    │
│                                                             │
│  해결: 메모리 풀에서 관리 → 재사용 시점을 우리가 제어!     │
└─────────────────────────────────────────────────────────────┘
```

---

## 💡 설계 원칙

### 1. 고정 크기 블록 (Fixed-Size Blocks)

```
┌─────────────────────────────────────────────────────────────┐
│  모든 블록이 같은 크기 → 단편화 없음!                       │
│                                                             │
│  ┌──────┬──────┬──────┬──────┬──────┬──────┐               │
│  │ 64B  │ 64B  │ 64B  │ 64B  │ 64B  │ 64B  │  ...          │
│  └──────┴──────┴──────┴──────┴──────┴──────┘               │
│                                                             │
│  vs malloc/free:                                            │
│  ┌──┬────────┬──┬──────┬────┬──┐  ← 다양한 크기 = 단편화   │
│  └──┴────────┴──┴──────┴────┴──┘                            │
└─────────────────────────────────────────────────────────────┘
```

### 2. Free List (사용 가능한 블록 연결 리스트)

```
┌─────────────────────────────────────────────────────────────┐
│  Intrusive Free List: 블록 자체에 next 포인터 저장         │
│                                                             │
│  free_head_ ──► [Block 2] ──► [Block 5] ──► [Block 7] ──► nullptr
│                                                             │
│  할당(allocate): head에서 pop                               │
│  해제(deallocate): head에 push                              │
│                                                             │
│  메모리 오버헤드 없음! (사용 중엔 데이터, 미사용 시 포인터) │
└─────────────────────────────────────────────────────────────┘
```

### 3. Lock-Free 연산

```
┌─────────────────────────────────────────────────────────────┐
│  CAS 기반 Lock-Free Push/Pop                                │
│                                                             │
│  allocate():                                                │
│    do {                                                     │
│      old_head = free_list_.load();                         │
│      if (old_head == nullptr) return nullptr;              │
│      new_head = old_head->next;                            │
│    } while (!CAS(&free_list_, old_head, new_head));        │
│    return old_head;                                         │
│                                                             │
│  deallocate(ptr):                                          │
│    do {                                                     │
│      old_head = free_list_.load();                         │
│      ptr->next = old_head;                                 │
│    } while (!CAS(&free_list_, old_head, ptr));             │
└─────────────────────────────────────────────────────────────┘
```

### 4. 동적 확장 (Growable Pool)

```
┌─────────────────────────────────────────────────────────────┐
│  Chunk 기반 확장: 풀이 고갈되면 새 청크 할당               │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │  Chunk 0    │  │  Chunk 1    │  │  Chunk 2    │         │
│  │ [Block x N] │  │ [Block x N] │  │ [Block x N] │  ...    │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
│                                                             │
│  장점: 미리 거대한 메모리 예약 불필요                       │
│  단점: 청크 추가 시 락 필요 (드물게 발생)                  │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔧 구현 단계

### Step 1: FreeNode 구조체

```cpp
/**
 * Free List 노드
 * 
 * 핵심: 할당되지 않은 블록을 이 구조로 해석
 * - 사용 중: T 데이터로 해석
 * - 미사용: FreeNode로 해석 (next 포인터만 사용)
 * 
 * 요구사항: sizeof(FreeNode) <= sizeof(T)
 *          또는 BLOCK_SIZE = max(sizeof(T), sizeof(FreeNode))
 */
struct FreeNode {
    FreeNode* next;
};
```

### Step 2: Chunk 구조체

```cpp
/**
 * 메모리 청크 (연속된 블록 배열)
 * 
 * TODO:
 * 1. 블록 배열을 위한 메모리 할당
 * 2. 정렬된 시작 주소 계산 (캐시 라인 고려)
 * 3. i번째 블록 주소 반환 함수
 */
struct Chunk {
    std::unique_ptr<std::byte[]> memory;
    std::size_t block_count;
    
    // TODO: 생성자, aligned_start(), block_at(i) 구현
};
```

### Step 3: Lock-Free allocate()

```cpp
/**
 * 메모리 할당
 * 
 * 알고리즘:
 * 1. free_list_ head를 원자적으로 읽음
 * 2. head가 nullptr이면:
 *    - growable이면 새 청크 추가 후 재시도
 *    - 아니면 nullptr 반환
 * 3. CAS로 head를 head->next로 변경
 * 4. 성공하면 old_head 반환, 실패하면 재시도
 * 
 * TODO: 구현하세요
 */
T* allocate();
```

### Step 4: Lock-Free deallocate()

```cpp
/**
 * 메모리 해제
 * 
 * 알고리즘:
 * 1. ptr을 FreeNode*로 캐스팅
 * 2. 현재 free_list_ head 읽음
 * 3. ptr->next = 현재 head
 * 4. CAS로 head를 ptr로 변경
 * 5. 실패하면 재시도
 * 
 * TODO: 구현하세요
 */
void deallocate(T* ptr);
```

### Step 5: 동적 확장

```cpp
/**
 * 새 청크 추가
 * 
 * 주의: 여러 스레드가 동시에 확장 시도할 수 있음!
 * 
 * 알고리즘:
 * 1. 스핀락으로 chunks_ 벡터 보호 (드물게 발생)
 * 2. 새 Chunk 생성
 * 3. 청크의 모든 블록을 free list에 push
 * 
 * TODO: 구현하세요
 */
void grow();
```

---

## ⚠️ 주의사항

### ABA 문제 (Free List 자체에서도 발생!)

```
Thread 1: allocate() 시작, old_head = A
Thread 1: (중단)
Thread 2: allocate() → A 획득
Thread 2: deallocate(A) → A 반환
Thread 1: CAS(head, A, A->next) → 성공! (하지만 A->next가 변경되었을 수 있음)
```

**해결책:**
1. **Tagged Pointer**: Phase 6에서 배운 기법 적용
2. **Version Counter**: 글로벌 버전 카운터로 변경 감지
3. **Hazard Pointer**: 고급 기법 (선택적 학습)

### 메모리 정렬

```cpp
// 캐시 라인 경계에 맞춰 할당 (False Sharing 방지)
static constexpr std::size_t BLOCK_ALIGNMENT = 64;  // 또는 alignof(T)

// 정렬된 주소 계산
std::uintptr_t aligned = (addr + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
```

---

## ✅ TDD 테스트 시나리오

### 기본 동작
- [ ] 단일 할당/해제
- [ ] 다중 할당 (중복 주소 없음)
- [ ] 해제 후 재사용 (LIFO)

### 풀 관리
- [ ] 풀 고갈 시 nullptr 반환 (고정 풀)
- [ ] 풀 고갈 시 자동 확장 (동적 풀)
- [ ] 통계: 용량, 할당 수, 가용 수

### 고급 API
- [ ] construct(): 할당 + 생성자 호출
- [ ] destroy(): 소멸자 호출 + 해제

### 멀티스레드
- [ ] 동시 할당/해제 (데이터 무결성)
- [ ] ABA 스트레스 테스트

---

## 📋 체크리스트

- [ ] FreeNode 구조체 이해
- [ ] Chunk 구조체 구현
- [ ] allocate() Lock-Free 구현
- [ ] deallocate() Lock-Free 구현
- [ ] 동적 확장 구현
- [ ] 단위 테스트 통과
- [ ] 멀티스레드 테스트 통과
- [ ] (선택) ABA 방지 기법 적용

---

## 🎯 학습 목표

1. **게임 엔진 패턴 이해**: Object Pool이 왜 필수인지
2. **Lock-Free 메모리 관리**: malloc/free 없이 스레드 안전한 할당
3. **Intrusive 자료구조**: 추가 메모리 없이 연결 리스트 구현
4. **실전 최적화**: 캐시 정렬, 동적 확장, 통계

---

## 다음 단계

메모리 풀 구현 완료 후:
1. **ABASafeStack과 통합**: new/delete 대신 풀 사용
2. **Hazard Pointer** (선택): 더 강력한 메모리 회수 기법
3. **Thread-Local Cache** (선택): 경합 감소를 위한 최적화
