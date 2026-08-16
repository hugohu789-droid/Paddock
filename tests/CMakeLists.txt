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
  unit/RasterTest.cpp
  unit/RngTest.cpp
  unit/Sha256Test.cpp
  unit/SimulationClockTest.cpp
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

# Conservation suite: closed system, 365 simulated days, budgets closing to
# 1e-9. Never skipped, never weakened, never marked flaky.
add_executable(paddock_conservation_tests conservation/ClosedSystemTest.cpp
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
