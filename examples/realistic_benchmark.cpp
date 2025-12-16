/**
 * Realistic Queue Benchmark
 * 
 * Simulates REAL workload:
 * - Producer does some work, then pushes
 * - Consumer pops, then does some work
 * - This creates realistic contention patterns
 */

#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <latch>
#include <random>
#include "lockfree/mpmc_queue.hpp"

using namespace std::chrono;
using Clock = high_resolution_clock;

constexpr size_t QUEUE_CAPACITY = 4096;

// ============================================================================
// Mutex Queue
// ============================================================================
template<typename T, size_t Capacity>
class MutexQueue {
public:
    bool push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= Capacity) return false;
        queue_.push(value);
        return true;
    }
    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        value = queue_.front();
        queue_.pop();
        return true;
    }
private:
    std::mutex mutex_;
    std::queue<T> queue_;
};

// ============================================================================
// Simulate work (CPU-bound task)
// ============================================================================
volatile int sink = 0;  // Prevent optimization

void simulate_work(int iterations) {
    int x = 0;
    for (int i = 0; i < iterations; ++i) {
        x += i * i;  // Some computation
    }
    sink = x;
}

// ============================================================================
// Benchmark with realistic work simulation
// ============================================================================
template<typename Queue>
struct BenchResult {
    double throughput;      // ops/sec
    double avg_latency_ns;  // average latency
    double p99_latency_ns;  // 99th percentile latency
};

template<typename Queue>
BenchResult<Queue> run_realistic_benchmark(
    int num_producers, 
    int num_consumers, 
    int ops_per_producer,
    int work_iterations  // Simulated work per operation
) {
    Queue queue;
    
    const int total_threads = num_producers + num_consumers;
    std::latch start_latch(total_threads + 1);
    std::latch end_latch(total_threads);
    
    std::vector<long long> all_latencies;
    std::mutex latency_mutex;
    
    std::vector<std::thread> threads;
    
    // Producers
    for (int p = 0; p < num_producers; ++p) {
        threads.emplace_back([&, p, ops_per_producer, work_iterations]() {
            std::vector<long long> local_latencies;
            local_latencies.reserve(ops_per_producer);
            
            start_latch.arrive_and_wait();
            
            for (int i = 0; i < ops_per_producer; ++i) {
                // Simulate producing work
                simulate_work(work_iterations);
                
                // Measure push latency
                auto t1 = Clock::now();
                while (!queue.push(p * ops_per_producer + i)) {
                    // Brief pause instead of tight spin
                    std::this_thread::yield();
                }
                auto t2 = Clock::now();
                local_latencies.push_back(duration_cast<nanoseconds>(t2 - t1).count());
            }
            
            {
                std::lock_guard<std::mutex> lock(latency_mutex);
                all_latencies.insert(all_latencies.end(), 
                    local_latencies.begin(), local_latencies.end());
            }
            
            end_latch.count_down();
        });
    }
    
    // Consumers
    int ops_per_consumer = (num_producers * ops_per_producer) / num_consumers;
    for (int c = 0; c < num_consumers; ++c) {
        threads.emplace_back([&, ops_per_consumer, work_iterations]() {
            std::vector<long long> local_latencies;
            local_latencies.reserve(ops_per_consumer);
            
            start_latch.arrive_and_wait();
            
            int value;
            for (int i = 0; i < ops_per_consumer; ++i) {
                // Measure pop latency
                auto t1 = Clock::now();
                while (!queue.pop(value)) {
                    std::this_thread::yield();
                }
                auto t2 = Clock::now();
                local_latencies.push_back(duration_cast<nanoseconds>(t2 - t1).count());
                
                // Simulate consuming/processing work
                simulate_work(work_iterations);
            }
            
            {
                std::lock_guard<std::mutex> lock(latency_mutex);
                all_latencies.insert(all_latencies.end(), 
                    local_latencies.begin(), local_latencies.end());
            }
            
            end_latch.count_down();
        });
    }
    
    auto start = Clock::now();
    start_latch.arrive_and_wait();
    end_latch.wait();
    auto end = Clock::now();
    
    for (auto& t : threads) t.join();
    
    // Calculate stats
    double elapsed_sec = duration_cast<microseconds>(end - start).count() / 1000000.0;
    int total_ops = num_producers * ops_per_producer * 2;
    double throughput = total_ops / elapsed_sec;
    
    // Sort for percentile calculation
    std::sort(all_latencies.begin(), all_latencies.end());
    
    double avg_latency = 0;
    for (auto l : all_latencies) avg_latency += l;
    avg_latency /= all_latencies.size();
    
    size_t p99_idx = static_cast<size_t>(all_latencies.size() * 0.99);
    double p99_latency = static_cast<double>(all_latencies[p99_idx]);
    
    return {throughput, avg_latency, p99_latency};
}

