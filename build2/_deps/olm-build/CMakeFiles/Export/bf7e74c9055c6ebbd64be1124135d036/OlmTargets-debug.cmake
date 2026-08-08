#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Olm::Olm" for configuration "Debug"
set_property(TARGET Olm::Olm APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Olm::Olm PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libolm.a"
  )

list(APPEND _cmake_import_check_targets Olm::Olm )
list(APPEND _cmake_import_check_files_for_Olm::Olm "${_IMPORT_PREFIX}/lib/libolm.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
