cmake_minimum_required (VERSION 2.8.11)
project(dnsovc)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

option(USE_SYSTEMD "Compile with systemd integration" OFF)

add_executable(dnsfwd
  src/dnsfwd.cpp
  src/client.cpp
  src/server.cpp
  src/service.cpp
  )
target_link_libraries(dnsfwd boost_system pthread)

if(USE_SYSTEMD)
  add_definitions(-DUSE_SYSTEMD)
  target_link_libraries(dnsfwd boost_system systemd)
  configure_file(systemd/dnsfwd.service dnsfwd.service)
  configure_file(systemd/dnsfwd.socket dnsfwd.socket)
endif()

install(TARGETS dnsfwd DESTINATION bin)
install(FILES dnsfwd.service DESTINATION lib/systemd)
install(FILES dnsfwd.socket DESTINATION lib/systemd)
