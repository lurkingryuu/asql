# Copyright (c) 2025, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# Embedded Cedar Authorization Plugin
#
# Links against libcedar (Rust → C ABI) for in-process Cedar evaluation.
# By default, libcedar is fetched from GitHub into the build tree and built
# with cargo. Override LIBCEDAR_DIR to point at an existing checkout instead.
#
# Typical build (from asql/build/):
#   cmake ..
#   make embedded_cedar
#
# Using an existing libcedar checkout:
#   cmake .. -DLIBCEDAR_DIR=/path/to/libcedar
#   make embedded_cedar

# ---- Locate libcedar -------------------------------------------------------

include(ExternalProject)

set(LIBCEDAR_REPO "https://github.com/lurkingryuu/libcedar.git"
    CACHE STRING "Git repository for libcedar")
set(LIBCEDAR_BRANCH "main"
    CACHE STRING "Git branch or tag for libcedar")

set(_embedded_cedar_fetch_libcedar OFF)
if(NOT DEFINED LIBCEDAR_DIR)
  set(_embedded_cedar_fetch_libcedar ON)
  set(LIBCEDAR_DIR "${CMAKE_BINARY_DIR}/external/libcedar"
      CACHE PATH "Root of the libcedar Rust workspace")
endif()

find_package(Git REQUIRED)
find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(LIBCEDAR_HEADER "${LIBCEDAR_DIR}/include/libcedar.h")
set(LIBCEDAR_STATIC "${LIBCEDAR_DIR}/target/release/libcedar.a")

if(_embedded_cedar_fetch_libcedar)
  set(_libcedar_source_args
      GIT_REPOSITORY "${LIBCEDAR_REPO}"
      GIT_TAG "${LIBCEDAR_BRANCH}"
      GIT_SHALLOW TRUE
      SOURCE_DIR "${LIBCEDAR_DIR}")
elseif(EXISTS "${LIBCEDAR_DIR}/Cargo.toml")
  set(_libcedar_source_args
      SOURCE_DIR "${LIBCEDAR_DIR}"
      DOWNLOAD_COMMAND "")
else()
  message(FATAL_ERROR
    "embedded_cedar: LIBCEDAR_DIR='${LIBCEDAR_DIR}' does not contain "
    "a libcedar checkout (missing Cargo.toml). Either point LIBCEDAR_DIR "
    "at an existing checkout or omit it to fetch from ${LIBCEDAR_REPO}.")
endif()

ExternalProject_Add(libcedar_project
  PREFIX "${CMAKE_BINARY_DIR}/external/libcedar-prefix"
  ${_libcedar_source_args}
  UPDATE_DISCONNECTED TRUE
  CONFIGURE_COMMAND ""
  BUILD_IN_SOURCE TRUE
  BUILD_COMMAND "${CARGO_EXECUTABLE}" build --release
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS
    "${LIBCEDAR_HEADER}"
    "${LIBCEDAR_STATIC}"
)

add_library(libcedar_static STATIC IMPORTED GLOBAL)
set_target_properties(libcedar_static PROPERTIES
  IMPORTED_LOCATION "${LIBCEDAR_STATIC}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBCEDAR_DIR}/include")
add_dependencies(libcedar_static libcedar_project)

# ---- jsoncpp ---------------------------------------------------------------

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(JSONCPP jsoncpp)
endif()

# ---- Plugin target ---------------------------------------------------------

MYSQL_ADD_PLUGIN(embedded_cedar
  embedded_cedar.cc
  MODULE_ONLY
  MODULE_OUTPUT_NAME "embedded_cedar")

target_include_directories(embedded_cedar
  PRIVATE
    "${LIBCEDAR_DIR}/include"
    "${CMAKE_SOURCE_DIR}/plugin/authorization"   # authorization_common.h
)
add_dependencies(embedded_cedar libcedar_project)

if(JSONCPP_FOUND)
  target_include_directories(embedded_cedar PRIVATE ${JSONCPP_INCLUDE_DIRS})
else()
  target_include_directories(embedded_cedar PRIVATE /usr/include/jsoncpp)
endif()

# ---- Linking ---------------------------------------------------------------
# Static-link libcedar.a into the plugin .so so no separate libcedar.so is
# needed in the runtime image.  Rust's libstd itself is statically included in
# libcedar.a (Rust default), so we only need OS-level libraries below.

if(UNIX AND NOT APPLE)
  target_link_libraries(embedded_cedar
    -Wl,--whole-archive libcedar_static -Wl,--no-whole-archive
    pthread dl m)
else()
  # macOS: -force_load is the --whole-archive equivalent
  target_link_libraries(embedded_cedar
    "-Wl,-force_load,${LIBCEDAR_STATIC}"
    pthread dl)
endif()

if(JSONCPP_FOUND)
  target_link_libraries(embedded_cedar ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(embedded_cedar jsoncpp)
endif()

message(STATUS
  "embedded_cedar: using libcedar from ${LIBCEDAR_DIR} "
  "(repo: ${LIBCEDAR_REPO}, ref: ${LIBCEDAR_BRANCH})")
