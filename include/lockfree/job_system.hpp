/**
 * Lock-Free Job System
 * 
 * 멀티코어 병렬 처리를 위한 Job 스케줄러
 * 
 * 특징:
 *   - Lock-Free Job 큐 (MPMC Queue 활용)
 *   - Memory Pool 기반 Job 할당
 *   - Job 의존성 지원 (Counter)
 *   - Work Stealing (고급, 선택)
 * 
 * 사용 예:
 *   JobSystem js(4);  // 4개 워커 스레드
 *   
 *   Counter counter(0);
 *   for (int i = 0; i < 100; i++) {
 *       js.schedule([i]() { process(i); }, &counter);
 *   }
 *   js.wait_for_counter(&counter);
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │                       Job System                            │
 * │                                                              │
 * │  ┌─────────────────────────────────────────────────────┐    │
 * │  │                    Job Queue                         │    │
 * │  │    [Job1] → [Job2] → [Job3] → [Job4] → ...          │    │
 * │  └─────────────────────────────────────────────────────┘    │
 * │           │           │           │           │              │
 * │           ▼           ▼           ▼           ▼              │
 * │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐         │
 * │  │ Worker 0│  │ Worker 1│  │ Worker 2│  │ Worker 3│         │
 * │  └─────────┘  └─────────┘  └─────────┘  └─────────┘         │
 * └─────────────────────────────────────────────────────────────┘
 */

#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <cstdint>
#include <cassert>

// 우리가 만든 Lock-Free 자료구조들!
#include "mpmc_queue.hpp"
#include "memory_pool.hpp"

namespace lockfree {

// ========================================
// Forward Declarations
// ========================================

struct Job;
struct Counter;

// ========================================
// Job 구조체
// ========================================

/**
 * Job: 실행할 작업 단위
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │  Job 구조                                                    │
 * │                                                              │
 * │  ┌──────────────┐                                           │
 * │  │   function   │  ← 실행할 함수                            │
 * │  │   counter    │  ← 완료 시 감소할 카운터                   │
 * │  │   parent     │  ← 부모 Job (선택)                        │
 * │  │ unfinished   │  ← 자식 Job 수 + 1                        │
 * │  └──────────────┘                                           │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * TODO: 직접 구현해보세요!
 */
struct Job {
    // ========================================
    // TODO: 멤버 변수 정의
    // ========================================
    
    /**
     * 실행할 함수
     * 
     * 힌트: std::function<void()> 사용
     * 주의: std::function은 약간의 오버헤드가 있음
     *       (고성능 버전에서는 함수 포인터 + void* 사용)
     */
    std::function<void()> function;
    
    /**
     * 완료 시 감소할 카운터 (선택적)
     * 
     * Job 완료 시 counter->decrement() 호출
     * nullptr이면 무시
     */
    Counter* counter{nullptr};
    
    /**
     * 부모 Job (선택적)
     * 
     * 자식 Job 완료 시 부모의 unfinished_jobs 감소
     * nullptr이면 최상위 Job
     */
    Job* parent{nullptr};
    
    /**
     * 미완료 작업 수
     * 
     * 초기값: 1 (자기 자신)
     * 자식 생성 시: +1
     * 자식 완료 시: -1
     * 0이 되면 Job 완전 종료
     */
    std::atomic<std::int32_t> unfinished_jobs{1};
    
    // ========================================
    // TODO: 기본 생성자
    // ========================================
    
    Job() = default;
    
    // ========================================
    // TODO: 함수와 카운터를 받는 생성자
    // ========================================
    
    template <typename F>
    explicit Job(F&& func, Counter* cnt = nullptr, Job* par = nullptr)
        : function(std::forward<F>(func))
        , counter(cnt)
        , parent(par)
        , unfinished_jobs(1)
    {}
};

// ========================================
// Counter 구조체
// ========================================

/**
 * Counter: Job 그룹의 완료 추적
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │  Counter 사용 패턴                                           │
 * │                                                              │
 * │  Counter counter(0);                                        │
 * │                                                              │
 * │  for (int i = 0; i < 10; i++) {                             │
 * │      schedule(job, &counter);  // counter++                 │
 * │  }                                                          │
 * │                                                              │
 * │  wait_for_counter(&counter);   // counter == 0 될 때까지    │
 * │                                                              │
 * │  // 모든 Job 완료!                                           │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * TODO: 직접 구현해보세요!
 */
struct Counter {
    /**
     * 카운터 값 (atomic)
     * 
     * 양수: 진행 중인 Job 수
     * 0: 모든 Job 완료
     */
    std::atomic<std::int32_t> value{0};
    
    // ========================================
    // TODO: 생성자
    // ========================================
    
