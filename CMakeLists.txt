cmake_minimum_required(VERSION 3.16)
project(MiniNet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mininet
    src/main.cpp
    src/http_server.cpp
    src/thread_pool.cpp
)

target_include_directories(mininet PRIVATE include)
target_compile_options(mininet PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(mininet PRIVATE Threads::Threads)
