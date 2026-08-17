# gis/ reads DEM, cadastre and waterway data and hands core its own raster and
# polygon types. GDAL, PROJ and GEOS appear here and nowhere else in the tree.
#
# GDAL 3.0 and PROJ 6.0 are floors, not preferences. PROJ 6 is the release that
# moved the coordinate operation tables into proj.db and introduced the API used
# in Environment.cpp; GDAL 3 is the release built on it, and the release that
# started honouring the authority-declared axis order. That second change
# matters more here than anywhere else: EPSG:2193 declares its axes as
# (northing, easting), so a GDAL 3 build hands back coordinates in the opposite
# order from the (easting, northing) a New Zealand reader expects unless the
# traditional order is asked for explicitly. See docs/adr/0011.
find_package(GDAL 3.0 REQUIRED)
find_package(PROJ 6.0 REQUIRED)

add_library(paddock_gis STATIC src/Module.cpp src/Environment.cpp)
add_library(Paddock::gis ALIAS paddock_gis)

target_include_directories(
  paddock_gis PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_gis PUBLIC Paddock::core)

# PRIVATE, deliberately. A PUBLIC link would put GDAL's include directories on
# every consumer of Paddock::gis, and the rule that GDAL types stop at this
# boundary would then hold only by convention. PRIVATE makes it hold by link
# error: no header under gis/include names a GDAL or PROJ type.
target_link_libraries(paddock_gis PRIVATE GDAL::GDAL PROJ::proj Paddock::project_options)

message(STATUS "  gis: GDAL ${GDAL_VERSION}, PROJ ${PROJ_VERSION}")
