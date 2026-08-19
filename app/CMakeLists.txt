# app/ is the Qt6 desktop application from M2 onwards. In M1 it is the command
# line entry point only: `paddock --version` is enough to prove the executable
# links against core and that packaging has something to package.
#
# Qt6 arrives with the 2D map view in M2 (vcpkg feature "gui").

add_executable(paddock src/main.cpp)

if(PADDOCK_BUILD_CONFIG)
  target_link_libraries(paddock PRIVATE Paddock::config)
  target_compile_definitions(paddock PRIVATE PADDOCK_WITH_CONFIG)
endif()

# `paddock source test` can check a DEM or a parcel file as well as a weather
# snapshot, but only in a build that has the geospatial stack. Without it the
# command still works on weather, and says why the others are unavailable
# rather than pretending the file is unreadable.
if(PADDOCK_BUILD_GIS)
  target_link_libraries(paddock PRIVATE Paddock::gis)
  target_compile_definitions(paddock PRIVATE PADDOCK_WITH_GIS)
endif()

target_link_libraries(paddock PRIVATE Paddock::core Paddock::project_options)

set_target_properties(paddock PROPERTIES OUTPUT_NAME paddock)

# paddock-gui: the 2D map view (ADR 0010). Built only when PADDOCK_BUILD_GUI is
# on, so a headless machine never links Qt or VTK.
if(PADDOCK_BUILD_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  find_package(
    VTK REQUIRED
    COMPONENTS CommonCore
               CommonDataModel
               ImagingCore
               IOImage
               InteractionStyle
               RenderingAnnotation
               RenderingCore
               RenderingOpenGL2
               GUISupportQt)

  add_executable(
    paddock-gui
    src/gui/main.cpp
    src/gui/MapWindow.cpp
    src/gui/MapWindow.hpp
    src/gui/ReportDialog.cpp
    src/gui/ReportDialog.hpp
    src/gui/SetupPanel.cpp
    src/gui/SetupPanel.hpp)

  set_target_properties(paddock-gui PROPERTIES AUTOMOC ON)

  target_link_libraries(paddock-gui PRIVATE Paddock::config Paddock::viz_vtk Qt6::Widgets
                                            ${VTK_LIBRARIES} Paddock::project_options)

  # A build with the geospatial stack can draw a farm on measured ground; one
  # without still runs, and refuses a scenario whose terrain is a file rather
  # than quietly flattening it.
  if(PADDOCK_BUILD_GIS)
    target_link_libraries(paddock-gui PRIVATE Paddock::gis)
    target_compile_definitions(paddock-gui PRIVATE PADDOCK_WITH_GIS)
  endif()

  # VTK's factories are registered by generated code; without this the render
  # window has no OpenGL implementation and the map is a blank rectangle.
  vtk_module_autoinit(TARGETS paddock-gui MODULES ${VTK_LIBRARIES})
endif()
