# viz/ turns core snapshots into something a person can look at.
#
# Two targets, deliberately. paddock_viz is the value-to-colour mapping: pure
# C++, no Qt, no VTK, so the part that can be quantitatively wrong is unit
# tested headlessly. paddock_viz_vtk is the render pipeline and exists only when
# the GUI is being built. Keeping them apart is what stops a colour-scale test
# from linking a graphics stack it has no use for - which is exactly what
# happened when they were one target.
#
# See docs/adr/0010-2d-map-view-with-vtk.md.

add_library(paddock_viz STATIC src/ColourScale.cpp)
add_library(Paddock::viz ALIAS paddock_viz)

target_include_directories(
  paddock_viz PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_viz PUBLIC Paddock::core)
target_link_libraries(paddock_viz PRIVATE Paddock::project_options)

if(PADDOCK_BUILD_GUI)
  find_package(
    VTK REQUIRED
    COMPONENTS CommonCore
               CommonDataModel
               FiltersCore
               FiltersGeometry
               ImagingCore
               IOImage
               InteractionStyle
               RenderingAnnotation
               RenderingCore
               RenderingOpenGL2
               GUISupportQt)

  add_library(paddock_viz_vtk STATIC src/ColourTable.cpp src/MapScene.cpp src/MobMarkers.cpp
                                     src/TerrainScene.cpp)
  add_library(Paddock::viz_vtk ALIAS paddock_viz_vtk)

  target_link_libraries(paddock_viz_vtk PUBLIC Paddock::viz ${VTK_LIBRARIES})
  target_link_libraries(paddock_viz_vtk PRIVATE Paddock::project_options)

  # Every target that creates VTK objects needs this. Without it the object
  # factories are never registered, vtkNew hands back an implementation with no
  # OpenGL behind it, and construction dies on the first widget.
  vtk_module_autoinit(TARGETS paddock_viz_vtk MODULES ${VTK_LIBRARIES})
endif()
