cmake_minimum_required(VERSION 3.20)
project(subtitle_engine VERSION 0.1.0 LANGUAGES C CXX)

option(SE_BUILD_TESTS "Build the engine tests" ON)
option(SE_BUILD_TOOLS "Build se_cli" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Old CMake files in third_party")

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
