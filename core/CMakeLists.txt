# core/ is pure C++17 + stdlib. No find_package, no link libraries, ever.
# scripts/check-dependency-direction.sh fails CI if that changes.

add_library(
  paddock_core STATIC
  src/AnimalEnergy.cpp
  src/BudgetLedger.cpp
  src/DataSource.cpp
  src/Distributions.cpp
  src/Entity.cpp
  src/Farm.cpp
  src/FarmAccount.cpp
  src/FarmDecision.cpp
  src/Farmer.cpp
  src/FeedStore.cpp
  src/FarmletGrid.cpp
  src/Flock.cpp
  src/Geometry.cpp
  src/Grazing.cpp
  src/GrazingCalendar.cpp
  src/Mycotoxin.cpp
  src/PaddockMask.cpp
  src/Pasture.cpp
  src/SimulationClock.cpp
  src/SlopeRadiation.cpp
  src/Irrigation.cpp
  src/SoilWater.cpp
  src/Solar.cpp
  src/Rng.cpp
  src/SupplementMarket.cpp
  src/Sha256.cpp
  src/Simulation.cpp
  src/SnapshotWeather.cpp
  src/SyntheticTerrain.cpp
  src/SyntheticWeather.cpp
  src/Topography.cpp
  src/Version.cpp
  src/Weather.cpp)

add_library(Paddock::core ALIAS paddock_core)

target_include_directories(
  paddock_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(paddock_core PUBLIC cxx_std_17)

target_link_libraries(paddock_core PRIVATE Paddock::project_options)

set_target_properties(paddock_core PROPERTIES EXPORT_NAME core)
