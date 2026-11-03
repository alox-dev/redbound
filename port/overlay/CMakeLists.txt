set(IMGUI ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_library(redbound_overlay STATIC Overlay.cpp Config.cpp AndroidBridge.cpp I18n.cpp
	${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
	${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(redbound_overlay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${IMGUI})
target_compile_definitions(redbound_overlay PUBLIC IMGUI_IMPL_OPENGL_ES2)
set_target_properties(redbound_overlay PROPERTIES CXX_STANDARD 17)
target_compile_definitions(redbound_overlay PRIVATE REDBOUND_VERSION="${PROJECT_VERSION}")
target_link_libraries(redbound_overlay PUBLIC redbound_launcher redbound_audio redbound_sdl2 redbound_gles)
if(NOT ANDROID)
  target_compile_definitions(redbound_overlay PRIVATE REDBOUND_LANG_DIR="${PROJECT_SOURCE_DIR}/assets/lang")
endif()
