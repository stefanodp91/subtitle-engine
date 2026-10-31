add_library(subtitle_engine SHARED src/subtitle_engine_c.cpp)
target_include_directories(subtitle_engine
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${SE_THIRD_PARTY}/nlohmann/include)
target_compile_definitions(subtitle_engine PRIVATE SE_BUILDING_LIBRARY)
target_link_libraries(subtitle_engine PRIVATE subtitle_engine_core)
set_target_properties(subtitle_engine PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Export only the se_* functions: the static dependencies (Marian, zstd, …)
# stay internal to the library.
if(APPLE)
  target_link_options(subtitle_engine PRIVATE -Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports_apple.txt)
else()
  target_link_options(subtitle_engine PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map -Wl,--exclude-libs,ALL)
endif()
