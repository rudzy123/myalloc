cmake_minimum_required(VERSION 3.5.1)
project(myalloc C)

set(HEADER_FILES myalloc.h)
set(SOURCE_FILES alligator.c myalloc.c)
add_executable(alligator ${SOURCE_FILES})
