add_library(redbound_gles2 STATIC GLES2.cpp EffectParser.cpp WaterReflection.cpp)
target_include_directories(redbound_gles2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(redbound_gles2 PROPERTIES CXX_STANDARD 11 CXX_EXTENSIONS ON)
target_link_libraries(redbound_gles2 PUBLIC redbound_image ka3d_core redbound_gles)
