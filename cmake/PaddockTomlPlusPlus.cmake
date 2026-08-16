# toml++ comes from vcpkg when a manifest install is in play, and is fetched at
# a pinned commit otherwise - the same arrangement as gtest, for the same
# reason: a contributor with a compiler and network access can build everything
# without first learning a package manager.
#
# It is header-only, so this costs a download and no build time.

include(FetchContent)

macro(paddock_provide_tomlplusplus)
  find_package(tomlplusplus CONFIG QUIET)
  if(tomlplusplus_FOUND)
    message(STATUS "toml++: found via find_package")
  else()
    message(STATUS "toml++: not installed, fetching pinned v3.4.0")
    FetchContent_Declare(
      tomlplusplus
      GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
      GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de # v3.4.0
      SYSTEM EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(tomlplusplus)
  endif()
endmacro()
