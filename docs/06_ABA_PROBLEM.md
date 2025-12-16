# Phase 6: ABA 문제 (ABA Problem)

## 📚 개요

ABA 문제는 Lock-Free 자료구조에서 발생하는 가장 교묘하고 위험한 버그 중 하나입니다.

---

## 🎯 ABA 문제란?

### 기본 개념

```
CAS(Compare-And-Swap) 연산:
"현재 값이 A면 B로 바꿔라"

문제 상황:
1. Thread 1이 값 'A'를 읽음
2. Thread 1이 일시 중단됨
3. Thread 2가 값을 A → B → A로 변경
4. Thread 1이 재개되어 CAS 실행
5. "값이 여전히 A네? 변경 없음!" → CAS 성공!

하지만 실제로는 중간에 상태가 변경되었습니다!
```

### 왜 위험한가?

```
Lock-Free Stack에서의 ABA 문제:

초기 상태:
    head → [A] → [B] → [C] → nullptr

Thread 1: pop() 시작
    1. old_head = A
    2. next = B    ← 이 값을 저장
    3. (중단됨...)

Thread 2: 여러 작업 수행
    4. pop() → A 제거, delete A
    5. pop() → B 제거, delete B  
    6. push(D) → 새 노드 (우연히 A와 같은 주소!)
    7. push(E)
    
현재 상태:
    head → [A*] → [E] → [C] → nullptr
    (* 같은 주소지만 다른 노드!)

Thread 1: 재개
    8. CAS(head, A, B) → 주소가 같으니 성공!
    9. head = B ← 이미 삭제된 노드!!!

결과:
    head → [B] → ??? (dangling pointer!)
    
    → 프로그램 크래시 또는 데이터 손상!
```

---

## 💻 코드로 보는 ABA 문제

### 취약한 코드

```cpp
template <typename T>
class ABAProneStack {
    struct Node {
        T data;
        Node* next;
    };
    
    std::atomic<Node*> head_;
    
    std::optional<T> pop() {
        Node* old_head = head_.load();
        
        do {
            if (old_head == nullptr) return std::nullopt;
            
            // ⚠️ 위험: old_head->next 접근
            // old_head가 삭제되고 메모리가 재사용되었다면?
            
        } while (!head_.compare_exchange_weak(
            old_head,
            old_head->next,  // ⚠️ dangling pointer 가능!
            std::memory_order_acquire
        ));
        
        T result = old_head->data;
        delete old_head;  // → 이 메모리가 재사용될 수 있음!
        return result;
    }
};
```

### 문제 발생 타임라인

```
시간 ──────────────────────────────────────────────────────────→

Thread A                          Thread B
────────                          ────────
old_head = 0x1000 (Node A)
next = 0x2000 (Node B)
                                  
 zzz (sleeping)                   pop() → delete 0x1000
                                  pop() → delete 0x2000
                                  push(X) → new = 0x1000 (재사용!)
                                  
wake up!
CAS(head, 0x1000, 0x2000)
  → 0x1000 == 0x1000 ✓ 성공!
  → head = 0x2000 (삭제된 메모리!)

💥 CRASH or DATA CORRUPTION
```

---

## 🔧 해결 방법

### 방법 1: Tagged Pointer (버전 태그)

포인터에 버전 번호를 함께 저장:

```cpp
// 64비트 시스템에서 상위 비트 활용
struct TaggedPointer {
    // 하위 48비트: 실제 포인터
    // 상위 16비트: 버전 태그
    uintptr_t value;
    
    Node* ptr() const { 
        return reinterpret_cast<Node*>(value & 0xFFFFFFFFFFFF); 
    }
    
    uint16_t tag() const { 
        return static_cast<uint16_t>(value >> 48); 
    }
    
    TaggedPointer(Node* p, uint16_t t) {
        value = reinterpret_cast<uintptr_t>(p) | (uintptr_t(t) << 48);
    }
};

// CAS할 때 포인터 + 태그 모두 비교
// 같은 주소라도 태그가 다르면 CAS 실패!
```

