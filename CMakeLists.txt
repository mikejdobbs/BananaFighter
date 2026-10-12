cmake_minimum_required(VERSION 3.16)
project(fighter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNINGS -Wall -Wextra)

find_package(SDL3 CONFIG REQUIRED)

set(BANANA_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/client
)

# Server target
add_executable(server server.c)
target_include_directories(server PRIVATE ${BANANA_INCLUDE_DIRS})
target_compile_options(server PRIVATE ${WARNINGS})

# Client target split into functional modules.
add_executable(client
    client/main.c
    client/audio.c
    client/input.c
    client/network.c
    client/state.c
)
target_include_directories(client PRIVATE ${BANANA_INCLUDE_DIRS})
target_compile_options(client PRIVATE ${WARNINGS})
target_link_libraries(client PRIVATE SDL3::SDL3)

foreach(dir sprites sounds)
  add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/${dir} $<TARGET_FILE_DIR:client>/${dir})
endforeach()

message(STATUS "Configured BananaFighter: server + split client modules")
