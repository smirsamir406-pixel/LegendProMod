cmake_minimum_required(VERSION 3.18)
project(client CXX)
set(CMAKE_CXX_STANDARD 17)

set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Dobby EXCLUDE_FROM_ALL)

add_library(client SHARED main.cpp)
target_include_directories(client PRIVATE third_party/Dobby/include .)
target_compile_options(client PRIVATE -fno-exceptions -fno-rtti -fvisibility=hidden -Os)
target_link_libraries(client PRIVATE dobby log dl)
