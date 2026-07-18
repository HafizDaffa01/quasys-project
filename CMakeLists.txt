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

# --- Dependencies (local clones in deps/) ---

# ASIO (standalone, used by Crow)
set(ASIO_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/asio/asio/include" CACHE PATH "ASIO headers")

find_package(Threads REQUIRED)

# Create asio::asio interface target manually
add_library(asio::asio INTERFACE IMPORTED)
target_include_directories(asio::asio INTERFACE ${ASIO_INCLUDE_DIR})
target_compile_definitions(asio::asio INTERFACE ASIO_STANDALONE)
target_link_libraries(asio::asio INTERFACE Threads::Threads)

# Crow
add_library(Crow INTERFACE)
add_library(Crow::Crow ALIAS Crow)
target_include_directories(Crow INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/crow/include>
)
target_link_libraries(Crow INTERFACE asio::asio)

# --- Main executable ---
add_executable(quasys src/main.cpp src/util.cpp)
target_link_libraries(quasys PRIVATE Crow::Crow)

# Copy web/ to build dir so static files are served from there
add_custom_command(TARGET quasys POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/web $<TARGET_FILE_DIR:quasys>/web
)
