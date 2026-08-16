# ai/ is optional and off by default in CI (PADDOCK_WITH_AI). It generates
# configs, explains results and writes reports; every output is schema-validated
# before it enters the system, and it never mutates simulation state directly.
#
# libcurl and the JSON library (vcpkg feature "ai") arrive with the first
# feature in M6. The API key comes from the environment, never from a file.

add_library(paddock_ai STATIC src/Module.cpp)
add_library(Paddock::ai ALIAS paddock_ai)

target_include_directories(
  paddock_ai PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(paddock_ai PUBLIC Paddock::core)
target_link_libraries(paddock_ai PRIVATE Paddock::project_options)
