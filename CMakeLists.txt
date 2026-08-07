cmake_minimum_required(VERSION 3.20)

project(nif-port-cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(
  "${CMAKE_CURRENT_SOURCE_DIR}/external/nifly"
  "${CMAKE_CURRENT_BINARY_DIR}/nifly"
)

add_executable(nif-port-cli main.cpp)
target_link_libraries(nif-port-cli PRIVATE nifly)
