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
