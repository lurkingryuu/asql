# Copyright (c) 2025, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# Embedded Cedar Authorization Plugin
#
# Links against libcedar (Rust → C ABI) for in-process Cedar evaluation.
# libcedar.a is built from the libcedar/ workspace in this repository.
# Set LIBCEDAR_DIR at cmake time or rely on the default path below.
#
# Typical build (from asql/build/):
#   cmake .. -DLIBCEDAR_DIR=/path/to/libcedar
#   make embedded_cedar

# ---- Locate libcedar -------------------------------------------------------

if(NOT DEFINED LIBCEDAR_DIR)
  # Default: sibling directory relative to the MySQL source root
  set(LIBCEDAR_DIR "${CMAKE_SOURCE_DIR}/../libcedar"
      CACHE PATH "Root of the libcedar Rust workspace")
endif()

set(LIBCEDAR_HEADER "${LIBCEDAR_DIR}/include/libcedar.h")
set(LIBCEDAR_STATIC "${LIBCEDAR_DIR}/target/release/libcedar.a")

if(NOT EXISTS "${LIBCEDAR_HEADER}")
  message(WARNING
    "embedded_cedar: libcedar.h not found at ${LIBCEDAR_HEADER}. "
    "Build libcedar first: cd ${LIBCEDAR_DIR} && cargo build --release. "
    "Plugin will be skipped.")
  return()
endif()

if(NOT EXISTS "${LIBCEDAR_STATIC}")
  message(WARNING
    "embedded_cedar: libcedar.a not found at ${LIBCEDAR_STATIC}. "
    "Build libcedar first: cd ${LIBCEDAR_DIR} && cargo build --release. "
    "Plugin will be skipped.")
  return()
endif()

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
    -Wl,--whole-archive "${LIBCEDAR_STATIC}" -Wl,--no-whole-archive
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

message(STATUS "embedded_cedar: using libcedar from ${LIBCEDAR_DIR}")
