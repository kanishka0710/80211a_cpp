
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was Matplot++Config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################
include(CMakeFindDependencyMacro)

# How this Matplot++ installation was built
set(MATPLOT_BUILT_SHARED "OFF")
set(MATPLOT_BUILT_CXX_COMPILER_ID "GNU")
set(MATPLOT_BUILT_CXX_COMPILER_VERSION "15.2.0")

# Check if it matches the current toolchain
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL MATPLOT_BUILT_CXX_COMPILER_ID)
    message(WARNING "This installation of Matplot++ was built with ${MATPLOT_BUILT_CXX_COMPILER_ID}.")
endif()

if ("OFF")
  find_dependency(glad CONFIG)
  find_dependency(glfw3 CONFIG)
endif()
find_dependency(nodesoup CONFIG )
if ("OFF")
find_dependency(JPEG)
endif()

if ("OFF")
find_dependency(TIFF)
endif()

if ("OFF")
find_dependency(ZLIB)
find_dependency(libpng CONFIG)
endif()

if ("OFF")
find_dependency(LAPACK)
endif()

if ("OFF")
find_dependency(BLAS)
endif()

if ("OFF")
find_dependency(FFTW3 CONFIG)
endif()

if ("OFF")
find_dependency(OpenCV CONFIG)
endif()

# Find dependencies
if(NOT ${MATPLOT_BUILT_SHARED})
    list(APPEND CMAKE_MODULE_PATH ${MATPLOT_CONFIG_INSTALL_DIR})
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
    list(POP_BACK CMAKE_MODULE_PATH)
endif()

# Create imported targets
include("${CMAKE_CURRENT_LIST_DIR}/Matplot++Targets.cmake")
check_required_components(Matplot++)
