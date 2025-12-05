/**
 * False Sharing Benchmark
 * 
 * Measures performance difference between:
 * - No padding (False Sharing occurs)
 * - With padding (False Sharing prevented)
 */

#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

// Number of iterations
constexpr int ITERATIONS = 100'000'000;

// ============================================
// Case 1: False Sharing (No Padding)
// ============================================
struct NoPadding {
    std::atomic<int> a{0};  // 4 bytes
    std::atomic<int> b{0};  // 4 bytes
    std::atomic<int> c{0};  // 4 bytes
    std::atomic<int> d{0};  // 4 bytes
    // Total 16 bytes - all in same cache line (64 bytes)!
};

// ============================================
// Case 2: No False Sharing (64-byte Padding)
// ============================================
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

struct alignas(64) PaddedCounter {
    std::atomic<int> value{0};
    // alignas(64) ensures each instance is on separate cache line
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct WithPadding {
    PaddedCounter a;
    PaddedCounter b;
    PaddedCounter c;
    PaddedCounter d;
    // Each on separate cache line -> No False Sharing
};

// ============================================
// Benchmark Functions
// ============================================

void increment_no_padding(std::atomic<int>& counter) {
    for (int i = 0; i < ITERATIONS; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

void increment_with_padding(PaddedCounter& counter) {
    for (int i = 0; i < ITERATIONS; ++i) {
        counter.value.fetch_add(1, std::memory_order_relaxed);
    }
}

template<typename Func>
double measure_time(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   False Sharing Benchmark\n";
    std::cout << "========================================\n\n";

    // Print struct sizes
    std::cout << "[Struct Sizes]\n";
    std::cout << "  NoPadding size:     " << sizeof(NoPadding) << " bytes\n";
    std::cout << "  WithPadding size:   " << sizeof(WithPadding) << " bytes\n";
    std::cout << "  PaddedCounter size: " << sizeof(PaddedCounter) << " bytes\n";
    std::cout << "  Cache line size:    64 bytes (typical)\n\n";

    const int num_threads = 4;
    std::cout << "[Test Configuration]\n";
    std::cout << "  Threads:    " << num_threads << "\n";
    std::cout << "  Iterations: " << ITERATIONS << " per thread\n\n";

    // ============================================
    // Test 1: With False Sharing
    // ============================================
    std::cout << "[Test 1] FALSE SHARING (No Padding)\n";
    std::cout << "  - 4 atomic<int> in same cache line\n";
    std::cout << "  - Each thread modifies different variable\n";
    std::cout << "  - But cache line bounces between CPUs!\n";
    
    NoPadding no_pad;
    double time_no_padding = measure_time([&]() {
        std::vector<std::jthread> threads;
        threads.emplace_back(increment_no_padding, std::ref(no_pad.a));
        threads.emplace_back(increment_no_padding, std::ref(no_pad.b));
        threads.emplace_back(increment_no_padding, std::ref(no_pad.c));
        threads.emplace_back(increment_no_padding, std::ref(no_pad.d));
        // jthread auto-joins
    });
    
    std::cout << "  Result: a=" << no_pad.a << ", b=" << no_pad.b 
              << ", c=" << no_pad.c << ", d=" << no_pad.d << "\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(2) 
              << time_no_padding << " ms\n\n";

    // ============================================
    // Test 2: Without False Sharing
    // ============================================
    std::cout << "[Test 2] NO FALSE SHARING (64-byte Padding)\n";
    std::cout << "  - Each counter on separate cache line\n";
    std::cout << "  - True parallel processing\n";
    
    WithPadding with_pad;
    double time_with_padding = measure_time([&]() {
        std::vector<std::jthread> threads;
        threads.emplace_back(increment_with_padding, std::ref(with_pad.a));
        threads.emplace_back(increment_with_padding, std::ref(with_pad.b));
        threads.emplace_back(increment_with_padding, std::ref(with_pad.c));
        threads.emplace_back(increment_with_padding, std::ref(with_pad.d));
    });
    
    std::cout << "  Result: a=" << with_pad.a.value << ", b=" << with_pad.b.value 
              << ", c=" << with_pad.c.value << ", d=" << with_pad.d.value << "\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(2) 
              << time_with_padding << " ms\n\n";

    // ============================================
    // Results Comparison
    // ============================================
    std::cout << "========================================\n";
    std::cout << "   Results\n";
    std::cout << "========================================\n";
    std::cout << "  No Padding:   " << std::setw(10) << time_no_padding << " ms\n";
    std::cout << "  With Padding: " << std::setw(10) << time_with_padding << " ms\n";
    std::cout << "  ------------------------------------\n";
    
    double speedup = time_no_padding / time_with_padding;
    std::cout << "  Speedup:      " << std::setprecision(2) << speedup << "x\n\n";

    if (speedup > 1.5) {
        std::cout << "  [OK] False Sharing impact clearly measured!\n";
        std::cout << "  -> Padding improves performance by " << speedup << "x\n";
    } else if (speedup > 1.1) {
        std::cout << "  [OK] Some False Sharing impact detected\n";
        std::cout << "  -> Results may vary by CPU/system\n";
    } else {
        std::cout << "  [?] Difference is small on this system\n";
        std::cout << "  -> May be due to CPU cache policy or other factors\n";
    }

    std::cout << "\n[Key Takeaways]\n";
    std::cout << "  1. Different variables in same cache line = performance hit\n";
    std::cout << "  2. Use alignas(64) to place data on separate cache lines\n";
    std::cout << "  3. In lock-free structures, separate head/tail pointers!\n";

    return 0;
}
