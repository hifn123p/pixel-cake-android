cmake_minimum_required(VERSION 3.22.1)

project(PixelCakeRaw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 本文件位于 app/src/main/cpp/，往上 4 级是仓库根。
# third_party/ 与 app/ 同级，所以 LIBRAW_ROOT 不能用相对 app 的路径。
get_filename_component(PC_REPO_ROOT "${CMAKE_SOURCE_DIR}/../../../.." ABSOLUTE)
set(LIBRAW_ROOT "${PC_REPO_ROOT}/third_party/libraw" CACHE PATH "LibRaw 源码根目录")

if(NOT EXISTS "${LIBRAW_ROOT}/libraw/libraw.h")
    message(FATAL_ERROR
        "找不到 LibRaw 源码：${LIBRAW_ROOT}\n"
        "请先执行：git submodule update --init --recursive")
endif()

add_subdirectory(libraw)

add_library(pixelcake_raw SHARED raw_bridge.cpp)

target_link_libraries(pixelcake_raw
    PRIVATE
        libraw_static
        log)
