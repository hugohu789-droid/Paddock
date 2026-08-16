# One interface target carrying the warning and sanitizer settings. Every
# Paddock target links it PRIVATE, so the settings never leak to consumers.

add_library(paddock_project_options INTERFACE)
add_library(Paddock::project_options ALIAS paddock_project_options)

if(MSVC)
  # /EHsc is stated rather than inherited: CMake 4.x no longer injects the old
  # default MSVC flags, and standard exception semantics are not optional here.
  target_compile_options(
    paddock_project_options INTERFACE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
                                      /wd4996)
  if(PADDOCK_WARNINGS_AS_ERRORS)
    target_compile_options(paddock_project_options INTERFACE /WX)
  endif()
else()
  target_compile_options(
    paddock_project_options
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wnon-virtual-dtor
              -Wold-style-cast
              -Wcast-align
              -Wunused
              -Woverloaded-virtual
              -Wdouble-promotion
              -Wformat=2)
  if(PADDOCK_WARNINGS_AS_ERRORS)
    target_compile_options(paddock_project_options INTERFACE -Werror)
  endif()
endif()

if(PADDOCK_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "PADDOCK_SANITIZE is only supported with GCC or Clang.")
  endif()
  string(REPLACE ";" "," _paddock_sanitize_list "${PADDOCK_SANITIZE}")
  target_compile_options(
    paddock_project_options INTERFACE -fsanitize=${_paddock_sanitize_list}
                                      -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(paddock_project_options INTERFACE
                      -fsanitize=${_paddock_sanitize_list})
  unset(_paddock_sanitize_list)
endif()
