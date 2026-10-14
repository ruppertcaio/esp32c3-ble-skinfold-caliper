cmake_minimum_required(VERSION 3.16)
project(caliper_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(test_caliper_math
    test_caliper_math.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/caliper_math.cpp
)
target_include_directories(test_caliper_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../main)

add_test(NAME caliper_math COMMAND test_caliper_math)
