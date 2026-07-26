cmake_minimum_required(VERSION 4.2)
project(keyboard_assault VERSION 1.3.2 LANGUAGES CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(windows)
else()
    add_subdirectory(linux)
endif()