cmake_minimum_required(VERSION 3.24)

project(brodmabuf VERSION 0.1.0 LANGUAGES C CXX)

# Targets
#   brodmabuf::brodmabuf   DMA-BUF buffers, GBM, Vulkan import/export, DRM sync, KMS scanout
#
# DMA-BUF is a Linux kernel interface. On Linux the whole library builds
# against libdrm, Mesa GBM and the Vulkan loader. Everywhere else only the
# portable part builds (types, the DRM format model, plane layout math) and
# DmaBufAllocator's factories fail with the reason (src/unavailable/).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_brodmabuf_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_brodmabuf_top_level ON)
endif()

option(BRODMABUF_BUILD_TESTS "Build the brodmabuf tests" ${_brodmabuf_top_level})

# Coverage. Scoped to a top-level build: a consumer that add_subdirectory()s
# brodmabuf has no business having its own build instrumented from in here.
option(BRODMABUF_COVERAGE "Instrument the build for gcov coverage (GCC/Clang)" OFF)
if(BRODMABUF_COVERAGE AND _brodmabuf_top_level)
    if(MSVC)
        message(FATAL_ERROR "BRODMABUF_COVERAGE requires GCC or Clang (gcov instrumentation)")
    endif()
    add_compile_options(--coverage -fprofile-update=atomic -O0 -g)
    add_link_options(--coverage)
    message(STATUS "brodmabuf: coverage instrumentation ON (-O0)")
endif()

if(MSVC AND _brodmabuf_top_level)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(brodmabuf_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(BRODMABUF_SOURCES
    src/types.cpp
    src/formats.cpp
    src/buffer.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BRODMABUF_LINUX ON)
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    find_package(Vulkan REQUIRED)
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
    list(APPEND BRODMABUF_SOURCES
        src/gbm.cpp
        src/sync.cpp
        src/vulkan.cpp
        src/kms.cpp
        src/allocator.cpp)
else()
    set(BRODMABUF_LINUX OFF)
    list(APPEND BRODMABUF_SOURCES src/unavailable/allocator.cpp)
    message(STATUS "brodmabuf: not Linux, building the portable format model only; DMA-BUF reports itself unavailable")
endif()

add_library(brodmabuf STATIC ${BRODMABUF_SOURCES})
add_library(brodmabuf::brodmabuf ALIAS brodmabuf)

target_include_directories(brodmabuf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(BRODMABUF_LINUX)
    target_link_libraries(brodmabuf
        PUBLIC
            PkgConfig::LIBDRM
            PkgConfig::GBM
            Vulkan::Vulkan
            Threads::Threads)
endif()

brodmabuf_warnings(brodmabuf)

if(BRODMABUF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
