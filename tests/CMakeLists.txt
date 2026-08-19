include(PaddockGoogleTest)
paddock_provide_googletest()
include(GoogleTest)

# Unit and property suites. Labelled "fast": the pre-commit hook runs exactly
# this set and it must stay under ten seconds.
add_executable(
  paddock_unit_tests
  unit/AnimalEnergyTest.cpp
  unit/BudgetLedgerTest.cpp
  unit/DistributionsTest.cpp
  unit/EntityTest.cpp
  unit/FarmerTest.cpp
  unit/FarmletGridTest.cpp
  unit/GeometryTest.cpp
  unit/GrazingCalendarTest.cpp
  unit/GrazingTest.cpp
  unit/LiveweightResponseTest.cpp
  unit/PaddockMaskTest.cpp
  unit/PastureTest.cpp
  unit/RasterTest.cpp
  unit/RngTest.cpp
  unit/Sha256Test.cpp
  unit/SimulationClockTest.cpp
  unit/SimulationTest.cpp
  unit/SnapshotWeatherTest.cpp
  unit/SlopeRadiationTest.cpp
  unit/SoilWaterTest.cpp
  unit/SolarSlopeTest.cpp
  unit/SolarTest.cpp
  unit/SyntheticTerrainTest.cpp
  unit/SyntheticWeatherTest.cpp
  unit/TopographyTest.cpp
  unit/VersionTest.cpp
  unit/WeatherTest.cpp
  property/DeterminismTest.cpp)

target_link_libraries(paddock_unit_tests PRIVATE Paddock::core GTest::gtest_main
                                                 Paddock::project_options)

