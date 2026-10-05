cmake_minimum_required(VERSION 3.24)

project(brodmabuf VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Top-level detection
set(_brodmabuf_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_brodmabuf_top_level ON)
endif()

option(BRODMABUF_BUILD_TESTS "Build the brodmabuf test suite" ${_brodmabuf_top_level})

# Warnings helper
function(brodmabuf_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)

set(BRODMABUF_SOURCES
    src/types.cpp
    src/formats.cpp
    src/buffer.cpp
    src/gbm.cpp
    src/sync.cpp
    src/vulkan.cpp
    src/kms.cpp
)

add_library(brodmabuf STATIC ${BRODMABUF_SOURCES})
add_library(brodmabuf::brodmabuf ALIAS brodmabuf)

target_include_directories(brodmabuf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(brodmabuf
    PUBLIC
        PkgConfig::LIBDRM
        PkgConfig::GBM
        Vulkan::Vulkan
        Threads::Threads
)

brodmabuf_warnings(brodmabuf)

if(BRODMABUF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
