# Copyright (c) 2025, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is designed to work with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have either included with
# the program or referenced in the documentation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# Authorization plugins

include(ExternalProject)

# Simple authorization plugin (basic example)
MYSQL_ADD_PLUGIN(simple_authorization
  simple_authorization.cc
  MODULE_ONLY
  MODULE_OUTPUT_NAME "simple_authorization")

# External authorization plugin (advanced example with HTTP)
# Requires curl and jsoncpp (present in Dockerfile build image)
MYSQL_ADD_PLUGIN(external_authorization 
  external_authorization.cc
  MODULE_ONLY
  MODULE_OUTPUT_NAME "external_authorization")

# Ensure jsoncpp headers are found (Ubuntu installs under /usr/include/jsoncpp)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(JSONCPP jsoncpp)
endif()

# Embedded Cedar authorization plugin (in-process via libcedar)
# By default, libcedar is fetched into the build tree and built with cargo.
# Override LIBCEDAR_DIR to point at an existing checkout instead.
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

# Link against curl and jsoncpp; add include dirs for jsoncpp
if(JSONCPP_FOUND)
  target_include_directories(external_authorization PRIVATE ${JSONCPP_INCLUDE_DIRS})
  TARGET_LINK_LIBRARIES(external_authorization curl ${JSONCPP_LIBRARIES})
else()
  # Fallback include path commonly used on Debian/Ubuntu
  target_include_directories(external_authorization PRIVATE /usr/include/jsoncpp)
  TARGET_LINK_LIBRARIES(external_authorization curl jsoncpp)
endif()

# Cedar authorization plugin (Cedar policy engine integration)
# Requires curl and jsoncpp libraries
MYSQL_ADD_PLUGIN(cedar_authorization
  cedar_authorization.cc
  authorization_common.cc
  MODULE_ONLY
  MODULE_OUTPUT_NAME "cedar_authorization")

# Link against curl and jsoncpp; add include dirs for jsoncpp
if(JSONCPP_FOUND)
  target_include_directories(cedar_authorization PRIVATE ${JSONCPP_INCLUDE_DIRS})
  TARGET_LINK_LIBRARIES(cedar_authorization curl ${JSONCPP_LIBRARIES})
else()
  # Fallback include path commonly used on Debian/Ubuntu
  target_include_directories(cedar_authorization PRIVATE /usr/include/jsoncpp)
  TARGET_LINK_LIBRARIES(cedar_authorization curl jsoncpp)
endif()

MYSQL_ADD_PLUGIN(embedded_cedar
  embedded_cedar.cc
  authorization_common.cc
  MODULE_ONLY
  MODULE_OUTPUT_NAME "embedded_cedar")

target_include_directories(embedded_cedar
  PRIVATE
    "${LIBCEDAR_DIR}/include")
add_dependencies(embedded_cedar libcedar_project)

if(JSONCPP_FOUND)
  target_include_directories(embedded_cedar PRIVATE ${JSONCPP_INCLUDE_DIRS})
else()
  target_include_directories(embedded_cedar PRIVATE /usr/include/jsoncpp)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(embedded_cedar
    -Wl,--whole-archive libcedar_static -Wl,--no-whole-archive
    pthread dl m)
else()
  target_link_libraries(embedded_cedar
    "-Wl,-force_load,${LIBCEDAR_STATIC}"
    pthread dl)
endif()

if(JSONCPP_FOUND)
  target_link_libraries(embedded_cedar ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(embedded_cedar jsoncpp)
endif()

# Note: libcurl is typically built with OpenSSL support, enabling HTTPS out of the box.
# If curl was built with SSL/TLS support (--with-ssl), HTTPS will work automatically.
