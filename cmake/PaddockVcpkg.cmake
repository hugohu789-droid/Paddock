# Adopt a vcpkg toolchain when the environment provides one, and stay silent
# when it does not. Machine-local paths belong in CMakeUserPresets.json (which
# is gitignored) or in VCPKG_ROOT — never in CMakePresets.json.
#
# This file is included before project(), because CMAKE_TOOLCHAIN_FILE has no
# effect afterwards.

if(DEFINED CMAKE_TOOLCHAIN_FILE)
  return()
endif()

if(NOT DEFINED ENV{VCPKG_ROOT})
  return()
endif()

set(_paddock_vcpkg_toolchain "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

if(EXISTS "${_paddock_vcpkg_toolchain}")
  set(CMAKE_TOOLCHAIN_FILE
      "${_paddock_vcpkg_toolchain}"
      CACHE STRING "vcpkg toolchain file")
  message(STATUS "Using vcpkg toolchain from VCPKG_ROOT: ${_paddock_vcpkg_toolchain}")
else()
  message(WARNING "VCPKG_ROOT is set but ${_paddock_vcpkg_toolchain} does not exist; "
                  "configuring without vcpkg.")
endif()

unset(_paddock_vcpkg_toolchain)
