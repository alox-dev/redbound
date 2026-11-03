cmake_minimum_required(VERSION 3.16)
project(redbound VERSION 1.0.0 LANGUAGES C CXX)
set(REDBOUND_VERSION_STRING "${PROJECT_VERSION}")

option(REDBOUND_ASAN "Build with AddressSanitizer" OFF)
if(REDBOUND_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

option(REDBOUND_VIEWER "Build the game (requires SDL2 and GLES2)" ON)
if(ANDROID)
  set(REDBOUND_TOOLS OFF CACHE BOOL "" FORCE)
  if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/SDL2/CMakeLists.txt)
    message(FATAL_ERROR "SDL2 for Android is missing. Run scripts/setup-sdl2.sh")
  endif()
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  add_subdirectory(third_party/SDL2 SDL2)
  add_library(redbound_sdl2 INTERFACE)
  target_link_libraries(redbound_sdl2 INTERFACE SDL2::SDL2)
  add_library(redbound_gles INTERFACE)
  target_link_libraries(redbound_gles INTERFACE GLESv2 EGL)
elseif(REDBOUND_VIEWER)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
  add_library(redbound_sdl2 INTERFACE)
  target_link_libraries(redbound_sdl2 INTERFACE PkgConfig::SDL2)
  add_library(redbound_gles INTERFACE)
  target_link_libraries(redbound_gles INTERFACE PkgConfig::GLESV2)
endif()
option(REDBOUND_TOOLS "Desktop tools and tests" ON)

add_subdirectory(engine)
add_subdirectory(game)

add_library(redbound_image STATIC
  port/render/gles2/ImageLoader.cpp
  third_party/pvrtc/PVRTDecompress.cpp)
target_include_directories(redbound_image PUBLIC
  ${PROJECT_SOURCE_DIR}/port/render/gles2
  ${PROJECT_SOURCE_DIR}/third_party/pvrtc
  ${PROJECT_SOURCE_DIR}/third_party/stb)
set_target_properties(redbound_image PROPERTIES CXX_STANDARD 11 CXX_EXTENSIONS ON)

add_subdirectory(port/game)
if(REDBOUND_TOOLS)
  add_subdirectory(tools/hgr-dump)
  add_subdirectory(tools/pvr2png)
endif()

if(REDBOUND_VIEWER)
  add_subdirectory(port/render/gles2)
  add_subdirectory(port/audio)
  add_subdirectory(port/ui)
  add_subdirectory(port/launcher)
  add_subdirectory(port/overlay)
  add_subdirectory(app)
endif()
