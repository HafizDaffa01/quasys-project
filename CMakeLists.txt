cmake_minimum_required(VERSION 3.20)
project(QuaSYS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Release optimizations (PRD: -march=native -O3)
add_compile_options(-O3 -march=native)

# --- Dependencies ---

find_package(Curses REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

find_library(UUID_LIB uuid)
if(NOT UUID_LIB)
  message(FATAL_ERROR "libuuid not found")
endif()

# --- Main executable ---
add_executable(quasys src/main.cpp src/util.cpp src/gemini.cpp src/claude.cpp)
target_link_libraries(quasys PRIVATE ${CURSES_LIBRARIES} CURL::libcurl Threads::Threads ${UUID_LIB})
target_include_directories(quasys PRIVATE ${CURSES_INCLUDE_DIRS})
