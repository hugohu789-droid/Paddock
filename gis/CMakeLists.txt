# gis/ reads DEM, cadastre and waterway data and hands core its own raster and
# polygon types. GDAL, PROJ and GEOS appear here and nowhere else in the tree.
#
# The dependencies (vcpkg feature "gis") arrive together with the real readers
# in M3; the skeleton exists now so that the dependency direction gis -> core is
# a fact in the build graph rather than a promise in a document.

add_library(paddock_gis STATIC src/Module.cpp)
add_library(Paddock::gis ALIAS paddock_gis)

target_include_directories(
  paddock_gis PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_gis PUBLIC Paddock::core)
target_link_libraries(paddock_gis PRIVATE Paddock::project_options)
