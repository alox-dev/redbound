add_executable(redbound_3ds
  main.cpp
  ${PROJECT_SOURCE_DIR}/app/SceneSetup.cpp
  ${PROJECT_SOURCE_DIR}/app/FileUtils.cpp
  ${PROJECT_SOURCE_DIR}/app/LaunchOptions.cpp
  ${PROJECT_SOURCE_DIR}/port/launcher/Paths.cpp)
set_target_properties(redbound_3ds PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS ON)
target_compile_options(redbound_3ds PRIVATE -fpermissive)
target_include_directories(redbound_3ds PRIVATE ${PROJECT_SOURCE_DIR}/app ${PROJECT_SOURCE_DIR}/port/launcher)
target_link_libraries(redbound_3ds redbound_citro3d redbound_world)
target_compile_definitions(redbound_3ds PRIVATE REDBOUND_VERSION="${REDBOUND_VERSION_STRING}")

ctr_generate_smdh(redbound.smdh
  NAME "Redbound"
  DESCRIPTION "Bounce Evolution source port (probe)"
  AUTHOR "Alox")

ctr_create_3dsx(redbound_3ds SMDH redbound.smdh)
