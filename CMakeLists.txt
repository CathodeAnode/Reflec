cmake_minimum_required(VERSION 3.30)
project(Reflec VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "Reflec requires GCC 16+ with C++26 reflection (-freflection). Configure with -DCMAKE_CXX_COMPILER=g++.")
endif()
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Reflec)
target_sources(Reflec PUBLIC FILE_SET CXX_MODULES FILES src/Reflec.cppm)
target_compile_options(Reflec PUBLIC -freflection)
target_compile_options(Reflec PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ReflecExample Examples/Main.cpp)
target_link_libraries(ReflecExample PRIVATE Reflec)
