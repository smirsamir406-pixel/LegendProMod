cmake_minimum_required(VERSION 3.18)
project(client CXX)
set(CMAKE_CXX_STANDARD 17)

add_library(client SHARED main.cpp)
target_compile_options(client PRIVATE -fno-exceptions -fno-rtti -fvisibility=hidden -Os)
target_link_libraries(client PRIVATE log dl)
