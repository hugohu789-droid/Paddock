# config/ turns validated TOML into the plain parameter structs core works
# with. toml++ stops here: core never sees a TOML type, and never parses text.
#
# See docs/adr/0009-config-module.md for why this module exists at all.

include(PaddockTomlPlusPlus)
paddock_provide_tomlplusplus()

add_library(
  paddock_config STATIC
  src/ConfigError.cpp
  src/FarmConfig.cpp
  src/PastureConfig.cpp
  src/ScenarioConfig.cpp
  src/SoilConfig.cpp
  src/TomlSupport.cpp
  src/WeatherConfig.cpp)
add_library(Paddock::config ALIAS paddock_config)

target_include_directories(
  paddock_config PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_config PUBLIC Paddock::core)
target_link_libraries(paddock_config PRIVATE tomlplusplus::tomlplusplus Paddock::project_options)
