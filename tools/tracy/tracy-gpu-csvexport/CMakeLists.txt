cmake_minimum_required(VERSION 3.15)
project(tracy-gpu-csvexport C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(TRACY ${CMAKE_CURRENT_SOURCE_DIR}/../../../wowViewerLib/3rdparty/tracy)

file(GLOB ZSTD_SOURCES
    ${TRACY}/zstd/common/*.c ${TRACY}/zstd/compress/*.c
    ${TRACY}/zstd/decompress/*.c ${TRACY}/zstd/dictBuilder/*.c)

add_executable(tracy-gpu-csvexport
    csvexport.cpp
    ${TRACY}/getopt/getopt.c
    ${TRACY}/public/common/TracySocket.cpp
    ${TRACY}/public/common/TracyStackFrames.cpp
    ${TRACY}/public/common/TracySystem.cpp
    ${TRACY}/public/common/tracy_lz4.cpp
    ${TRACY}/public/common/tracy_lz4hc.cpp
    ${TRACY}/server/TracyMemory.cpp
    ${TRACY}/server/TracyMmap.cpp
    ${TRACY}/server/TracyPrint.cpp
    ${TRACY}/server/TracyTaskDispatch.cpp
    ${TRACY}/server/TracyTextureCompression.cpp
    ${TRACY}/server/TracyThreadCompress.cpp
    ${TRACY}/server/TracyWorker.cpp
    ${ZSTD_SOURCES}
    stub/capstone_stub.c)

# csvexport.cpp is the bundled file with the -g patch, so its includes ("../../server/...")
# resolve against the bundled csvexport/src, not against this folder.
target_include_directories(tracy-gpu-csvexport PRIVATE stub ${TRACY}/csvexport/src)
target_compile_definitions(tracy-gpu-csvexport PRIVATE
    _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE
    WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES)
target_link_libraries(tracy-gpu-csvexport PRIVATE ws2_32)
