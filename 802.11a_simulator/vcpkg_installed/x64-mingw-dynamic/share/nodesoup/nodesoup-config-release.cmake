#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "nodesoup::nodesoup" for configuration "Release"
set_property(TARGET nodesoup::nodesoup APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(nodesoup::nodesoup PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libnodesoup.a"
  )

list(APPEND _cmake_import_check_targets nodesoup::nodesoup )
list(APPEND _cmake_import_check_files_for_nodesoup::nodesoup "${_IMPORT_PREFIX}/lib/libnodesoup.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
