add_library(redbound_audio STATIC Audio.cpp)
target_include_directories(redbound_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${PROJECT_SOURCE_DIR}/third_party/dr_libs)
set_target_properties(redbound_audio PROPERTIES CXX_STANDARD 17)
target_link_libraries(redbound_audio PUBLIC redbound_sdl2)
