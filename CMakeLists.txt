cmake_minimum_required(VERSION 3.16)
project(fighter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNINGS -Wall -Wextra)

add_executable(server server.c)
target_compile_options(server PRIVATE ${WARNINGS})

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  add_executable(client
    client/main.c
    client/audio.c
    client/input.c
    client/network.c
    client/render.c)
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
