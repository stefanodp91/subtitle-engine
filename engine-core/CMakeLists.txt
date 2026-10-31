add_library(subtitle_engine_core STATIC src/translator.cpp src/ebml.cpp src/mkv_text_tracks.cpp)
target_include_directories(subtitle_engine_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${SE_THIRD_PARTY}/mozilla-translations/inference/src
          ${SE_THIRD_PARTY}/mozilla-translations/inference/marian-fork/src/3rd_party/zlib)
target_link_libraries(subtitle_engine_core PUBLIC bergamot-translator-source)
