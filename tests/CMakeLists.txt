# The engine's include paths carry a regex.h of their own (ssplit, Marian):
# googletest must not reach for POSIX <regex.h>.
target_compile_definitions(gtest PUBLIC GTEST_HAS_POSIX_RE=0)

add_executable(se_tests test_version.cpp test_translator.cpp test_translator_capi.cpp test_mkv.cpp test_mkv_capi.cpp test_zstd.cpp)
target_include_directories(se_tests PRIVATE ${SE_THIRD_PARTY}/nlohmann/include
  ${CMAKE_SOURCE_DIR}/engine-core/src
  ${SE_THIRD_PARTY}/mozilla-translations/inference/marian-fork/src/3rd_party/zlib)
target_link_libraries(se_tests PRIVATE subtitle_engine subtitle_engine_core zstd_compress_for_tests GTest::gtest_main)
target_compile_definitions(se_tests PRIVATE SE_FIXTURES="${CMAKE_SOURCE_DIR}/fixtures" SE_TMP="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME se_tests COMMAND se_tests)

# zstd compression only to build test inputs; the engine ships decompression only.
file(GLOB SE_ZSTD_COMPRESS ${SE_THIRD_PARTY}/zstd/lib/compress/*.c)
add_library(zstd_compress_for_tests STATIC ${SE_ZSTD_COMPRESS})
target_link_libraries(zstd_compress_for_tests PUBLIC zstd_decompress)
target_compile_definitions(zstd_compress_for_tests PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0)
