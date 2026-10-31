add_executable(se_cli se_cli.cpp)
target_include_directories(se_cli PRIVATE ${SE_THIRD_PARTY}/nlohmann/include)
target_link_libraries(se_cli PRIVATE subtitle_engine)
