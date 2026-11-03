add_executable(hgr-dump hgr-dump.cpp)
set_target_properties(hgr-dump PROPERTIES CXX_STANDARD 11 CXX_EXTENSIONS ON)
target_link_libraries(hgr-dump ka3d_core redbound_image)
