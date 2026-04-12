set(_LGMP_FABRIC_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# On Windows, libfabric must be compiled with MSVC. Set this to the MSVC install
# prefix so that MinGW can link against the libfabric library and DLL.
set(LIBFABRIC_ROOT "" CACHE PATH
  "Pre-built libfabric install prefix (include/ and lib/ subdirectories)")

set(FABRIC_SOURCES
  src/modules/fabric/host/host.c
  src/modules/fabric/host/host_callback.c
  src/modules/fabric/client/client.c
  src/modules/fabric/client/client_callback.c
  src/modules/fabric/nfr/nfr_mem.c
  src/modules/fabric/nfr/nfr_resource.c
)

# Configure a target to link against libfabric.
function(lgmp_link_fabric target)
  if(LIBFABRIC_ROOT OR WIN32)
    include("${_LGMP_FABRIC_CMAKE_DIR}/libfabric_prebuilt.cmake")
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFABRIC REQUIRED libfabric IMPORTED_TARGET)
    add_library(libfabric INTERFACE IMPORTED GLOBAL)
    target_link_libraries(libfabric INTERFACE PkgConfig::LIBFABRIC)
  endif()

  target_link_libraries(${target} PRIVATE libfabric)
  if(WIN32)
    target_link_libraries(${target} PRIVATE ws2_32 bcrypt)
  endif()
  target_compile_definitions(${target} PUBLIC ENABLE_FABRIC)
endfunction()
