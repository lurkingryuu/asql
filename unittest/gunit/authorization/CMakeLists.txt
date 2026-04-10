# Unit tests for authorization plugin helpers and cedar plugin non-network logic

ADD_DEFINITIONS(-DEXTRA_CODE_FOR_UNIT_TESTING)

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

MYSQL_ADD_EXECUTABLE(embedded-cedar-t
  embedded-cedar-t.cc
  ${CMAKE_SOURCE_DIR}/plugin/authorization/authorization_common.cc
  ${CMAKE_SOURCE_DIR}/plugin/authorization/embedded_cedar.cc
  ENABLE_EXPORTS
  ADD_TEST embedded-cedar-t
  LINK_LIBRARIES
    gunit_large server_unittest_library
)

target_include_directories(embedded-cedar-t PRIVATE
  ${LIBCEDAR_DIR}/include)

if(JSONCPP_FOUND)
  target_include_directories(embedded-cedar-t PRIVATE ${JSONCPP_INCLUDE_DIRS})
else()
  target_include_directories(embedded-cedar-t PRIVATE /usr/include/jsoncpp)
endif()

if(TARGET libcedar_project)
  add_dependencies(embedded-cedar-t libcedar_project)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(embedded-cedar-t
    -Wl,--whole-archive libcedar_static -Wl,--no-whole-archive
    pthread dl m)
else()
  target_link_libraries(embedded-cedar-t
    "-Wl,-force_load,${LIBCEDAR_STATIC}"
    pthread dl)
endif()

if(JSONCPP_FOUND)
  target_link_libraries(embedded-cedar-t ${JSONCPP_LIBRARIES})
else()
  target_link_libraries(embedded-cedar-t jsoncpp)
endif()


