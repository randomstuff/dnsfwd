cmake_minimum_required (VERSION 2.8.11)
project(dnsfwd)

include(CheckSymbolExists)

check_symbol_exists(SO_DOMAIN "sys/socket.h" HAVE_SO_DOMAIN)
if(HAVE_SO_DOMAIN)
  add_definitions(-DHAVE_SO_DOMAIN)
endif()

check_symbol_exists(SO_TYPE "sys/socket.h" HAVE_SO_TYPE)
if(HAVE_SO_TYPE)
  add_definitions(-DHAVE_SO_TYPE)
endif()

option(USE_SYSTEMD "Link against libsystemd" OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

add_executable(dnsfwd
  src/dnsfwd.cpp
  src/client.cpp
  src/server.cpp
  src/service.cpp
  src/config.cpp
  )
target_link_libraries(dnsfwd boost_system boost_program_options pthread)

if(USE_SYSTEMD)
  add_definitions(-DUSE_SYSTEMD)
  target_link_libraries(dnsfwd systemd)
  configure_file(systemd/dnsfwd.socket dnsfwd.socket)
  configure_file(systemd/dnsfwd.service dnsfwd.service)
endif()

install(TARGETS dnsfwd DESTINATION bin)
if(USE_SYSTEMD)
  install(FILES dnsfwd.service DESTINATION lib/systemd/system)
  install(FILES dnsfwd.socket DESTINATION lib/systemd/system)
endif()
