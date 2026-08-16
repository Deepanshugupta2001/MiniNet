cmake_minimum_required(VERSION 3.16)
project(MiniNet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MININET_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MININET_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

add_executable(mininet
    src/main.cpp
    src/http.cpp
    src/http_server_v2.cpp
    src/thread_pool.cpp
)

target_include_directories(mininet PRIVATE include)
target_compile_options(mininet PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(mininet PRIVATE Threads::Threads)

function(enable_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if(MININET_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
        if(MININET_ENABLE_UBSAN)
            target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endif()
    endif()
endfunction()

enable_sanitizers(mininet)

include(CTest)
if(BUILD_TESTING)
    add_executable(mininet_tests
        tests/http_tests.cpp
        src/http.cpp
        src/http_server_v2.cpp
        src/thread_pool.cpp
    )
    target_include_directories(mininet_tests PRIVATE include)
    target_compile_options(mininet_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(mininet_tests PRIVATE Threads::Threads)
    enable_sanitizers(mininet_tests)
    add_test(NAME mininet_tests COMMAND mininet_tests)
endif()
