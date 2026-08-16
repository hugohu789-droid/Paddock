include(PaddockGoogleTest)
paddock_provide_googletest()
include(GoogleTest)

# Unit and property suites. Labelled "fast": the pre-commit hook runs exactly
# this set and it must stay under ten seconds.
add_executable(
  paddock_unit_tests
  unit/BudgetLedgerTest.cpp
  unit/DistributionsTest.cpp
  unit/EntityTest.cpp
  unit/GeometryTest.cpp
  unit/PastureTest.cpp
  unit/RasterTest.cpp
  unit/RngTest.cpp
  unit/Sha256Test.cpp
  unit/SimulationClockTest.cpp
  unit/SimulationTest.cpp
  unit/SnapshotWeatherTest.cpp
  unit/SoilWaterTest.cpp
  unit/SolarTest.cpp
  unit/SyntheticWeatherTest.cpp
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
                                          conservation/PastureBalanceTest.cpp
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
                                      config/ScenarioTest.cpp)

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

  add_executable(paddock_validation_tests validation/SeasonalGrowthTest.cpp)

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
