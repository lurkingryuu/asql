# Unit tests for authorization plugin helpers and cedar plugin non-network logic

MYSQL_ADD_EXECUTABLE(authorization-t
  authorization-t.cc
  ${CMAKE_SOURCE_DIR}/plugin/authorization/authorization_common.cc
  ${CMAKE_SOURCE_DIR}/plugin/authorization/cedar_authorization.cc
  ENABLE_EXPORTS
  ADD_TEST authorization-t
  LINK_LIBRARIES
    gunit_large server_unittest_library
)

if(JSONCPP_FOUND)
  target_include_directories(authorization-t PRIVATE ${JSONCPP_INCLUDE_DIRS})
  TARGET_LINK_LIBRARIES(authorization-t curl ${JSONCPP_LIBRARIES})
else()
  target_include_directories(authorization-t PRIVATE /usr/include/jsoncpp)
  TARGET_LINK_LIBRARIES(authorization-t curl jsoncpp)
endif()


