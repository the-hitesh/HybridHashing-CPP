cmake_minimum_required(VERSION 3.10)
project(HybridHashing)

# Set C++ standard to 17 for modern features like std::optional and threading
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories for headers
include_directories(include)

# Add executable with source files
add_executable(hybrid_hash
    src/main.cpp
    src/HybridHashTable.cpp
    src/HashFunctions.cpp
)

# Link necessary libraries (standard C++ libraries are included by default;
# for threading, we use std::thread, etc., which are in the standard library)
# If needed later, add external libs like -lpthread for POSIX threads, but C++17 handles most
target_link_libraries(hybrid_hash)