# gtest comes from vcpkg when a manifest install is in play, and is fetched at
# a pinned commit otherwise. Both paths expose GTest::gtest_main, so a machine
# with nothing but a compiler and network access can run the science suite.

include(FetchContent)

macro(paddock_provide_googletest)
  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    message(STATUS "gtest: found via find_package")
  else()
    message(STATUS "gtest: not installed, fetching pinned v1.18.0")
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    set(INSTALL_GTEST
        OFF
        CACHE BOOL "" FORCE)
    set(BUILD_GMOCK
        OFF
        CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG 063de7e9578f82b369302001269680b4b1553359 # v1.18.0
      SYSTEM EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(googletest)
  endif()
endmacro()
