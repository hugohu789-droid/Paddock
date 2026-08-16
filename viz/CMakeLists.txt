# viz/ turns core snapshots into VTK scenes: the 2D map view with its colour
# scale and timeline, and the 3D terrain view with livestock glyphs.
#
# VTK (vcpkg feature "gui") arrives with the 2D map view in M2. The skeleton
# exists now to hold the direction viz -> core in the build graph.

add_library(paddock_viz STATIC src/Module.cpp)
add_library(Paddock::viz ALIAS paddock_viz)

target_include_directories(
  paddock_viz PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_viz PUBLIC Paddock::core)
target_link_libraries(paddock_viz PRIVATE Paddock::project_options)