    explicit Counter(std::int32_t initial = 0) : value(initial) {}
    
    // ========================================
    // TODO: 증가 (Job 시작 시)
    // ========================================
    
    /**
     * 카운터 증가
     * 
     * 힌트: fetch_add 사용
     * Memory Order: relaxed로 충분할까? acquire/release가 필요할까?
     */
    void increment() {
        // TODO: 구현
        value.fetch_add(1, std::memory_order_relaxed);
    }
    
    // ========================================
    // TODO: 감소 (Job 완료 시)
    // ========================================
    
    /**
     * 카운터 감소
     * 
     * 힌트: fetch_sub 사용
     * 반환값: 감소 후 0이 되었는지?
     */
    bool decrement() {
        // TODO: 구현
        std::int32_t prev = value.fetch_sub(1, std::memory_order_acq_rel);
        return prev == 1;  // 0이 되었음
    }
    
    // ========================================
    // TODO: 현재 값 확인
    // ========================================
    
    /**
     * 모든 Job이 완료되었는지?
     */
    bool is_zero() const {
        // TODO: 구현
        return value.load(std::memory_order_acquire) == 0;
    }
    
    /**
     * 현재 카운터 값
     */
    std::int32_t get() const {
        return value.load(std::memory_order_acquire);
    }
};

// ========================================
// JobSystem 클래스
// ========================================

/**
 * JobSystem: 멀티스레드 Job 스케줄러
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │  핵심 컴포넌트                                               │
 * │                                                              │
 * │  1. Job Queue (MPMC)                                        │
 * │     - 모든 워커가 공유                                       │
 * │     - Lock-Free push/pop                                    │
 * │                                                              │
 * │  2. Memory Pool                                              │
 * │     - Job 객체 할당/해제                                     │
 * │     - Lock-Free, ABA-Safe                                   │
 * │                                                              │
 * │  3. Worker Threads                                          │
 * │     - CPU 코어 수만큼 생성                                   │
 * │     - Job을 가져와서 실행                                    │
 * └─────────────────────────────────────────────────────────────┘
 * 
 * TODO: 직접 구현해보세요!
 */
class JobSystem {
public:
    // ========================================
    // 상수
    // ========================================
    
    static constexpr std::size_t DEFAULT_QUEUE_SIZE = 4096;
    static constexpr std::size_t DEFAULT_POOL_SIZE = 4096;
    
private:
    // ========================================
    // 멤버 변수
    // ========================================
    
    /**
     * Job 큐 (MPMC Queue)
     * 
     * 우리가 만든 Lock-Free 큐 사용!
     * 모든 워커가 여기서 Job을 가져감
     */
    MPMCQueue<Job*> job_queue_;
    
    /**
     * Job 메모리 풀
     * 
     * new/delete 대신 풀에서 할당
     * ABA 문제 해결에도 도움!
     */
    MemoryPool<Job> job_pool_;
    
    /**
     * 워커 스레드 목록
     */
    std::vector<std::thread> workers_;
    
    /**
     * 실행 상태
     * 
     * true: 실행 중
     * false: 종료 요청
     */
    std::atomic<bool> running_{true};
    
    /**
     * 대기 중인 Job 수 (디버깅/통계)
     */
    std::atomic<std::size_t> pending_jobs_{0};

public:
    // ========================================
    // TODO: 생성자
    // ========================================
    
    /**
     * JobSystem 생성자
     * 
     * @param num_workers 워커 스레드 수 (0이면 하드웨어 스레드 수)
     * @param queue_size Job 큐 크기
     * @param pool_size Job 풀 크기
     * 
     * 알고리즘:
     * 1. num_workers가 0이면 std::thread::hardware_concurrency() 사용
     * 2. job_queue_, job_pool_ 초기화
     * 3. 워커 스레드들 생성 및 시작
     */
    explicit JobSystem(
        std::size_t num_workers = 0,
        std::size_t queue_size = DEFAULT_QUEUE_SIZE,
        std::size_t pool_size = DEFAULT_POOL_SIZE
    );
    
    // ========================================
    // TODO: 소멸자
    // ========================================
    
    /**
     * JobSystem 소멸자
     * 
     * 알고리즘:
     * 1. running_ = false 설정
     * 2. 모든 워커 스레드 join()
     * 3. 남은 Job들 정리 (메모리 풀 반환)
     */
    ~JobSystem();
    
    // 복사/이동 금지
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;
    
    // ========================================
    // TODO: Job 스케줄링 API
    // ========================================
    
