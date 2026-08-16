# viz/ turns core snapshots into something a person can look at.
#
# It is split deliberately. The value-to-colour mapping is pure C++ with no Qt
# and no VTK in it, so the part that can be quantitatively wrong is unit tested
# headlessly. The VTK pipeline is compiled only when the GUI is being built.
# See docs/adr/0010-2d-map-view-with-vtk.md.

add_library(paddock_viz STATIC src/ColourScale.cpp)
add_library(Paddock::viz ALIAS paddock_viz)

target_include_directories(
  paddock_viz PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_viz PUBLIC Paddock::core)
target_link_libraries(paddock_viz PRIVATE Paddock::project_options)
