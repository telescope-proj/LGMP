option(ENABLE_FABRIC "Enable fabric (network) transport module" OFF)

include(CMakeDependentOption)
cmake_dependent_option(
  USE_UPSTREAM_LIBFABRIC
  "Build and use the bundled libfabric v2 from repos/libfabric instead of the system installation"
  OFF
  "ENABLE_FABRIC;NOT WIN32"
  OFF
)

# On Windows, libfabric must be compiled with MSVC. Set this to the MSVC install
# prefix so that MinGW can link against the libfabric library and DLL.
set(LIBFABRIC_ROOT "" CACHE PATH
  "Pre-built libfabric install prefix (include/ and lib/ subdirectories)")

if(ENABLE_FABRIC)
  set(FABRIC_SOURCES
    src/modules/fabric/host/host.c
    src/modules/fabric/host/host_callback.c
    src/modules/fabric/client/client.c
    src/modules/fabric/client/client_callback.c
    src/modules/fabric/nfr/nfr_mem.c
    src/modules/fabric/nfr/nfr_resource.c
  )
endif()

# Configure a target to link against libfabric.
# Called after the target is created.
function(lgmp_link_fabric target)
  if(NOT ENABLE_FABRIC)
    return()
  endif()

  if(LIBFABRIC_ROOT OR WIN32)
    include(libfabric_prebuilt)
  elseif(USE_UPSTREAM_LIBFABRIC)
    include(libfabric_upstream)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBFABRIC REQUIRED libfabric IMPORTED_TARGET)
    add_library(libfabric INTERFACE IMPORTED GLOBAL)
    target_link_libraries(libfabric INTERFACE PkgConfig::LIBFABRIC)
  endif()

  target_include_directories(${target} PRIVATE
    src/modules/fabric
    src/modules/fabric/nfr
  )
  target_link_libraries(${target} libfabric)
  target_compile_definitions(${target} PUBLIC ENABLE_FABRIC)
endfunction()
