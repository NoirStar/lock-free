cmake_minimum_required(VERSION 3.20)
project(lockfree-containers
    VERSION 0.1.0
    DESCRIPTION "Modern C++20 Lock-Free Concurrency Container Package"
    LANGUAGES CXX
)

# C++20 표준 설정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 컴파일러 경고 옵션
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 헤더 전용 라이브러리 (인터페이스 라이브러리)
add_library(lockfree INTERFACE)
target_include_directories(lockfree INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 테스트 활성화 옵션
option(LOCKFREE_BUILD_TESTS "Build tests" ON)

if(LOCKFREE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 예제 빌드 옵션
option(LOCKFREE_BUILD_EXAMPLES "Build examples" ON)

if(LOCKFREE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 설치 설정
include(GNUInstallDirs)
install(TARGETS lockfree
    EXPORT lockfree-targets
)
install(DIRECTORY include/lockfree
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
