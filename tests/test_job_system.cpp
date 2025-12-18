/**
 * Job System 테스트
 * 
 * Step별로 구현하며 테스트를 통과시켜 보세요!
 * 
 * 학습 순서:
 *   1. Counter 테스트 통과
 *   2. Simple Job 테스트 통과
 *   3. Concurrent Jobs 테스트 통과
 *   4. Parent-Child 테스트 통과 (선택)
 */

#include <gtest/gtest.h>
#include <lockfree/job_system.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <numeric>
#include <random>

using namespace lockfree;

// ========================================
// Step 1: Counter 테스트
// ========================================

/**
 * Counter 기본 동작 테스트
 * 
 * 이 테스트가 통과하면 Counter 구현 완료!
 */
TEST(JobSystemCounter, BasicOperations) {
    Counter counter(0);
    
    EXPECT_EQ(counter.get(), 0);
    EXPECT_TRUE(counter.is_zero());
    
    counter.increment();
    EXPECT_EQ(counter.get(), 1);
    EXPECT_FALSE(counter.is_zero());
    
    counter.increment();
    EXPECT_EQ(counter.get(), 2);
    
    bool was_last = counter.decrement();
    EXPECT_FALSE(was_last);
    EXPECT_EQ(counter.get(), 1);
    
    was_last = counter.decrement();
    EXPECT_TRUE(was_last);
    EXPECT_TRUE(counter.is_zero());
}

/**
 * Counter 초기값 테스트
 */
TEST(JobSystemCounter, InitialValue) {
    Counter counter(10);
    
    EXPECT_EQ(counter.get(), 10);
    EXPECT_FALSE(counter.is_zero());
    
    for (int i = 0; i < 10; ++i) {
        counter.decrement();
    }
    
    EXPECT_TRUE(counter.is_zero());
}

/**
 * Counter 멀티스레드 안전성 테스트
 */
TEST(JobSystemCounter, ConcurrentIncrementDecrement) {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 10000;
    
    Counter counter(0);
    
    auto worker = [&]() {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            counter.increment();
        }
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            counter.decrement();
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_TRUE(counter.is_zero()) << "Counter should be 0 after equal increments and decrements";
}

// ========================================
// Step 2: Job 구조체 테스트
// ========================================

/**
 * Job 기본 생성 테스트
 */
TEST(JobSystemJob, BasicConstruction) {
    bool executed = false;
    
    Job job([&]() { executed = true; });
    
    EXPECT_FALSE(executed);
    EXPECT_EQ(job.counter, nullptr);
    EXPECT_EQ(job.parent, nullptr);
    EXPECT_EQ(job.unfinished_jobs.load(), 1);
    
    // 직접 실행
    job.function();
    EXPECT_TRUE(executed);
}

/**
 * Job with Counter
 */
TEST(JobSystemJob, WithCounter) {
    Counter counter(0);
    bool executed = false;
    
    Job job([&]() { executed = true; }, &counter);
    
    EXPECT_EQ(job.counter, &counter);
    
    job.function();
    EXPECT_TRUE(executed);
}

// ========================================
// Step 3: JobSystem 기본 테스트
// ========================================

/**
 * JobSystem 생성/소멸 테스트
 * 
 * JobSystem 생성자/소멸자 구현 후 통과
 */
TEST(JobSystem, CreateDestroy) {
    {
        JobSystem js(2);  // 2개 워커
        
        EXPECT_EQ(js.worker_count(), 2);
        EXPECT_TRUE(js.is_running());
        EXPECT_EQ(js.pending_jobs(), 0);
    }
    // 소멸자에서 워커들이 정상 종료되어야 함
}

/**
 * 기본 워커 수 (하드웨어 스레드 수)
 */
TEST(JobSystem, DefaultWorkerCount) {
    JobSystem js;  // 기본값
    
    std::size_t hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) hw_threads = 1;  // 감지 실패 시
    
    EXPECT_EQ(js.worker_count(), hw_threads);
}

