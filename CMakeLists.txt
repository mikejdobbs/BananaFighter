cmake_minimum_required(VERSION 3.16)
project(fighter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNINGS -Wall -Wextra)

# Headers shared by the client and server (sim.h is used by both; listed so they show up in Xcode)
set(SHARED_HEADERS
  sim.h
  config.h)

set(SERVER_HEADERS
  server/server.h
  server/ai.h)

add_executable(server
  server/main.c
  server/util.c
  server/state.c
  server/combat.c
  server/sim.c
  server/net.c
  ${SERVER_HEADERS}
  ${SHARED_HEADERS})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} server)
target_compile_options(server PRIVATE ${WARNINGS})

# ---------------------------------------------------------------------------
# Server tests: a black-box client that talks to the server over TCP and UDP.
# Build with `cmake --build . --target server_test`, then run `ctest`.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(server_test tests/test.c)
target_compile_options(server_test PRIVATE ${WARNINGS})
add_dependencies(server_test server)  # the tests launch the freshly built server

# Fast tests. "-s" starts a fresh server for every test case.
add_test(NAME server_protocol
  COMMAND server_test -s $<TARGET_FILE:server>)

# Slow tests (wait past the 5 s UDP timeout). Naming them runs them even without --slow.
add_test(NAME server_udp_timeouts
  COMMAND server_test -s $<TARGET_FILE:server>
          udp_timeout_drops_player udp_keepalive_prevents_timeout)

# Both bind the fixed port from config.h, so they must not run in parallel.
set_tests_properties(server_protocol server_udp_timeouts PROPERTIES
  RUN_SERIAL TRUE
  TIMEOUT 120)
set_tests_properties(server_udp_timeouts PROPERTIES
  LABELS slow)

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  # client headers: picked up automatically, re-globbed when files are added
  file(GLOB CLIENT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/*.h)

  add_executable(client
    client/main.c
    client/audio.c
    client/input.c
    client/network.c
    client/render.c
    ${CLIENT_HEADERS}
    ${SHARED_HEADERS})
  target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} client)
  target_compile_options(client PRIVATE ${WARNINGS})
  target_link_libraries(client PRIVATE SDL3::SDL3)
  # sprites and sounds are loaded from <exe dir>/..., falling back to the current directory
  foreach(dir sprites sounds)
    add_custom_command(TARGET client POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${CMAKE_CURRENT_SOURCE_DIR}/${dir} $<TARGET_FILE_DIR:client>/${dir})
  endforeach()
else()
  message(WARNING "SDL3 not found: building the server only")
endif()