# So that suites can include "support/..." helpers by a stable path.
target_include_directories(paddock_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The escaped semicolon keeps both labels on every discovered test: without it
# CMake splits the list and only the first label survives into CTest.
gtest_discover_tests(paddock_unit_tests PROPERTIES LABELS "unit\;fast" DISCOVERY_TIMEOUT 120)

# Statistical shape suites: many draws checked against the distribution they
# come from. Labelled "statistical" and deliberately outside the "fast" subset -
# CI runs them on every pull request, but the pre-commit hook must stay quick
# enough that nobody starts bypassing it.
add_executable(paddock_statistical_tests statistical/DistributionShapeTest.cpp
                                         statistical/WeatherShapeTest.cpp)

target_link_libraries(paddock_statistical_tests PRIVATE Paddock::core GTest::gtest_main
                                                        Paddock::project_options)

target_include_directories(paddock_statistical_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(paddock_statistical_tests PROPERTIES LABELS "unit\;statistical"
                     DISCOVERY_TIMEOUT 120)

# Conservation suite: closed system, 365 simulated days, budgets closing to
# 1e-9. Never skipped, never weakened, never marked flaky.
add_executable(paddock_conservation_tests conservation/ClosedSystemTest.cpp
                                          conservation/FarmBalanceTest.cpp
                                          conservation/PastureBalanceTest.cpp
                                          conservation/TerrainBalanceTest.cpp
                                          conservation/WaterBalanceTest.cpp)

target_link_libraries(paddock_conservation_tests PRIVATE Paddock::core GTest::gtest_main
                                                         Paddock::project_options)

target_include_directories(paddock_conservation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(paddock_conservation_tests PROPERTIES LABELS "conservation"
                     DISCOVERY_TIMEOUT 120)

# Smoke test for the packaged entry point: the executable has to start on every
# platform in the matrix, not just link.
if(PADDOCK_BUILD_APP)
  add_test(NAME app_reports_its_version COMMAND paddock --version)
  set_tests_properties(app_reports_its_version PROPERTIES LABELS "unit;fast")
endif()

# config/ has its own suite because it links a dependency core does not have.
# The data files committed under data/ are loaded by it, so a definition that
# stops parsing fails the build rather than the first run that needs it.
if(PADDOCK_BUILD_CONFIG)
  add_executable(paddock_config_tests config/ConfigTest.cpp config/DataFilesTest.cpp
                                      config/FarmConfigTest.cpp config/ScenarioTest.cpp
                                      config/SpeciesConfigTest.cpp)

  target_link_libraries(paddock_config_tests PRIVATE Paddock::config GTest::gtest_main
                                                     Paddock::project_options)

  target_compile_definitions(paddock_config_tests
                             PRIVATE PADDOCK_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

  target_include_directories(paddock_config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  gtest_discover_tests(paddock_config_tests PROPERTIES LABELS "unit\;fast" DISCOVERY_TIMEOUT 120)
endif()

# T3: validation against measured data.
#
# Labelled "validation" and outside the fast subset. It compares the example
# scenario's seasonal growth with DairyNZ's published averages and writes the
# comparison series; CI turns that into a plot and keeps it as an artifact, so
# the gap between model and measurement is visible on every pull request.
if(PADDOCK_BUILD_CONFIG)
  set(PADDOCK_VALIDATION_OUTPUT_DIR "${CMAKE_BINARY_DIR}/validation")
  file(MAKE_DIRECTORY "${PADDOCK_VALIDATION_OUTPUT_DIR}")

  add_executable(paddock_validation_tests validation/GrazedYearTest.cpp
                                          validation/GrazingSystemComparisonTest.cpp
                                          validation/ManagedFarmTest.cpp
                                          validation/ScenarioReportTest.cpp
                                          validation/LivestockCalibrationTest.cpp
                                          validation/SeasonalGrowthTest.cpp
                                          validation/TerrainReachesTheModelTest.cpp)

  target_link_libraries(paddock_validation_tests PRIVATE Paddock::config GTest::gtest_main
                                                         Paddock::project_options)

  target_include_directories(paddock_validation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  target_compile_definitions(
    paddock_validation_tests
    PRIVATE PADDOCK_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
            PADDOCK_VALIDATION_OUTPUT_DIR="${PADDOCK_VALIDATION_OUTPUT_DIR}")

  gtest_discover_tests(paddock_validation_tests PROPERTIES LABELS "validation"
                       DISCOVERY_TIMEOUT 120)
endif()

# The colour mapping, tested without a display: which value gets which colour is
# the part of a map view that can be measurably wrong.
if(PADDOCK_BUILD_VIZ)
  add_executable(paddock_viz_tests viz/ColourScaleTest.cpp)

  target_link_libraries(paddock_viz_tests PRIVATE Paddock::viz GTest::gtest_main
                                                  Paddock::project_options)

  gtest_discover_tests(paddock_viz_tests PROPERTIES LABELS "unit\;fast" DISCOVERY_TIMEOUT 120)
endif()

# Where the map puts things, which is the other way a map view can be
# measurably wrong and needs VTK to check. Still no display: MapScene builds its
# data on construction and fills it in show(), and only wants a render window
# when somebody wants pixels.
if(PADDOCK_BUILD_GUI)
  add_executable(paddock_map_scene_tests viz/MapSceneTest.cpp viz/MobMarkersTest.cpp
                                        viz/TerrainSceneTest.cpp)

  target_link_libraries(paddock_map_scene_tests PRIVATE Paddock::viz_vtk GTest::gtest_main
                                                        Paddock::project_options)

  vtk_module_autoinit(TARGETS paddock_map_scene_tests MODULES ${VTK_LIBRARIES})

  # PRE_TEST, not the default POST_BUILD: discovery runs the binary, and on
  # Windows this one cannot start until Qt's and VTK's DLLs are on PATH, which
  # is a thing about running it rather than about building it. Deferring
  # discovery to test time keeps `cmake --build` from failing on a machine that
  # can compile the map perfectly well.
  gtest_discover_tests(paddock_map_scene_tests DISCOVERY_MODE PRE_TEST
                       PROPERTIES LABELS "unit\;fast" DISCOVERY_TIMEOUT 120)
endif()

# Whether this machine's GDAL and PROJ can do NZTM at all. Labelled "gis"
# rather than "fast" because the suite only exists when the geospatial stack is
# built, and a developer without it should not see failures.
if(PADDOCK_BUILD_GIS)
  add_executable(
    paddock_gis_tests gis/EnvironmentTest.cpp gis/GeoPackageParcelsTest.cpp
                      gis/GeoTiffElevationTest.cpp gis/ProjectionTest.cpp)

  target_link_libraries(paddock_gis_tests PRIVATE Paddock::gis GTest::gtest_main
                                                  Paddock::project_options)

  # PROJ_DATA comes from where gis/CMakeLists.txt found proj.db. Without it the
  # suite passes on a distribution-packaged PROJ and fails on a vcpkg one, for a
  # reason that has nothing to do with this project's code - and a suite whose
  # result depends on remembering to export a variable is a suite people learn
  # to ignore.
  if(PADDOCK_PROJ_DATA_DIR)
    gtest_discover_tests(
      paddock_gis_tests PROPERTIES LABELS "unit\;gis" ENVIRONMENT
                                   "PROJ_DATA=${PADDOCK_PROJ_DATA_DIR}" DISCOVERY_TIMEOUT 120)
  else()
    gtest_discover_tests(paddock_gis_tests PROPERTIES LABELS "unit\;gis" DISCOVERY_TIMEOUT 120)
  endif()
endif()