**장점:** 간단하고 효율적
**단점:** 태그 오버플로우 가능 (65536번 후 wrap-around)

### 방법 2: Double-Width CAS (DWCAS)

128비트 원자적 연산 사용:

```cpp
struct alignas(16) TaggedPointer {
    Node* ptr;       // 64비트
    uint64_t tag;    // 64비트
};

std::atomic<TaggedPointer> head_;

// x86-64: CMPXCHG16B 명령어 사용
// 두 값을 동시에 원자적으로 비교/교환
```

**장점:** 태그 오버플로우 걱정 없음
**단점:** 플랫폼 의존적, 정렬 필요

### 방법 3: Hazard Pointer

"이 포인터 사용 중" 표시:

```cpp
thread_local Node* hazard_pointer = nullptr;

std::optional<T> pop() {
    Node* old_head;
    
    do {
        old_head = head_.load();
        if (old_head == nullptr) return std::nullopt;
        
        // "나 이 포인터 쓰고 있어!"
        hazard_pointer = old_head;
        
        // 다시 확인 (등록하는 동안 바뀌었을 수 있음)
        if (head_.load() != old_head) continue;
        
        // 이제 old_head는 안전함
        
    } while (!head_.compare_exchange_weak(...));
    
    hazard_pointer = nullptr;  // 사용 완료
    
    // 삭제는 나중에: retire_node(old_head);
    // 아무도 hazard로 등록 안 했을 때만 실제 delete
}
```

**장점:** 완벽한 해결책
**단점:** 구현 복잡, 약간의 오버헤드

### 방법 4: Epoch-based Reclamation

시간 기반 안전한 삭제:

```cpp
std::atomic<uint64_t> global_epoch{0};
thread_local uint64_t local_epoch;
thread_local std::vector<Node*> retire_list;

void enter_critical() {
    local_epoch = global_epoch.load();
}

void leave_critical() {
    // 이 epoch의 작업 완료
}

void retire_node(Node* node) {
    retire_list.push_back({node, global_epoch.load()});
    
    // 모든 스레드가 이 epoch을 지났으면 삭제 가능
    try_reclaim();
}
```

**장점:** Hazard Pointer보다 간단
**단점:** 메모리 해제 지연 가능

---

## 🧪 테스트 코드

`tests/test_aba_problem.cpp`에 학습용 테스트가 있습니다:

```bash
cd build
cmake --build . --config Release --target test_aba_problem
.\tests\Release\test_aba_problem.exe
```

---

## 🎯 과제

1. `test_aba_problem.cpp`의 테스트들을 실행하고 결과 관찰
2. ABA 문제가 왜 위험한지 이해
3. 아래 중 하나를 선택해서 구현:
   - Tagged Pointer 버전 스택
   - Hazard Pointer 버전 스택

---

## 💭 생각해볼 질문

1. **SPSC Queue에서는 ABA 문제가 발생할까?**
   - 힌트: Producer와 Consumer가 각각 다른 포인터만 수정

2. **Mutex를 쓰면 ABA 문제가 없는 이유는?**
   - 힌트: 상호 배제 vs 낙관적 동시성

3. **GC(Garbage Collection)가 있는 언어에서는?**
   - 힌트: 참조가 있으면 메모리 재사용 안 됨

4. **Tagged Pointer의 16비트 태그가 오버플로우하면?**
   - 힌트: 2^16 = 65536번마다 wrap-around

---

## 📚 참고 자료

- [Lock-Free Programming - Preshing](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [ABA Problem - Wikipedia](https://en.wikipedia.org/wiki/ABA_problem)
- [Hazard Pointers - Maged Michael](https://www.research.ibm.com/people/m/michael/ieeetpds-2004.pdf)
