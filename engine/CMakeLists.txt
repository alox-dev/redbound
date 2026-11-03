set(KA3D_DIR ${PROJECT_SOURCE_DIR}/third_party/ka3d)
if(NOT EXISTS ${KA3D_DIR}/include/config/linux/config.h)
  message(FATAL_ERROR "KA3D is not set up. Run scripts/setup-ka3d.sh")
endif()

file(GLOB KA3D_SRC
  ${KA3D_DIR}/source/lang/*.cpp
  ${KA3D_DIR}/source/io/*.cpp ${KA3D_DIR}/source/io/std/*.cpp
  ${KA3D_DIR}/source/math/*.cpp ${KA3D_DIR}/source/math/std/*.cpp
  ${KA3D_DIR}/source/gr/*.cpp
  ${KA3D_DIR}/source/hgr/*.cpp)
list(FILTER KA3D_SRC EXCLUDE REGEX "/test\\.cpp$")

add_library(ka3d_core STATIC ${KA3D_SRC})
target_include_directories(ka3d_core PUBLIC ${KA3D_DIR}/include ${KA3D_DIR}/include/config/linux)
set_target_properties(ka3d_core PROPERTIES CXX_STANDARD 98 CXX_EXTENSIONS ON)
target_compile_options(ka3d_core PUBLIC -fpermissive -w)
