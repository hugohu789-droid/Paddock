# app/ is the Qt6 desktop application from M2 onwards. In M1 it is the command
# line entry point only: `paddock --version` is enough to prove the executable
# links against core and that packaging has something to package.
#
# Qt6 arrives with the 2D map view in M2 (vcpkg feature "gui").

add_executable(paddock src/main.cpp)

target_link_libraries(paddock PRIVATE Paddock::core Paddock::project_options)

set_target_properties(paddock PROPERTIES OUTPUT_NAME paddock)
