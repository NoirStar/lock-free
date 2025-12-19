# GoogleTest 설정 (vcpkg 사용)
find_package(GTest CONFIG REQUIRED)

# 테스트 실행 파일 생성 함수
function(add_lockfree_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE
        lockfree
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# 테스트 파일들
add_lockfree_test(test_spsc_queue)
add_lockfree_test(test_mpsc_queue)
add_lockfree_test(test_mpmc_queue)
add_lockfree_test(test_spinlock)
add_lockfree_test(test_aba_problem)
add_lockfree_test(test_aba_safe_stack)
add_lockfree_test(test_memory_pool)

# job_system은 cpp 파일이 있으므로 별도 처리
add_executable(test_job_system test_job_system.cpp ${CMAKE_SOURCE_DIR}/src/job_system.cpp)
target_link_libraries(test_job_system PRIVATE
    lockfree
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_job_system COMMAND test_job_system)
# add_lockfree_test(test_aba_detection)
