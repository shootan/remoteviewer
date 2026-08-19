cmake_minimum_required(VERSION 3.20)
# Honour MSVC_RUNTIME_LIBRARY instead of baking /MD into the default flags.
cmake_policy(SET CMP0091 NEW)
project(remote60 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C runtime statically so an installed product runs on a machine that has never had
# the Visual C++ redistributable. With the dynamic runtime every executable here imports
# MSVCP140/VCRUNTIME140 and fails in the loader -- before reaching main, so it cannot even
# report why. Every library in this tree is static, so there is no cross-module CRT to share.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(libs/common)
add_subdirectory(libs/capture)
add_subdirectory(apps/native_poc)
