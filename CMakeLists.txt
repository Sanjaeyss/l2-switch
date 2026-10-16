cmake_minimum_required(VERSION 3.20)
project(l2-switch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Core library -----------------------------------------------------------
add_library(l2sw
    src/ethernet.cpp
    src/mac_table.cpp
)
target_include_directories(l2sw PUBLIC include)
target_compile_options(l2sw PRIVATE -Wall -Wextra -Wpedantic -Werror)

# ---- Unit tests (GoogleTest) ------------------------------------------------
option(L2SW_BUILD_TESTS "Build unit tests" ON)

if(L2SW_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(l2sw_tests
        tests/test_ethernet.cpp
        tests/test_mac_table.cpp
    )
    target_link_libraries(l2sw_tests PRIVATE l2sw GTest::gtest_main)
    target_compile_options(l2sw_tests PRIVATE -Wall -Wextra)

    include(GoogleTest)
    gtest_discover_tests(l2sw_tests)
endif()
