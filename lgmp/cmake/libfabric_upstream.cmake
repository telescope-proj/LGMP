# Builds libfabric from the bundled source tree via autotools + ExternalProject.
# Creates an IMPORTED SHARED target named `libfabric` and wires it to `${target}`.
# Must be included from within lgmp_link_fabric() so that `target` is in scope.

# Only supported on non-Windows platforms, as Libfabric on Windows must be built
# with MSVC, and Looking Glass uses MinGW. See the file libfabric_prebuilt.cmake 
# for using a pre-built MSVC libfabric on Windows.

if(WIN32)
  message(
    FATAL_ERROR 
    "Upstream libfabric build is not supported on Windows. "
    "Set LIBFABRIC_ROOT to a pre-built MSVC libfabric installation instead."
  )
endif()

include(ExternalProject)
set(_LF_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../repos/libfabric")

find_program(AUTORECONF_EXECUTABLE autoreconf REQUIRED
  DOC "autoreconf is required to generate the libfabric configure script")

set(_LF_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/libfabric-install")
set(_LF_LIB
  "${_LF_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}fabric${CMAKE_SHARED_LIBRARY_SUFFIX}")

# When cross-compiling, pass --host derived from the C compiler name so that
# autotools targets the right system (e.g. x86_64-w64-mingw32).
set(_LF_CONFIGURE_ARGS
  --prefix=${_LF_INSTALL_DIR}
  --disable-static
  --enable-shared
)
if(CMAKE_CROSSCOMPILING)
  get_filename_component(_CC_NAME "${CMAKE_C_COMPILER}" NAME)
  string(REGEX REPLACE "-[^-]+$" "" _LF_HOST_TRIPLE "${_CC_NAME}")
  list(APPEND _LF_CONFIGURE_ARGS --host=${_LF_HOST_TRIPLE})
endif()

ExternalProject_Add(libfabric_ext
  SOURCE_DIR "${_LF_SOURCE_DIR}"
  CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E chdir "${_LF_SOURCE_DIR}" sh autogen.sh
    COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
      "${_LF_SOURCE_DIR}/configure" ${_LF_CONFIGURE_ARGS}
  BUILD_COMMAND   $(MAKE)
  INSTALL_COMMAND $(MAKE) install
  BUILD_BYPRODUCTS "${_LF_LIB}"
)

file(MAKE_DIRECTORY "${_LF_INSTALL_DIR}/include")
add_library(libfabric SHARED IMPORTED GLOBAL)
set_target_properties(libfabric PROPERTIES
  IMPORTED_LOCATION             "${_LF_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${_LF_INSTALL_DIR}/include"
)

add_dependencies(${target} libfabric_ext)
