cmake_minimum_required(VERSION 3.10)
project(lgmp LANGUAGES C)

include_directories(include)

set(SOURCES
	src/host.c
	src/client.c
	src/status.c
	src/log.c
	src/modules/shm/host.c
	src/modules/shm/client.c
)

include(cmake/fabric.cmake)

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

add_library(lgmp STATIC ${SOURCES} ${FABRIC_SOURCES})
target_include_directories(lgmp
	INTERFACE
		include
	PRIVATE
		src
		src/modules
)

lgmp_link_fabric(lgmp)
