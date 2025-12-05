# GoogleTest 설정 (FetchContent 사용)
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Windows에서 정적 런타임 사용 + GMock 비활성화
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 테스트 실행 파일 생성 함수
function(add_lockfree_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE
        lockfree
        gtest
        gtest_main
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# 테스트 파일들
add_lockfree_test(test_spsc_queue)
# add_lockfree_test(test_mpsc_queue)
# add_lockfree_test(test_mpmc_queue)
# add_lockfree_test(test_aba_detection)