// ========================================
// Step 4: Simple Job 실행 테스트
// ========================================

/**
 * 단일 Job 실행
 * 
 * schedule() + wait_for_counter() 구현 후 통과
 */
TEST(JobSystem, SingleJobExecution) {
    JobSystem js(2);
    
    std::atomic<int> result{0};
    Counter counter(0);
    
    js.schedule([&]() {
        result.store(42, std::memory_order_relaxed);
    }, &counter);
    
    js.wait_for_counter(&counter);
    
    EXPECT_EQ(result.load(), 42) << "Job should have executed";
}

/**
 * 여러 Job 순차 스케줄링
 */
TEST(JobSystem, MultipleJobsSequential) {
    JobSystem js(2);
    
    std::atomic<int> count{0};
    Counter counter(0);
    
    for (int i = 0; i < 10; ++i) {
        js.schedule([&]() {
            count.fetch_add(1, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    EXPECT_EQ(count.load(), 10) << "All 10 jobs should have executed";
}

// ========================================
// Step 5: 병렬 Job 테스트
// ========================================

/**
 * 많은 Job 병렬 실행
 */
TEST(JobSystem, ManyJobsParallel) {
    constexpr int NUM_JOBS = 1000;
    
    JobSystem js(4);
    
    std::atomic<int> count{0};
    Counter counter(0);
    
    for (int i = 0; i < NUM_JOBS; ++i) {
        js.schedule([&]() {
            count.fetch_add(1, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    EXPECT_EQ(count.load(), NUM_JOBS) << "All jobs should have executed";
}

/**
 * 결과 합산 정확성 테스트
 */
TEST(JobSystem, ParallelSum) {
    constexpr int NUM_JOBS = 100;
    
    JobSystem js(4);
    
    std::atomic<int> sum{0};
    Counter counter(0);
    
    // 1부터 NUM_JOBS까지 합산
    for (int i = 1; i <= NUM_JOBS; ++i) {
        js.schedule([&sum, value = i]() {
            sum.fetch_add(value, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    int expected = NUM_JOBS * (NUM_JOBS + 1) / 2;  // 가우스 합 공식
    EXPECT_EQ(sum.load(), expected) << "Sum should be correct";
}

/**
 * 배열 병렬 처리 테스트
 */
TEST(JobSystem, ParallelArrayProcessing) {
    constexpr int ARRAY_SIZE = 10000;
    
    JobSystem js(4);
    
    std::vector<int> data(ARRAY_SIZE);
    std::iota(data.begin(), data.end(), 0);  // 0, 1, 2, ... ARRAY_SIZE-1
    
    std::atomic<long long> sum{0};
    Counter counter(0);
    
    // 청크로 나누어 처리
    constexpr int CHUNK_SIZE = 100;
    for (int start = 0; start < ARRAY_SIZE; start += CHUNK_SIZE) {
        int end = std::min(start + CHUNK_SIZE, ARRAY_SIZE);
        
        js.schedule([&data, &sum, start, end]() {
            long long local_sum = 0;
            for (int i = start; i < end; ++i) {
                local_sum += data[i];
            }
            sum.fetch_add(local_sum, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    long long expected = static_cast<long long>(ARRAY_SIZE - 1) * ARRAY_SIZE / 2;
    EXPECT_EQ(sum.load(), expected);
}

// ========================================
// Step 6: 스트레스 테스트
// ========================================

/**
 * 대량 Job 스트레스 테스트
 */
TEST(JobSystem, StressTest) {
    constexpr int NUM_JOBS = 10000;
    
    JobSystem js(4);
    
    std::atomic<int> executed{0};
    Counter counter(0);
    
    for (int i = 0; i < NUM_JOBS; ++i) {
        js.schedule([&]() {
            // 약간의 작업 시뮬레이션
            volatile int dummy = 0;
            for (int j = 0; j < 100; ++j) {
                dummy += j;
            }
            executed.fetch_add(1, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    EXPECT_EQ(executed.load(), NUM_JOBS);
    EXPECT_EQ(js.pending_jobs(), 0);
}

/**
 * 반복적인 Schedule/Wait 테스트
 */
TEST(JobSystem, RepeatedScheduleWait) {
    JobSystem js(4);
    
    for (int round = 0; round < 100; ++round) {
        std::atomic<int> count{0};
        Counter counter(0);
        
        for (int i = 0; i < 10; ++i) {
            js.schedule([&]() {
                count.fetch_add(1, std::memory_order_relaxed);
            }, &counter);
        }
        
        js.wait_for_counter(&counter);
        
        EXPECT_EQ(count.load(), 10) << "Round " << round << " failed";
    }
}

// ========================================
// Step 7: wait_all() 테스트
// ========================================

/**
 * wait_all() 테스트
 */
TEST(JobSystem, WaitAll) {
    JobSystem js(4);
    
    std::atomic<int> count{0};
    
    // Counter 없이 스케줄링
    for (int i = 0; i < 100; ++i) {
        js.schedule([&]() {
            count.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    js.wait_all();
    
    EXPECT_EQ(count.load(), 100);
}

// ========================================
// 고급: Parent-Child 테스트 (선택적)
// ========================================

/**
 * Parent-Child Job 관계 테스트
 * 
 * 이 테스트는 고급 기능입니다.
 * 먼저 기본 테스트들을 모두 통과시킨 후 도전하세요!
 */
TEST(JobSystem, DISABLED_ParentChildJobs) {
    JobSystem js(4);
    
    std::atomic<int> parent_finished{0};
    std::atomic<int> children_finished{0};
    
    // TODO: Parent-Child 관계 구현 후 활성화
    // 
    // Parent Job은 모든 Child가 완료된 후 완료되어야 함
    // 
    // Parent
    //   ├── Child 1
    //   ├── Child 2
    //   └── Child 3
    
    EXPECT_EQ(children_finished.load(), 3);
    EXPECT_EQ(parent_finished.load(), 1);
}

// ========================================
// 벤치마크 (선택적)
// ========================================

/**
 * 성능 벤치마크
 */
TEST(JobSystem, DISABLED_PerformanceBenchmark) {
    constexpr int NUM_JOBS = 100000;
    
    JobSystem js(std::thread::hardware_concurrency());
    
    std::atomic<int> count{0};
    Counter counter(0);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_JOBS; ++i) {
        js.schedule([&]() {
            count.fetch_add(1, std::memory_order_relaxed);
        }, &counter);
    }
    
    js.wait_for_counter(&counter);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "[  BENCH   ] " << NUM_JOBS << " jobs in " << us << " us\n";
    std::cout << "[  BENCH   ] " << (us * 1000.0 / NUM_JOBS) << " ns/job\n";
    std::cout << "[  BENCH   ] " << (NUM_JOBS * 1000000.0 / us) << " jobs/sec\n";
    
    EXPECT_EQ(count.load(), NUM_JOBS);
}

/**
 * 워커 수에 따른 스케일링 테스트
 */
TEST(JobSystem, DISABLED_ScalingBenchmark) {
    constexpr int NUM_JOBS = 10000;
    constexpr int WORK_PER_JOB = 1000;  // 각 Job의 작업량
    
    for (std::size_t workers = 1; workers <= std::thread::hardware_concurrency(); ++workers) {
        JobSystem js(workers);
        
        std::atomic<long long> result{0};
        Counter counter(0);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < NUM_JOBS; ++i) {
            js.schedule([&]() {
                // 실제 작업 시뮬레이션
                long long sum = 0;
                for (int j = 0; j < WORK_PER_JOB; ++j) {
                    sum += j * j;
                }
                result.fetch_add(sum, std::memory_order_relaxed);
            }, &counter);
        }
        
        js.wait_for_counter(&counter);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        std::cout << "[  BENCH   ] " << workers << " workers: " << ms << " ms\n";
    }
}
