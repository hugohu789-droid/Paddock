cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Must run before project(): picks up a vcpkg toolchain if the machine has one.
# core/ needs no dependencies at all, so a bare compiler is a complete
# development environment; the manifest only matters from M2 onwards.
include(PaddockVcpkg)

project(
  Paddock
  VERSION 0.1.0
  DESCRIPTION "Spatially explicit pastoral farm simulator for New Zealand conditions"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(PADDOCK_BUILD_TESTS "Build the gtest suites" ON)
option(PADDOCK_BUILD_APP "Build the app/ entry point" ON)
option(PADDOCK_BUILD_CONFIG "Build config/ (TOML loading; fetches toml++ if absent)" ON)
option(PADDOCK_BUILD_GIS "Build gis/ (requires GDAL, PROJ, GEOS)" OFF)
option(PADDOCK_BUILD_VIZ "Build viz/ (colour mapping; no external dependencies)" ON)
option(PADDOCK_BUILD_GUI "Build the Qt6 + VTK desktop application" OFF)
option(PADDOCK_WITH_AI "Build the optional ai/ module (requires libcurl)" OFF)
option(PADDOCK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(PADDOCK_SANITIZE
    ""
    CACHE STRING "Semicolon-separated sanitizers, e.g. address;undefined")

if(PADDOCK_BUILD_GUI)
  # Distribution builds of VTK are compiled with MPI, and their CMake package
  # resolves the imported target MPI::MPI_C. find_package(MPI) only provides
  # that when the C language is enabled, so a CXX-only project fails to
  # configure against Ubuntu's VTK with "MPI component 'C' was requested, but
  # language C is not enabled". Nothing here is written in C.
  enable_language(C)
endif()

include(PaddockCompileOptions)

add_subdirectory(core)

if(PADDOCK_BUILD_CONFIG)
  add_subdirectory(config)
endif()

if(PADDOCK_BUILD_GIS)
  add_subdirectory(gis)
endif()

if(PADDOCK_BUILD_VIZ)
  add_subdirectory(viz)
endif()

if(PADDOCK_BUILD_APP)
  add_subdirectory(app)
endif()

if(PADDOCK_WITH_AI)
  add_subdirectory(ai)
endif()

if(PADDOCK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "Paddock ${PROJECT_VERSION}")
message(STATUS "  tests: ${PADDOCK_BUILD_TESTS}  app: ${PADDOCK_BUILD_APP}  config: ${PADDOCK_BUILD_CONFIG}")
message(STATUS "  gis: ${PADDOCK_BUILD_GIS}  viz: ${PADDOCK_BUILD_VIZ}  gui: ${PADDOCK_BUILD_GUI}  ai: ${PADDOCK_WITH_AI}")
message(STATUS "  sanitizers: '${PADDOCK_SANITIZE}'")
