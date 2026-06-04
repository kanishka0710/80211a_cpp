#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Matplot++::matplot" for configuration "Debug"
set_property(TARGET Matplot++::matplot APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Matplot++::matplot PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libmatplot.a"
  )

list(APPEND _cmake_import_check_targets Matplot++::matplot )
list(APPEND _cmake_import_check_files_for_Matplot++::matplot "${_IMPORT_PREFIX}/debug/lib/libmatplot.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
