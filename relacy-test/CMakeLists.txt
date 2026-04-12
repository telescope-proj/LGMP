cmake_minimum_required(VERSION 3.10)
project(relacy-test)

include_directories(include)
set(SOURCES main.cpp)

get_filename_component(PROJECT_TOP "${PROJECT_SOURCE_DIR}/.." ABSOLUTE)
add_subdirectory("${PROJECT_TOP}/lgmp" "${CMAKE_BINARY_DIR}/lgmp")
target_compile_definitions(lgmp PRIVATE -DLGMP_REALACY)
if(ENABLE_SHM)
  target_compile_definitions(lgmp_shm PRIVATE -DLGMP_REALACY)
endif()

add_executable(relacy-test ${SOURCES})
target_link_libraries(relacy-test
	lgmp
)

if(ENABLE_SHM)
  target_link_libraries(relacy-test lgmp_shm)
endif()
target_include_directories(relacy-test
	PUBLIC
		"${PROJECT_TOP}/relacy"
)
