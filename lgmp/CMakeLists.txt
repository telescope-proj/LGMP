cmake_minimum_required(VERSION 3.10)
project(lgmp LANGUAGES C)

option(ENABLE_FABRIC "Enable fabric (network) transport module" OFF)
if(ENABLE_FABRIC)
  include(cmake/fabric.cmake)
endif()

add_compile_options(
  "-Wall"
  "-Werror"
  "-Wstrict-prototypes"
  "-Wfatal-errors"
  "-ffast-math"
  "-fdata-sections"
  "-ffunction-sections"
  "$<$<CONFIG:DEBUG>:-O0;-g3;-ggdb>"
)

# --- Core library (dispatch layer, logging, status) ---
add_library(lgmp STATIC
  src/host.c
  src/client.c
  src/status.c
  src/log.c
)
target_include_directories(lgmp
  PUBLIC
    include
  PRIVATE
    src
    src/modules
)

# --- SHM module (optional, on by default) ---
option(ENABLE_SHM "Enable shared memory transport module" ON)
if(ENABLE_SHM)
  add_library(lgmp_shm STATIC
    src/modules/shm/host.c
    src/modules/shm/client.c
  )
  target_include_directories(lgmp_shm
    PRIVATE
      src
      src/modules
  )
  target_link_libraries(lgmp_shm PUBLIC lgmp)
  target_compile_definitions(lgmp_shm PUBLIC ENABLE_SHM)
endif()

# Fabric Module (optional, off by default)
if(ENABLE_FABRIC)
  add_library(lgmp_fabric STATIC ${FABRIC_SOURCES})
  target_include_directories(lgmp_fabric
    PRIVATE
      src
      src/modules
      src/modules/fabric
      src/modules/fabric/nfr
  )
  target_link_libraries(lgmp_fabric PUBLIC lgmp)
  lgmp_link_fabric(lgmp_fabric)
endif()
