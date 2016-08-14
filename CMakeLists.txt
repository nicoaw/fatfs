cmake_minimum_required(VERSION 2.6)

project(FATFS C)

add_definitions(-DFFS_DEBUG)
add_subdirectory(src)
