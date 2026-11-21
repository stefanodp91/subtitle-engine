cmake_minimum_required(VERSION 3.20)
project(subtitle_engine VERSION 0.4.0 LANGUAGES C CXX)

option(SE_BUILD_TESTS "Build the engine tests" ON)
option(SE_BUILD_TOOLS "Build se_cli" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Old CMake files in third_party")

# Every dependency static inside the engine (whisper.cpp's option(BUILD_SHARED_LIBS)
# would default to ON, and the cpuinfo of Marian's ruy follows it): set before any of them.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static libraries inside the engine" FORCE)

set(SE_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(NOT EXISTS ${SE_THIRD_PARTY}/mozilla-translations/inference/CMakeLists.txt)
  message(FATAL_ERROR "Run scripts/fetch_deps.sh first")
endif()

# Mozilla's inference engine, as a static library (bergamot-translator-source).
set(SSPLIT_USE_INTERNAL_PCRE2 ON CACHE BOOL "" FORCE)
set(SSPLIT_PCRE2_TARBALL ${SE_THIRD_PARTY}/_dl/pcre2-10.39.tar.gz CACHE FILEPATH "" FORCE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR ANDROID_ABI STREQUAL "arm64-v8a")
  # Marian sets these only for its own directory; the translator sources need them too.
  add_compile_definitions(ARM FMA SSE)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-flax-vector-conversions>)
endif()
add_subdirectory(${SE_THIRD_PARTY}/mozilla-translations/inference inference EXCLUDE_FROM_ALL)

# whisper.cpp (MIT), static inside the engine, on the CPU: no examples, tests,
# server, OpenMP, Metal or BLAS. For Android the cores of the step 1 measures.
foreach(opt WHISPER_BUILD_EXAMPLES WHISPER_BUILD_TESTS WHISPER_BUILD_SERVER GGML_OPENMP GGML_METAL GGML_BLAS)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
if(ANDROID)
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  set(GGML_CPU_ARM_ARCH "armv8.2-a+dotprod+fp16" CACHE STRING "" FORCE)
endif()
add_subdirectory(${SE_THIRD_PARTY}/whisper.cpp whisper EXCLUDE_FROM_ALL)

# FFmpeg, audio only (LGPL 2.1, shared), built by scripts/build_ffmpeg.sh.
set(SE_FFMPEG_DIR "" CACHE PATH "An FFmpeg build of scripts/build_ffmpeg.sh (include/ and lib/)")
if(NOT EXISTS ${SE_FFMPEG_DIR}/include/libavformat/avformat.h)
  message(FATAL_ERROR "Set SE_FFMPEG_DIR to a build of scripts/build_ffmpeg.sh")
endif()
foreach(lib avformat avcodec avutil swresample)
  find_library(SE_FFMPEG_${lib} NAMES ${lib} PATHS ${SE_FFMPEG_DIR}/lib NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
  add_library(ffmpeg::${lib} SHARED IMPORTED)
  set_target_properties(ffmpeg::${lib} PROPERTIES IMPORTED_LOCATION ${SE_FFMPEG_${lib}}
    INTERFACE_INCLUDE_DIRECTORIES ${SE_FFMPEG_DIR}/include)
endforeach()

add_subdirectory(engine-core)
add_subdirectory(engine-capi)
if(SE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(SE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${SE_THIRD_PARTY}/googletest googletest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
