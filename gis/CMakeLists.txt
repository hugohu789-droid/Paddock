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
#
# The floors are checked by hand rather than passed to find_package, because a
# version argument there does not mean "at least this". Each package decides for
# itself: PROJ's config declares SameMajorVersion compatibility, so
# find_package(PROJ 6.0) is read as "major version must be 6" and a machine with
# PROJ 9.8 is rejected for being too new. That is how this first landed, and
# both Linux and macOS refused to configure against a perfectly good PROJ.
find_package(GDAL REQUIRED)
find_package(PROJ REQUIRED)

if(GDAL_VERSION VERSION_LESS 3.0)
  message(FATAL_ERROR "GDAL ${GDAL_VERSION} found; 3.0 or newer is required "
                      "(axis order and PROJ 6 support). See docs/adr/0011-gdal-and-proj.md.")
endif()

if(PROJ_VERSION VERSION_LESS 6.0)
  message(FATAL_ERROR "PROJ ${PROJ_VERSION} found; 6.0 or newer is required "
                      "(proj.db and the modern API). See docs/adr/0011-gdal-and-proj.md.")
endif()

# Where proj.db actually is, which is not always where PROJ will look for it.
#
# PROJ 6 finds its coordinate operation database through a search path baked in
# when it was built. A package manager that builds in one directory and installs
# into another leaves a PROJ that links, runs, and cannot resolve a single CRS,
# pointing at a directory that never existed on this machine. vcpkg on Windows
# does exactly that: proj.db sits in installed/x64-windows/share/proj while the
# library looks in %LOCALAPPDATA%\proj.
#
# Finding the file at configure time lets the test suite set PROJ_DATA for
# itself, instead of every developer having to know to set it. Note what this
# does not do: the packaged application gets no such help, and making sure a
# shipped Paddock finds proj.db is a separate problem - task #28 - that a unit
# test was never going to answer.
find_file(
  PADDOCK_PROJ_DB
  NAMES proj.db
  HINTS "${PROJ_DIR}" "${PROJ_DIR}/.." "${PROJ_DIR}/../.." "${PROJ_DIR}/../../.."
        "${PROJ_DIR}/../../../.."
  PATH_SUFFIXES "" proj share/proj
  DOC "PROJ's coordinate operation database")

if(PADDOCK_PROJ_DB)
  get_filename_component(_paddock_proj_data_dir "${PADDOCK_PROJ_DB}" DIRECTORY)
  set(PADDOCK_PROJ_DATA_DIR
      "${_paddock_proj_data_dir}"
      CACHE INTERNAL "Directory holding proj.db, for the test environment")
  message(STATUS "  gis: proj.db at ${PADDOCK_PROJ_DB}")
else()
  message(STATUS "  gis: proj.db not found near ${PROJ_DIR}; PROJ's own search path will be used")
endif()

add_library(paddock_gis STATIC src/Module.cpp src/Environment.cpp src/Projection.cpp)
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
