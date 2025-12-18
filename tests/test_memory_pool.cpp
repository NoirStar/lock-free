/**
 * Memory Pool 테스트
 * 
 * 범용 Lock-Free 메모리 풀 테스트
 */

#include <gtest/gtest.h>
#include <lockfree/memory_pool.hpp>
#include <thread>
#include <vector>
#include <array>
#include <set>
#include <atomic>
#include <chrono>
#include <random>

using namespace lockfree;

// ========================================
// 테스트 1: 기본 구조 테스트
// ========================================

TEST(MemoryPool, CapacityCheck) {
    MemoryPool<int> pool(128);
    
    EXPECT_EQ(pool.capacity(), 128);
    EXPECT_EQ(pool.allocated_count(), 0);
    EXPECT_EQ(pool.available_count(), 128);
}

TEST(MemoryPool, IsLockFree) {
    EXPECT_TRUE(MemoryPool<int>::is_lock_free());
}

TEST(MemoryPool, BlockSize) {
    // int는 4바이트, 포인터는 8바이트 (64비트)
    // BLOCK_SIZE = max(sizeof(int), sizeof(void*)) = 8
    EXPECT_GE(MemoryPool<int>::block_size(), sizeof(int));
    EXPECT_GE(MemoryPool<int>::block_size(), sizeof(void*));
}

// ========================================
// 테스트 2: 기본 할당/해제
// ========================================

TEST(MemoryPool, BasicAllocate) {
    MemoryPool<int> pool(64);
    
    int* ptr = pool.allocate();
    
    ASSERT_NE(ptr, nullptr) << "Allocation should succeed";
    EXPECT_EQ(pool.allocated_count(), 1);
    
    // 데이터 쓰기/읽기
    *ptr = 42;
    EXPECT_EQ(*ptr, 42);
    
    pool.deallocate(ptr);
    EXPECT_EQ(pool.allocated_count(), 0);
}

TEST(MemoryPool, BasicDeallocate) {
    MemoryPool<int> pool(64);
    
    int* ptr1 = pool.allocate();
    ASSERT_NE(ptr1, nullptr);
    
    pool.deallocate(ptr1);
    
    int* ptr2 = pool.allocate();
    ASSERT_NE(ptr2, nullptr);
    
    // Free List가 LIFO이므로 같은 주소 예상
    EXPECT_EQ(ptr1, ptr2) << "Deallocated block should be reused (LIFO)";
    
    pool.deallocate(ptr2);
}

TEST(MemoryPool, MultipleAllocations) {
    constexpr std::size_t POOL_SIZE = 16;
    MemoryPool<int> pool(POOL_SIZE, false);  // 확장 불가
    
    std::set<int*> allocated;
    
    for (std::size_t i = 0; i < POOL_SIZE; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr) << "Allocation " << i << " should succeed";
        
        // 중복 주소 없어야 함
        EXPECT_EQ(allocated.count(ptr), 0) << "Duplicate address!";
        allocated.insert(ptr);
    }
    
    EXPECT_EQ(pool.allocated_count(), POOL_SIZE);
    
    // 정리
    for (int* ptr : allocated) {
        pool.deallocate(ptr);
    }
    
    EXPECT_EQ(pool.allocated_count(), 0);
}

// ========================================
// 테스트 3: 풀 고갈 및 동적 확장
// ========================================

TEST(MemoryPool, FixedPoolExhaustion) {
    constexpr std::size_t POOL_SIZE = 8;
    MemoryPool<int> pool(POOL_SIZE, false);  // growable = false
    
    EXPECT_FALSE(pool.is_growable());
    
    std::vector<int*> allocated;
    
    // 풀 크기만큼 할당
    for (std::size_t i = 0; i < POOL_SIZE; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        allocated.push_back(ptr);
    }
    
    // 추가 할당 → 실패
    int* overflow = pool.allocate();
    EXPECT_EQ(overflow, nullptr) << "Fixed pool should return nullptr when exhausted";
    
    // 하나 해제 후 다시 할당 가능
    pool.deallocate(allocated.back());
    allocated.pop_back();
    
    int* reclaimed = pool.allocate();
    EXPECT_NE(reclaimed, nullptr);
    allocated.push_back(reclaimed);
    
    // 정리
    for (int* ptr : allocated) {
        pool.deallocate(ptr);
    }
}

TEST(MemoryPool, GrowablePoolExpansion) {
    constexpr std::size_t INITIAL_SIZE = 4;
    MemoryPool<int> pool(INITIAL_SIZE, true);  // growable = true
    
    EXPECT_TRUE(pool.is_growable());
    EXPECT_EQ(pool.capacity(), INITIAL_SIZE);
    EXPECT_EQ(pool.chunk_count(), 1);
    
    std::vector<int*> allocated;
    
    // 초기 용량 이상 할당 → 자동 확장
    for (std::size_t i = 0; i < INITIAL_SIZE * 3; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr) << "Growable pool should expand automatically";
        *ptr = static_cast<int>(i);
        allocated.push_back(ptr);
    }
    
    // 청크 추가 확인
    EXPECT_GT(pool.chunk_count(), 1);
    
    // 데이터 검증
    for (std::size_t i = 0; i < allocated.size(); ++i) {
        EXPECT_EQ(*allocated[i], static_cast<int>(i));
    }
    
    // 정리
    for (int* ptr : allocated) {
        pool.deallocate(ptr);
    }
}

// ========================================
// 테스트 4: Construct / Destroy
// ========================================

struct TestObject {
    int value;
    std::string name;
    
    static std::atomic<int> construct_count;
    static std::atomic<int> destruct_count;
    
