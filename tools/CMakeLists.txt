add_executable(se_cli se_cli.cpp listen_command.cpp models_command.cpp)
target_include_directories(se_cli PRIVATE ${SE_THIRD_PARTY}/nlohmann/include)
target_link_libraries(se_cli PRIVATE subtitle_engine)