    /**
     * Job 스케줄링 (람다/함수 버전)
     * 
     * @param func 실행할 함수
     * @param counter 완료 카운터 (선택)
     * 
     * 알고리즘:
     * 1. Job 풀에서 메모리 할당
     * 2. Job 초기화 (function, counter)
     * 3. counter가 있으면 increment()
     * 4. Job 큐에 push
     * 
     * 힌트: job_pool_.construct() 사용
     */
    template <typename F>
    void schedule(F&& func, Counter* counter = nullptr);
    
    /**
     * Job 스케줄링 (Job 포인터 버전)
     * 
     * @param job 스케줄할 Job (이미 할당/초기화됨)
     * 
     * 주의: Job은 job_pool_에서 할당받은 것이어야 함
     */
    void schedule(Job* job);
    
    // ========================================
    // TODO: 대기 API
    // ========================================
    
    /**
     * 카운터가 0이 될 때까지 대기
     * 
     * @param counter 대기할 카운터
     * 
     * 주의: 단순히 스핀 대기하면 CPU 낭비!
     * 
     * 최적화 힌트:
     * 1. 대기 중에도 Job을 실행하면 좋음 (협력적 대기)
     * 2. 또는 조건 변수 사용 (CPU 절약)
     * 3. 여기서는 간단히 스핀 + yield로 시작
     */
    void wait_for_counter(Counter* counter);
    
    /**
     * 모든 Job 완료 대기
     * 
     * pending_jobs_ == 0 이 될 때까지 대기
     */
    void wait_all();
    
    // ========================================
    // TODO: 유틸리티
    // ========================================
    
    /**
     * 워커 스레드 수
     */
    std::size_t worker_count() const {
        return workers_.size();
    }
    
    /**
     * 대기 중인 Job 수
     */
    std::size_t pending_jobs() const {
        return pending_jobs_.load(std::memory_order_relaxed);
    }
    
    /**
     * 실행 중인지?
     */
    bool is_running() const {
        return running_.load(std::memory_order_relaxed);
    }
    
    /**
     * Job 풀에서 직접 할당 (고급 사용)
     * 
     * Parent-Child 관계 설정 시 필요
     */
    template <typename... Args>
    Job* allocate_job(Args&&... args) {
        return job_pool_.construct(std::forward<Args>(args)...);
    }
    
    /**
     * Job 반환 (고급 사용)
     */
    void deallocate_job(Job* job) {
        job_pool_.destroy(job);
    }

private:
    // ========================================
    // TODO: 내부 구현
    // ========================================
    
    /**
     * 워커 스레드 메인 루프
     * 
     * @param worker_id 워커 ID (디버깅용)
     * 
     * 알고리즘:
     * while (running_) {
     *     Job* job = try_get_job();  // 큐에서 Job 가져오기
     *     if (job) {
     *         execute(job);          // 실행
     *         finish(job);           // 완료 처리
     *     } else {
     *         wait_for_job();        // 잠시 대기
     *     }
     * }
     */
    void worker_main(std::size_t worker_id);
    
    /**
     * Job 큐에서 Job 가져오기
     * 
     * @return 가져온 Job, 큐가 비었으면 nullptr
     */
    Job* try_get_job();
    
    /**
     * Job 실행
     * 
     * @param job 실행할 Job
     */
    void execute(Job* job);
    
    /**
     * Job 완료 처리
     * 
     * @param job 완료된 Job
     * 
     * 알고리즘:
     * 1. counter가 있으면 decrement()
     * 2. parent가 있으면 parent->unfinished_jobs 감소
     * 3. unfinished_jobs == 0 이면:
     *    - Job 메모리 반환
     *    - pending_jobs_ 감소
     */
    void finish(Job* job);
    
    /**
     * Job 대기 (큐가 빌 때)
     * 
     * 옵션:
     * 1. std::this_thread::yield() - 간단, 높은 CPU
     * 2. 조건 변수 - 복잡, 낮은 CPU
     * 3. 하이브리드 - 잠깐 스핀 후 대기
     */
    void wait_for_job();
};

// ========================================
// 인라인 구현 (템플릿 메서드)
// ========================================

template <typename F>
void JobSystem::schedule(F&& func, Counter* counter) {
    // TODO: 구현해보세요!
    
    // 힌트:
    // 1. job_pool_.construct(...)로 Job 할당
    // 2. counter가 있으면 counter->increment()
    // 3. schedule(Job* job) 호출
    
    Job* job = job_pool_.construct(std::forward<F>(func), counter);
    if (job) {
        if (counter) {
            counter->increment();
        }
        schedule(job);
    }
}

// ========================================
// 선언만 (구현은 .cpp에서)
// ========================================

// 나머지 메서드들은 test_job_system.cpp의 테스트를 통과하도록
// 직접 구현해보세요!

} // namespace lockfree
