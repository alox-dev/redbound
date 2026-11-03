add_executable(pvr2png main.cpp)
set_target_properties(pvr2png PROPERTIES CXX_STANDARD 11 CXX_EXTENSIONS ON)
target_link_libraries(pvr2png redbound_image)
