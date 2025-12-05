# Examples 빌드 설정

add_executable(false_sharing_benchmark false_sharing_benchmark.cpp)
target_compile_features(false_sharing_benchmark PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(false_sharing_benchmark PRIVATE /W4)
else()
    target_compile_options(false_sharing_benchmark PRIVATE -Wall -Wextra -pthread)
    target_link_options(false_sharing_benchmark PRIVATE -pthread)
endif()
