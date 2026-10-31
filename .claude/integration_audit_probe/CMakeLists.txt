cmake_minimum_required(VERSION 3.20)
project(integration_audit_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(integration_audit_probe main.cpp
  "D:/remote/remote/apps/native_poc/src/input_macro.cpp"
  "D:/remote/remote/apps/native_poc/src/macro_shell_bridge.cpp")
target_include_directories(integration_audit_probe PRIVATE "D:/remote/remote/apps/native_poc/src")
