// SpinLock - Lock-Free Spinlock Implementation
#pragma once

#include <atomic>
#include <thread>

// 플랫폼별 pause 명령어 정의
#if defined(_MSC_VER)
    #include <intrin.h>
    #define SPIN_PAUSE() _mm_pause()
#elif defined(__x86_64__) || defined(__i386__)
    #define SPIN_PAUSE() __builtin_ia32_pause()
#elif defined(__arm__) || defined(__aarch64__)
    #define SPIN_PAUSE() __asm__ __volatile__("yield")
#else
    #define SPIN_PAUSE() ((void)0)
#endif

namespace lockfree {

class SpinLock {
public:
    SpinLock() = default;
    
    // Non-copyable, non-movable
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;
    
    void lock() {
        // ============================================
        // 1단계: Fast Path (빠른 경로)
        // ============================================
        // 경합이 없으면 바로 획득 - 가장 일반적인 케이스
        // exchange는 atomic하게 true로 설정하고 이전 값을 반환
        // 이전 값이 false였다면 = 락이 풀려있었다면 = 획득 성공!
        if (!locked_.exchange(true, std::memory_order_acquire)) {
            return;  // 락 획득 성공, 즉시 반환
        }
        
        // ============================================
        // 2단계: Spin with TTAS (Test-and-Test-and-Set)
        // ============================================
        // 짧은 시간 동안 스핀하면서 락 획득 시도
        // 왜 32번? -> 컨텍스트 스위치 비용보다 작은 시간 동안만 스핀
        // 이 값은 튜닝 가능 (16~64 정도가 일반적)
        for (int spin = 0; spin < spin_count_; ++spin) {
            
            // TTAS의 핵심: 먼저 "읽기만" 수행
            // load는 캐시에서 읽기만 하므로 다른 CPU 캐시를 오염시키지 않음
            // relaxed: 순서 보장 필요 없음, 그냥 현재 값만 읽으면 됨
            if (!locked_.load(std::memory_order_relaxed)) {
                
                // 락이 풀린 것 같다! 실제로 획득 시도
                // 이때만 exchange(쓰기 연산) 수행
                if (!locked_.exchange(true, std::memory_order_acquire)) {
                    return;  // 락 획득 성공!
                }
                // 다른 스레드가 먼저 가져갔다면 다시 스핀
            }
            
            // CPU에게 "나 스핀 중이야"라고 알려줌
            // x86: pause 명령어 - 파이프라인 최적화, 전력 절약
            // ARM: yield 명령어 - 다른 스레드에게 양보
            SPIN_PAUSE();
        }
        
        // ============================================
        // 3단계: OS Wait (운영체제 대기)
        // ============================================
        // 스핀으로도 못 얻었다면 = 경합이 심한 상황
        // CPU 낭비하지 말고 OS에게 대기를 맡김
        lock_slow_path();
    }
    
    bool try_lock() {
        // 딱 한 번만 시도하고 결과 반환
        // 현재 값이 false(잠금 해제)인 경우에만 true로 변경
        bool expected = false;
        return locked_.compare_exchange_strong(
            expected, 
            true, 
            std::memory_order_acquire,   // 성공 시: acquire
            std::memory_order_relaxed    // 실패 시: relaxed (어차피 아무것도 안 함)
        );
    }
    
    void unlock() {
        // 락 해제
        // release: 이 store 이전의 모든 메모리 연산이 완료됨을 보장
        locked_.store(false, std::memory_order_release);
        
        // C++20: 대기 중인 스레드가 있으면 깨움
        // 대기 중인 스레드가 없으면 아무 일도 안 함 (오버헤드 최소)
        locked_.notify_one();
    }
    
private:
    // Slow path: OS 레벨 대기
    // [[gnu::noinline]]이나 __declspec(noinline)로 
    // 이 함수를 인라인하지 않게 하면 fast path가 더 최적화됨
    void lock_slow_path() {
        while (true) {
            // 다시 한 번 획득 시도
            if (!locked_.exchange(true, std::memory_order_acquire)) {
                return;  // 성공!
            }
            
            // C++20 wait(): locked_가 true인 동안 OS 레벨에서 대기
            // 내부적으로 WaitOnAddress(Windows) 또는 futex(Linux) 사용
            // CPU를 소모하지 않고 효율적으로 대기
            // 
            // 작동 방식:
            // 1. 현재 값이 true(첫 번째 인자)와 같으면 대기 상태로 전환
            // 2. notify_one() 호출 시 깨어남
            // 3. spurious wakeup 가능성 있으므로 while 루프 필요
            locked_.wait(true, std::memory_order_relaxed);
        }
    }
    
private:
    // 락 상태: false = 잠금 해제, true = 잠금
    alignas(64) std::atomic<bool> locked_{false};  // 캐시라인 정렬
    
    // 스핀 횟수 (튜닝 가능)
    static constexpr int spin_count_ = 32;
};

// ============================================
// RAII wrapper for SpinLock
// ============================================
// 생성자에서 lock(), 소멸자에서 unlock() 자동 호출
// 예외가 발생해도 unlock()이 보장됨
class SpinLockGuard {
public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~SpinLockGuard() {
        lock_.unlock();
    }
    
    // Non-copyable, non-movable
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
    SpinLockGuard(SpinLockGuard&&) = delete;
    SpinLockGuard& operator=(SpinLockGuard&&) = delete;
    
private:
    SpinLock& lock_;
};

} // namespace lockfree