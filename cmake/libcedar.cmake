# Fetch and build libcedar (Rust -> staticlib) for in-tree consumers.
#
# This intentionally builds from source so the resulting archive matches the
# host toolchain/glibc, unlike manylinux-style prebuilt tarballs.

include(FetchContent)

set(LIBCEDAR_VERSION "v0.1.1" CACHE STRING "libcedar git tag")

if(TARGET libcedar::libcedar)
  return()
endif()

find_program(LIBCEDAR_CARGO_EXECUTABLE cargo REQUIRED)

FetchContent_Declare(
  libcedar_src
  URL "https://github.com/lurkingryuu/libcedar/archive/refs/tags/${LIBCEDAR_VERSION}.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(libcedar_src)

set(LIBCEDAR_SOURCE_DIR "${libcedar_src_SOURCE_DIR}")
set(LIBCEDAR_INCLUDE_DIR "${LIBCEDAR_SOURCE_DIR}/include")
set(LIBCEDAR_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/libcedar_cargo_target")
set(LIBCEDAR_STATIC_LIB "${LIBCEDAR_CARGO_TARGET_DIR}/release/libcedar.a")

file(GLOB_RECURSE LIBCEDAR_RUST_INPUTS CONFIGURE_DEPENDS
  "${LIBCEDAR_SOURCE_DIR}/src/*.rs"
  "${LIBCEDAR_SOURCE_DIR}/include/*.h")

list(APPEND LIBCEDAR_RUST_INPUTS
  "${LIBCEDAR_SOURCE_DIR}/Cargo.toml"
  "${LIBCEDAR_SOURCE_DIR}/build.rs"
  "${LIBCEDAR_SOURCE_DIR}/cbindgen.toml")

if(EXISTS "${LIBCEDAR_SOURCE_DIR}/Cargo.lock")
  list(APPEND LIBCEDAR_RUST_INPUTS "${LIBCEDAR_SOURCE_DIR}/Cargo.lock")
endif()

add_custom_command(
  OUTPUT "${LIBCEDAR_STATIC_LIB}"
  COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${LIBCEDAR_CARGO_TARGET_DIR}"
          "${LIBCEDAR_CARGO_EXECUTABLE}" build --release
          --manifest-path "${LIBCEDAR_SOURCE_DIR}/Cargo.toml"
  WORKING_DIRECTORY "${LIBCEDAR_SOURCE_DIR}"
  DEPENDS ${LIBCEDAR_RUST_INPUTS}
  COMMENT "Building libcedar ${LIBCEDAR_VERSION} (Rust staticlib)"
  VERBATIM)

add_custom_target(libcedar_rust DEPENDS "${LIBCEDAR_STATIC_LIB}")

add_library(libcedar::libcedar STATIC IMPORTED GLOBAL)
add_dependencies(libcedar::libcedar libcedar_rust)

set_target_properties(libcedar::libcedar PROPERTIES
  IMPORTED_LOCATION "${LIBCEDAR_STATIC_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBCEDAR_INCLUDE_DIR}")
