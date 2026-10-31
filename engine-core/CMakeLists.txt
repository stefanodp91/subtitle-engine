# zstd, decompression only (the models ship as .zst).
file(GLOB SE_ZSTD_DECOMPRESS ${SE_THIRD_PARTY}/zstd/lib/common/*.c ${SE_THIRD_PARTY}/zstd/lib/decompress/*.c)
add_library(zstd_decompress STATIC ${SE_ZSTD_DECOMPRESS})
target_include_directories(zstd_decompress PUBLIC ${SE_THIRD_PARTY}/zstd/lib)
target_compile_definitions(zstd_decompress PUBLIC ZSTD_DISABLE_ASM PRIVATE ZSTD_MULTITHREAD=0)
set_target_properties(zstd_decompress PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(subtitle_engine_core STATIC src/translator.cpp src/ebml.cpp src/mkv_text_tracks.cpp src/zstd_file.cpp)
target_include_directories(subtitle_engine_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${SE_THIRD_PARTY}/mozilla-translations/inference/src
          ${SE_THIRD_PARTY}/mozilla-translations/inference/marian-fork/src/3rd_party/zlib)
target_link_libraries(subtitle_engine_core PUBLIC bergamot-translator-source zstd_decompress)
