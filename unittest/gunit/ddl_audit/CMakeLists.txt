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

# Unit tests for ddl_audit plugin helper functions

MYSQL_ADD_EXECUTABLE(ddl_audit-t
  ddl_audit-t.cc
  ${CMAKE_SOURCE_DIR}/plugin/ddl_audit/ddl_audit.cc
  ENABLE_EXPORTS
  ADD_TEST ddl_audit-t
  LINK_LIBRARIES
    gunit_large server_unittest_library
)

# Link against curl and jsoncpp for ddl_audit.cc
if(JSONCPP_FOUND)
  target_include_directories(ddl_audit-t PRIVATE ${JSONCPP_INCLUDE_DIRS})
  TARGET_LINK_LIBRARIES(ddl_audit-t curl ${JSONCPP_LIBRARIES})
else()
  # Fallback include path commonly used on Debian/Ubuntu
  target_include_directories(ddl_audit-t PRIVATE /usr/include/jsoncpp)
  TARGET_LINK_LIBRARIES(ddl_audit-t curl jsoncpp)
endif()

