project(vhost-user-scmi)

cmake_minimum_required(VERSION 3.10)


set (VHOST_USER_SCMI_INC_PATH ${CMAKE_CURRENT_LIST_DIR}/include/)

#enable test related macros
#add_definitions(-D__TEST__)
add_definitions(-D_GNU_SOURCE)

set(VHOST_USER_SCMI_SOURCE_FILES
  src/vhost_user_scmi.c
  src/access_control.c
  src/worker.c
  src/log.c
  src/protocols/base.c
  src/protocols/perf.c
  src/protocols/reset.c
)

SET(CMAKE_INSTALL_PREFIX "/usr/")

add_executable (vhost-user-scmi ${VHOST_USER_SCMI_SOURCE_FILES})

include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${VHOST_USER_SCMI_INC_PATH})

link_directories(${SYSROOT_LIBDIR})
link_directories(${CMAKE_INSTALL_LIBDIR})

target_link_libraries (vhost-user-scmi c)
target_link_libraries (vhost-user-scmi vhostuserlib)
target_link_libraries (vhost-user-scmi pthread)

install(TARGETS "vhost-user-scmi" DESTINATION bin)

