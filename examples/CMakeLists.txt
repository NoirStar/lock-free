# Examples

add_executable(false_sharing_benchmark false_sharing_benchmark.cpp)
target_compile_features(false_sharing_benchmark PRIVATE cxx_std_20)

# add_executable(queue_fair_benchmark queue_fair_benchmark.cpp)
# target_compile_features(queue_fair_benchmark PRIVATE cxx_std_20)
# target_link_libraries(queue_fair_benchmark PRIVATE lockfree)

add_executable(realistic_benchmark realistic_benchmark.cpp)
target_compile_features(realistic_benchmark PRIVATE cxx_std_20)
target_link_libraries(realistic_benchmark PRIVATE lockfree)

if(MSVC)
    target_compile_options(false_sharing_benchmark PRIVATE /W4)
    # target_compile_options(queue_fair_benchmark PRIVATE /W4 /O2)
    target_compile_options(realistic_benchmark PRIVATE /W4 /O2)
else()
    target_compile_options(false_sharing_benchmark PRIVATE -Wall -Wextra -pthread)
    target_link_options(false_sharing_benchmark PRIVATE -pthread)
    # target_compile_options(queue_fair_benchmark PRIVATE -Wall -Wextra -O3 -pthread)
    # target_link_options(queue_fair_benchmark PRIVATE -pthread)
    target_compile_options(realistic_benchmark PRIVATE -Wall -Wextra -O3 -pthread)
    target_link_options(realistic_benchmark PRIVATE -pthread)
endif()
