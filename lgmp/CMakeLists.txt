cmake_minimum_required(VERSION 3.10)
project(lgmp LANGUAGES C)

include_directories(include)

set(SOURCES
	src/host.c
	src/client.c
	src/status.c
)

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
)
