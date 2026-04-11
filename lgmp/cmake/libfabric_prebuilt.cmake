# Uses a caller-supplied pre-built libfabric installation.
# Creates an IMPORTED SHARED target named `libfabric`.
# On Windows, libfabric must be compiled with MSVC; point MinGW at the
# resulting import library and DLL via -DLIBFABRIC_ROOT=<install-prefix>.
# If LIBFABRIC_ROOT is not set on Windows, the bundled submodule at
# repos/libfabric inside LGMP is probed automatically.
#
# Two layouts are supported:
#   Standard install prefix  – LIBFABRIC_ROOT/include/, LIBFABRIC_ROOT/lib/fabric.lib,
#                              LIBFABRIC_ROOT/bin/fabric.dll
#   MSVC submodule output    – LIBFABRIC_ROOT/libfabric.lib (and libfabric.dll) with
#                              the include/ directory at LIBFABRIC_ROOT/../../include/

# On Windows, auto-detect LIBFABRIC_ROOT from the bundled submodule when the
# user has not supplied it explicitly.
if(WIN32 AND NOT LIBFABRIC_ROOT)
  set(_lgmp_fabric_src
    "${CMAKE_CURRENT_LIST_DIR}/../../repos/libfabric")
  set(_lgmp_fabric_vcxproj "${_lgmp_fabric_src}/libfabric.vcxproj")
  if(EXISTS "${_lgmp_fabric_vcxproj}")
    # Parse available configurations directly from the project file so that
    # this list stays in sync with the submodule without manual maintenance.
    file(READ "${_lgmp_fabric_vcxproj}" _lgmp_fabric_vcxproj_xml)
    string(REGEX MATCHALL
      "<Configuration>[^<]+</Configuration>"
      _lgmp_fabric_cfg_tags "${_lgmp_fabric_vcxproj_xml}")
    unset(_lgmp_fabric_vcxproj_xml)

    set(_lgmp_fabric_debug_cfgs "")
    set(_lgmp_fabric_release_cfgs "")
    foreach(_lgmp_fabric_cfg_tag IN LISTS _lgmp_fabric_cfg_tags)
      string(REGEX REPLACE
        "<Configuration>([^<]+)</Configuration>" "\\1"
        _lgmp_fabric_cfg "${_lgmp_fabric_cfg_tag}")
      if(_lgmp_fabric_cfg MATCHES "^Debug")
        list(APPEND _lgmp_fabric_debug_cfgs "${_lgmp_fabric_cfg}")
      elseif(_lgmp_fabric_cfg MATCHES "^Release")
        list(APPEND _lgmp_fabric_release_cfgs "${_lgmp_fabric_cfg}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _lgmp_fabric_debug_cfgs)
    list(REMOVE_DUPLICATES _lgmp_fabric_release_cfgs)
    unset(_lgmp_fabric_cfg_tags)
    unset(_lgmp_fabric_cfg_tag)
    unset(_lgmp_fabric_cfg)

    # Select only the configurations that match the current build type.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _lgmp_fabric_build_type)
    if(_lgmp_fabric_build_type STREQUAL "DEBUG")
      set(_lgmp_fabric_probe_cfgs "${_lgmp_fabric_debug_cfgs}")
    else()
      set(_lgmp_fabric_probe_cfgs "${_lgmp_fabric_release_cfgs}")
    endif()
    unset(_lgmp_fabric_debug_cfgs)
    unset(_lgmp_fabric_release_cfgs)
    unset(_lgmp_fabric_build_type)

    foreach(_lgmp_fabric_candidate_cfg IN LISTS _lgmp_fabric_probe_cfgs)
      set(_lgmp_fabric_candidate
        "${_lgmp_fabric_src}/x64/${_lgmp_fabric_candidate_cfg}")
      if(EXISTS "${_lgmp_fabric_candidate}/libfabric.lib")
        set(LIBFABRIC_ROOT "${_lgmp_fabric_candidate}"
          CACHE PATH
          "Pre-built libfabric install prefix (include/ and lib/ subdirectories)"
          FORCE)
        message(STATUS
          "LGMP: auto-detected libfabric from submodule: ${LIBFABRIC_ROOT}")
        break()
      endif()
    endforeach()
    unset(_lgmp_fabric_probe_cfgs)
    unset(_lgmp_fabric_candidate_cfg)
    unset(_lgmp_fabric_candidate)
  endif()
  unset(_lgmp_fabric_vcxproj)
  unset(_lgmp_fabric_src)
endif()

find_library(LIBFABRIC_IMPLIB
  NAMES fabric libfabric
  PATHS "${LIBFABRIC_ROOT}/lib" "${LIBFABRIC_ROOT}"
  NO_DEFAULT_PATH
  REQUIRED
)

# Locate the include directory: standard layout has it under LIBFABRIC_ROOT,
# while a raw MSVC submodule build places it two levels up (at the repo root).
if(IS_DIRECTORY "${LIBFABRIC_ROOT}/include")
  set(_lgmp_fabric_include_dir "${LIBFABRIC_ROOT}/include")
else()
  get_filename_component(_lgmp_fabric_include_dir
    "${LIBFABRIC_ROOT}/../../include" ABSOLUTE)
endif()

set(_lgmp_fabric_include_dirs "${_lgmp_fabric_include_dir}")
if(WIN32 AND IS_DIRECTORY "${_lgmp_fabric_include_dir}/windows")
  list(APPEND _lgmp_fabric_include_dirs
    "${_lgmp_fabric_include_dir}/windows")
endif()

add_library(libfabric SHARED IMPORTED GLOBAL)
set_target_properties(libfabric PROPERTIES
  IMPORTED_IMPLIB               "${LIBFABRIC_IMPLIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${_lgmp_fabric_include_dirs}"
)
unset(_lgmp_fabric_include_dirs)
unset(_lgmp_fabric_include_dir)

if(WIN32)
  # Standard install layout puts the DLL in bin/; the MSVC submodule build
  # places it next to the import library.
  if(EXISTS "${LIBFABRIC_ROOT}/bin/fabric.dll")
    set_target_properties(libfabric PROPERTIES
      IMPORTED_LOCATION "${LIBFABRIC_ROOT}/bin/fabric.dll"
    )
  else()
    get_filename_component(_lgmp_fabric_implib_dir
      "${LIBFABRIC_IMPLIB}" DIRECTORY)
    set_target_properties(libfabric PROPERTIES
      IMPORTED_LOCATION "${_lgmp_fabric_implib_dir}/libfabric.dll"
    )
    unset(_lgmp_fabric_implib_dir)
  endif()
else()
  set_target_properties(libfabric PROPERTIES
    IMPORTED_LOCATION "${LIBFABRIC_IMPLIB}"
  )
endif()
