cmake_minimum_required(VERSION 3.22.1)

project("fusefixer")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--exclude-libs,ALL -Wl,-z,start-stop-visibility=hidden -Wl,--gc-sections")

add_library(${CMAKE_PROJECT_NAME} SHARED fusefixer.cpp utils.c)

target_link_libraries(${CMAKE_PROJECT_NAME}
    icu log elf_parser maps_scan)

include_directories(include)
add_subdirectory(elf_parser)
add_subdirectory(maps_scan)

