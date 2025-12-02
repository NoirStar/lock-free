# GoogleTest 설정 (FetchContent 사용)
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Windows에서 gtest_force_shared_crt 설정
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# 테스트 실행 파일 생성 함수
function(add_lockfree_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE
        lockfree
        GTest::gtest_main
    )
    gtest_discover_tests(${TEST_NAME})
endfunction()

# 테스트 파일들 (구현 시 주석 해제)
# add_lockfree_test(test_spsc_queue)
# add_lockfree_test(test_mpsc_queue)
# add_lockfree_test(test_mpmc_queue)
# add_lockfree_test(test_aba_detection)
