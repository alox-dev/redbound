add_library(redbound_ui STATIC Ui.cpp)
target_include_directories(redbound_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(redbound_ui PROPERTIES CXX_STANDARD 17)
target_link_libraries(redbound_ui PUBLIC redbound_image redbound_gles)
