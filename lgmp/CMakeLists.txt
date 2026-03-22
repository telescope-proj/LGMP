cmake_minimum_required(VERSION 3.10)
project(lgmp LANGUAGES C)

include_directories(include)

set(SOURCES
	src/host.c
	src/client.c
	src/status.c
	src/modules/shm/host.c
	src/modules/shm/client.c
)

option(ENABLE_FABRIC "Enable fabric (network) transport module" OFF)

if(ENABLE_FABRIC)
  list(APPEND SOURCES
    src/modules/fabric/host/host.c
    src/modules/fabric/host/host_callback.c
    src/modules/fabric/client/client.c
    src/modules/fabric/client/client_callback.c
    src/modules/fabric/nfr/nfr_log.c
    src/modules/fabric/nfr/nfr_mem.c
    src/modules/fabric/nfr/nfr_resource.c
  )
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

add_library(lgmp STATIC ${SOURCES})
target_include_directories(lgmp
	INTERFACE
		include
	PRIVATE
		src
		src/modules
)

if(ENABLE_FABRIC)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBFABRIC REQUIRED libfabric)
  target_include_directories(lgmp PRIVATE
    ${LIBFABRIC_INCLUDE_DIRS}
    src/modules/fabric
    src/modules/fabric/nfr
  )
  target_link_libraries(lgmp ${LIBFABRIC_LIBRARIES})
  target_compile_definitions(lgmp PUBLIC ENABLE_FABRIC)
endif()

