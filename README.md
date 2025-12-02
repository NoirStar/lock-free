# Lock-Free Concurrency Container Package

Modern C++20 기반 Lock-Free 동시성 자료구조 패키지

## 프로젝트 구조

```
lock-free/
├── include/
│   └── lockfree/
│       ├── spsc_queue.hpp    # Single Producer Single Consumer Queue
│       ├── mpsc_queue.hpp    # Multi Producer Single Consumer Queue
│       └── mpmc_queue.hpp    # Multi Producer Multi Consumer Queue
├── src/                       # 소스 파일 (필요시)
├── tests/                     # GoogleTest 기반 테스트
├── docs/                      # 학습 및 설계 문서
├── CMakeLists.txt
└── README.md
```

## 빌드 방법

### 요구사항
- CMake 3.20 이상
- C++20 지원 컴파일러 (MSVC 19.29+, GCC 10+, Clang 10+)

### 빌드 명령

```powershell
# 빌드 디렉토리 생성 및 이동
mkdir build
cd build

# CMake 구성
cmake ..

# 빌드
cmake --build .

# 테스트 실행
ctest --output-on-failure
```

## 학습 목표

이 프로젝트는 다음을 학습하기 위한 것입니다:

1. **기초 개념**: 스레드, atomic, memory_order
2. **동시성 문제**: 경쟁 조건, 가시성, false sharing
3. **Lock-Free 자료구조**: SPSC → MPSC → MPMC Queue
4. **고급 주제**: ABA 문제, Hazard Pointer, Epoch 기반 메모리 회수

## 문서

- [학습 로드맵](docs/00_LEARNING_ROADMAP.md)
- [Phase 1: 기초 개념](docs/01_FUNDAMENTALS.md)
- [Phase 2: SPSC Queue](docs/02_SPSC_QUEUE.md)
- [Phase 3: MPSC Queue](docs/03_MPSC_QUEUE.md)
- [Phase 4: MPMC Queue](docs/04_MPMC_QUEUE.md)
- [Phase 5: ABA 문제](docs/05_ABA_PROBLEM.md)
- [Phase 6: 메모리 회수](docs/06_MEMORY_RECLAMATION.md)

## 라이선스

MIT License
