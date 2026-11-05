set(APP_SOURCES
	main.cpp
	AmbientSounds.cpp
	Devices.cpp
	Log.cpp
	FadeOverlay.cpp
	FileUtils.cpp
	Hud.cpp
	LaunchOptions.cpp
	LevelMenu.cpp
	LevelSession.cpp
	PlayerInput.cpp
	SceneSetup.cpp
	SkyRenderer.cpp)

if(ANDROID)
  add_library(main SHARED ${APP_SOURCES})
  set(REDBOUND_APP main)
else()
  add_executable(redbound ${APP_SOURCES})
  set(REDBOUND_APP redbound)
endif()
set_target_properties(${REDBOUND_APP} PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS ON)
target_link_libraries(${REDBOUND_APP} redbound_world redbound_audio redbound_ui redbound_launcher redbound_overlay redbound_gles2 redbound_sdl2 redbound_gles)
if(WIN32)
  set_target_properties(${REDBOUND_APP} PROPERTIES WIN32_EXECUTABLE TRUE)
  if(REDBOUND_WINDOWS_ICON)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/redbound.rc "1 ICON \"${REDBOUND_WINDOWS_ICON}\"\n")
    target_sources(${REDBOUND_APP} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/redbound.rc)
  endif()
endif()
if(ANDROID)
  target_link_libraries(${REDBOUND_APP} log android)
else()
  target_compile_definitions(${REDBOUND_APP} PRIVATE REDBOUND_CONTROLLERDB="${PROJECT_SOURCE_DIR}/third_party/gamecontrollerdb/gamecontrollerdb.txt")
endif()
target_compile_definitions(${REDBOUND_APP} PRIVATE REDBOUND_VERSION="${REDBOUND_VERSION_STRING}")
