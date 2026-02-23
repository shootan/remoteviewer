cmake_minimum_required(VERSION 3.20)
project(remote60 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(libs/common)
add_subdirectory(apps/host)
add_subdirectory(apps/client)
add_subdirectory(apps/native_poc)
