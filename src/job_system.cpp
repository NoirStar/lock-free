/**
 * Lock-Free Job System 구현
 * 
 * TODO: 직접 구현해보세요!
 */

#include "lockfree/job_system.hpp"

namespace lockfree {

// ========================================
// 생성자
// ========================================

JobSystem::JobSystem(
    std::size_t num_workers,
    [[maybe_unused]] std::size_t queue_size,
    std::size_t pool_size
)
    : job_pool_(pool_size)
    , running_(true)
    , pending_jobs_(0)
{
    if (num_workers == 0) {
        num_workers = std::thread::hardware_concurrency();
    }

    for (size_t i=0; i<num_workers; ++i) {
        workers_.emplace_back(&JobSystem::worker_main, this, i);
    }
}

// ========================================
// 소멸자
// ========================================

JobSystem::~JobSystem() {
    running_.store(false, std::memory_order_relaxed);
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    // 남은 Job 정리
    Job* job = nullptr;
    while (job_queue_.pop(job)) {
        job_pool_.destroy(job);
    }
}

// ========================================
// Job 스케줄링
// ========================================

void JobSystem::schedule(Job* job) {
    pending_jobs_.fetch_add(1, std::memory_order_relaxed);
    job_queue_.push(job);
}

// ========================================
// 대기 API
// ========================================

void JobSystem::wait_for_counter(Counter* counter) {
    while (!counter->is_zero()) {
        // 협력적 대기: 대기하면서 다른 Job 실행
        Job* job = try_get_job();
        if (job) {
            execute(job);
            finish(job);
        } else {
            std::this_thread::yield();  // 큐가 비었으면 잠시 양보
        }
    }
}

void JobSystem::wait_all() {
    while (pending_jobs_.load(std::memory_order_acquire) > 0) {
        // 협력적 대기: 대기하면서 Job 실행
        Job* job = try_get_job();
        if (job) {
            execute(job);
            finish(job);
        } else {
            std::this_thread::yield();
        }
    }   
}

// ========================================
// 워커 스레드
// ========================================

void JobSystem::worker_main([[maybe_unused]] std::size_t worker_id) {
    while (running_.load(std::memory_order_relaxed)) {
        Job* job = try_get_job(); 

        if (job) {
            execute(job);
            finish(job);
        } else {
            wait_for_job();
        }
    }
}

Job* JobSystem::try_get_job() {
    Job* job = nullptr;
    if (job_queue_.pop(job)) {
        return job;   // 성공적으로 꺼냄
    }
    return nullptr; // 큐가 비었음
}

void JobSystem::execute(Job* job) {
    if (job && job->function) {
        job->function();  // 사용자가 등록한 함수 실행!
    }
}

void JobSystem::finish(Job* job) {
    if (job->counter) {
        job->counter->decrement();
    }

    std::int32_t prev = job->unfinished_jobs.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) { // 이제 0이 됨 = 왼전히 완료
        Job* parent = job->parent;
        job_pool_.destroy(job); // delete
        pending_jobs_.fetch_sub(1, std::memory_order_relaxed);
        if (parent) {
            finish(parent); // 재귀 호출
        }
    }
}

void JobSystem::wait_for_job() {
    std::this_thread::yield();
}

} // namespace lockfree
