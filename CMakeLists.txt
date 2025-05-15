cmake_minimum_required(VERSION 3.10)
project(TestProject VERSION 1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加源文件
add_subdirectory(src)