    TestObject() : value(0), name("default") {
        construct_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    TestObject(int v, const std::string& n) : value(v), name(n) {
        construct_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    ~TestObject() {
        destruct_count.fetch_add(1, std::memory_order_relaxed);
    }
};

std::atomic<int> TestObject::construct_count{0};
std::atomic<int> TestObject::destruct_count{0};

TEST(MemoryPool, ConstructAndDestroy) {
    TestObject::construct_count = 0;
    TestObject::destruct_count = 0;
    
    MemoryPool<TestObject> pool(16);
    
    // construct: 할당 + 생성자
    TestObject* obj = pool.construct(42, "hello");
    
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->value, 42);
    EXPECT_EQ(obj->name, "hello");
    EXPECT_EQ(TestObject::construct_count.load(), 1);
    
    // destroy: 소멸자 + 해제
    pool.destroy(obj);
    
    EXPECT_EQ(TestObject::destruct_count.load(), 1);
    EXPECT_EQ(pool.allocated_count(), 0);
}

TEST(MemoryPool, ConstructDefault) {
    TestObject::construct_count = 0;
    TestObject::destruct_count = 0;
    
    MemoryPool<TestObject> pool(16);
    
    TestObject* obj = pool.construct();  // 기본 생성자
    
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->value, 0);
    EXPECT_EQ(obj->name, "default");
    
    pool.destroy(obj);
}

// ========================================
// 테스트 5: 멀티스레드 동시 할당/해제
// ========================================

TEST(MemoryPool, ConcurrentAllocateDeallocate) {
    constexpr std::size_t POOL_SIZE = 256;
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 10000;
    
    MemoryPool<int> pool(POOL_SIZE, true);
    
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    
    auto worker = [&]() {
        std::vector<int*> local_allocated;
        local_allocated.reserve(64);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 2);
        
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            if (local_allocated.size() < 32 && dis(gen) != 0) {
                // 할당 시도
                int* ptr = pool.allocate();
                if (ptr) {
                    *ptr = i;
                    local_allocated.push_back(ptr);
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    fail_count.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (!local_allocated.empty()) {
                // 해제
                pool.deallocate(local_allocated.back());
                local_allocated.pop_back();
            }
        }
        
        // 남은 것 모두 해제
        for (int* ptr : local_allocated) {
            pool.deallocate(ptr);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(pool.allocated_count(), 0) << "All allocations should be freed";
    
    std::cout << "[  INFO    ] Successful allocations: " << success_count.load() << "\n";
    std::cout << "[  INFO    ] Pool final capacity: " << pool.capacity() << "\n";
}

// ========================================
// 테스트 6: 데이터 무결성 테스트
// ========================================

TEST(MemoryPool, DataIntegrityStressTest) {
    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 20000;
    
    MemoryPool<std::uint64_t> pool(128, true);
    
    std::atomic<int> error_count{0};
    
    auto worker = [&](int thread_id) {
        const std::uint64_t MAGIC = 0xDEADBEEF00000000ULL | thread_id;
        
        for (int i = 0; i < ITERATIONS; ++i) {
            std::uint64_t* ptr = pool.allocate();
            if (ptr) {
                // 고유 값 쓰기
                *ptr = MAGIC + i;
                
                // 검증
                if (*ptr != MAGIC + i) {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
                
                pool.deallocate(ptr);
            }
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(error_count.load(), 0) << "Data corruption detected!";
}

// ========================================
// 테스트 7: 대용량 객체 테스트
// ========================================

struct LargeObject {
    std::array<char, 256> data;
    int id;
    
    LargeObject(int i = 0) : id(i) {
        data.fill(static_cast<char>(i & 0xFF));
    }
    
    bool verify() const {
        char expected = static_cast<char>(id & 0xFF);
        for (char c : data) {
            if (c != expected) return false;
        }
        return true;
    }
};

TEST(MemoryPool, LargeObjectPool) {
    MemoryPool<LargeObject> pool(64);
    
    std::vector<LargeObject*> allocated;
    
    for (int i = 0; i < 32; ++i) {
        LargeObject* obj = pool.construct(i);
        ASSERT_NE(obj, nullptr);
        EXPECT_EQ(obj->id, i);
        EXPECT_TRUE(obj->verify());
        allocated.push_back(obj);
    }
    
    for (LargeObject* obj : allocated) {
        pool.destroy(obj);
    }
    
    EXPECT_EQ(pool.allocated_count(), 0);
}

// ========================================
// 테스트 8: FixedMemoryPool 별칭
// ========================================

TEST(MemoryPool, FixedMemoryPoolAlias) {
    FixedMemoryPool<int> pool(32);
    
    EXPECT_FALSE(pool.is_growable());
    EXPECT_EQ(pool.capacity(), 32);
    
    std::vector<int*> allocated;
    
    for (int i = 0; i < 32; ++i) {
        int* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        allocated.push_back(ptr);
    }
    
    EXPECT_EQ(pool.allocate(), nullptr);  // 고갈
    
    for (int* ptr : allocated) {
        pool.deallocate(ptr);
    }
}

// ========================================
// 벤치마크 (선택적 실행)
// ========================================

TEST(MemoryPool, DISABLED_PerformanceBenchmark) {
    constexpr int ITERATIONS = 1000000;
    
    // Memory Pool
    {
        MemoryPool<int> pool(4096, false);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ITERATIONS; ++i) {
            int* ptr = pool.allocate();
            if (ptr) {
                *ptr = i;
                pool.deallocate(ptr);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        
        std::cout << "[  BENCH   ] MemoryPool: " << ns / ITERATIONS << " ns/op\n";
    }
    
    // new/delete 비교
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ITERATIONS; ++i) {
            int* ptr = new int;
            *ptr = i;
            delete ptr;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        
        std::cout << "[  BENCH   ] new/delete: " << ns / ITERATIONS << " ns/op\n";
    }
}