void print_bar(double value, double max_value, int width = 25) {
    int filled = static_cast<int>((value / max_value) * width);
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        std::cout << (i < filled ? "#" : " ");
    }
    std::cout << "]";
}

int main() {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "       Realistic Queue Benchmark (with simulated work)\n";
    std::cout << "================================================================\n";
    std::cout << "  Each operation includes simulated CPU work\n";
    std::cout << "  This represents real-world usage patterns\n";
    std::cout << "================================================================\n\n";

    struct TestCase {
        int producers;
        int consumers;
        int ops_per_producer;
        int work_iterations;
        const char* name;
    };
    
    std::vector<TestCase> tests = {
        // Light work (fast producer/consumer)
        {4, 4, 50000, 100, "4P-4C (Light work)"},
        {8, 8, 25000, 100, "8P-8C (Light work)"},
        
        // Medium work (typical)
        {4, 4, 50000, 500, "4P-4C (Medium work)"},
        {8, 8, 25000, 500, "8P-8C (Medium work)"},
        
        // Heavy work (slow producer/consumer)
        {4, 4, 25000, 2000, "4P-4C (Heavy work)"},
        {8, 8, 12500, 2000, "8P-8C (Heavy work)"},
    };
    
    std::cout << "+----------------------+------------+------------+------------+------------+\n";
    std::cout << "|      Scenario        | Lock-Free  |   Mutex    |   Ratio    |   Winner   |\n";
    std::cout << "|                      | (M ops/s)  | (M ops/s)  |            |            |\n";
    std::cout << "+----------------------+------------+------------+------------+------------+\n";
    
    for (const auto& test : tests) {
        auto lf = run_realistic_benchmark<lockfree::MPMCQueue<int, QUEUE_CAPACITY>>(
            test.producers, test.consumers, test.ops_per_producer, test.work_iterations);
        auto mx = run_realistic_benchmark<MutexQueue<int, QUEUE_CAPACITY>>(
            test.producers, test.consumers, test.ops_per_producer, test.work_iterations);
        
        double ratio = lf.throughput / mx.throughput;
        const char* winner = ratio >= 1.0 ? "Lock-Free" : "Mutex";
        
        std::cout << "| " << std::left << std::setw(20) << test.name << " |"
                  << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
                  << (lf.throughput / 1000000.0) << "  |"
                  << std::setw(10) << (mx.throughput / 1000000.0) << "  |"
                  << std::setw(10) << ratio << "x |"
                  << std::setw(11) << winner << " |\n";
    }
    
    std::cout << "+----------------------+------------+------------+------------+------------+\n\n";

    // Latency comparison
    std::cout << "================================================================\n";
    std::cout << "       Latency Comparison (8P-8C, Medium work)\n";
    std::cout << "================================================================\n\n";
    
    auto lf = run_realistic_benchmark<lockfree::MPMCQueue<int, QUEUE_CAPACITY>>(8, 8, 25000, 500);
    auto mx = run_realistic_benchmark<MutexQueue<int, QUEUE_CAPACITY>>(8, 8, 25000, 500);
    
    std::cout << "                    Lock-Free          Mutex\n";
    std::cout << "  Avg Latency:    " << std::setw(10) << std::fixed << std::setprecision(0) 
              << lf.avg_latency_ns << " ns    " << std::setw(10) << mx.avg_latency_ns << " ns\n";
    std::cout << "  P99 Latency:    " << std::setw(10) << lf.p99_latency_ns << " ns    " 
              << std::setw(10) << mx.p99_latency_ns << " ns\n\n";
    
    if (lf.p99_latency_ns < mx.p99_latency_ns) {
        std::cout << "  => Lock-Free has " << std::setprecision(1) 
                  << (mx.p99_latency_ns / lf.p99_latency_ns) << "x better P99 latency!\n";
    } else {
        std::cout << "  => Mutex has " << std::setprecision(1) 
                  << (lf.p99_latency_ns / mx.p99_latency_ns) << "x better P99 latency\n";
    }
    
    std::cout << "\n================================================================\n";
    std::cout << "                    Benchmark Complete\n";
    std::cout << "================================================================\n\n";
    
    return 0;
}
