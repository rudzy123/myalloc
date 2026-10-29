cmake_minimum_required(VERSION 3.5.1)
project(myalloc C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(HEADER_FILES myalloc.h)
set(SOURCE_FILES alligator.c myalloc.c)
add_executable(alligator ${SOURCE_FILES})
