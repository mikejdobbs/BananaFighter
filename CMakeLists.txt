cmake_minimum_required(VERSION 3.16)
project(fighter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNINGS -Wall -Wextra)
if(MSVC)
  set(WARNINGS /W3)
endif()

# iOS is APPLE + CMAKE_SYSTEM_NAME iOS (set via toolchain / -DCMAKE_SYSTEM_NAME=iOS).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(BF_IOS TRUE)
else()
  set(BF_IOS FALSE)
endif()

find_package(Threads REQUIRED)

# libsodium: pkg-config on desktop Unix; SODIUM_ROOT / vcpkg on Windows and iOS.
set(SODIUM_TARGET "")
if(WIN32)
  find_package(unofficial-sodium CONFIG QUIET)
  if(unofficial-sodium_FOUND)
    set(SODIUM_TARGET unofficial-sodium::sodium)
  else()
    find_path(SODIUM_INCLUDE_DIR sodium.h
      HINTS ${SODIUM_ROOT} ENV SODIUM_ROOT
      PATH_SUFFIXES include)
    find_library(SODIUM_LIBRARY
      NAMES libsodium sodium
      HINTS ${SODIUM_ROOT} ENV SODIUM_ROOT
      PATH_SUFFIXES lib)
    if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
      message(FATAL_ERROR
        "libsodium not found. Install via vcpkg (libsodium) or set SODIUM_ROOT.")
    endif()
    add_library(fighter_sodium INTERFACE)
    target_include_directories(fighter_sodium INTERFACE ${SODIUM_INCLUDE_DIR})
    target_link_libraries(fighter_sodium INTERFACE ${SODIUM_LIBRARY})
    set(SODIUM_TARGET fighter_sodium)
  endif()
elseif(BF_IOS)
  # Headers-only INTERFACE here. Static .a paths are attached per-SDK on the
  # final app target (see BF_IOS_XCF below) so one Xcode project can Run on
  # both iphoneos and iphonesimulator — CMake cannot bake a single .a path.
  set(_bf_sodium_xcf "${CMAKE_SOURCE_DIR}/deps/ios/libsodium.xcframework")
  set(BF_IOS_SODIUM_DEV "")
  set(BF_IOS_SODIUM_SIM "")
  if(EXISTS "${_bf_sodium_xcf}/ios-arm64/libsodium.a" AND
     EXISTS "${_bf_sodium_xcf}/ios-arm64-simulator/libsodium.a")
    message(STATUS "iOS libsodium: ${_bf_sodium_xcf}")
    set(BF_IOS_SODIUM_DEV "${_bf_sodium_xcf}/ios-arm64/libsodium.a")
    set(BF_IOS_SODIUM_SIM "${_bf_sodium_xcf}/ios-arm64-simulator/libsodium.a")
    add_library(fighter_sodium INTERFACE)
    target_include_directories(fighter_sodium INTERFACE
      "${CMAKE_SOURCE_DIR}/deps/ios/iphoneos/include")
    set(SODIUM_TARGET fighter_sodium)
  else()
    if(NOT SODIUM_ROOT AND DEFINED ENV{SODIUM_ROOT})
      set(SODIUM_ROOT "$ENV{SODIUM_ROOT}")
    endif()
    if(NOT SODIUM_ROOT AND EXISTS "${CMAKE_SOURCE_DIR}/deps/ios/iphoneos/include/sodium.h")
      set(SODIUM_ROOT "${CMAKE_SOURCE_DIR}/deps/ios/iphoneos")
    endif()
    find_path(SODIUM_INCLUDE_DIR sodium.h
      HINTS ${SODIUM_ROOT}
      PATH_SUFFIXES include
      NO_CMAKE_FIND_ROOT_PATH)
    find_library(SODIUM_LIBRARY
      NAMES libsodium sodium
      HINTS ${SODIUM_ROOT}
      PATH_SUFFIXES lib
      NO_CMAKE_FIND_ROOT_PATH)
    if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
      message(FATAL_ERROR
        "iOS libsodium not found.\n"
        "  Build XCFrameworks (device + Simulator in one project):\n"
        "    ./tools/build_ios_xcframeworks.sh --build")
    endif()
    message(STATUS "iOS libsodium: ${SODIUM_LIBRARY}")
    add_library(fighter_sodium INTERFACE)
    target_include_directories(fighter_sodium INTERFACE ${SODIUM_INCLUDE_DIR})
    target_link_libraries(fighter_sodium INTERFACE ${SODIUM_LIBRARY})
    set(SODIUM_TARGET fighter_sodium)
  endif()
  unset(_bf_sodium_xcf)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  set(SODIUM_TARGET PkgConfig::SODIUM)
endif()

# Headers shared by the client and server (listed so they show up in Xcode)
set(SHARED_HEADERS
  sim.h
  config.h
  crypto_session.h
  platform.h)

set(SERVER_HEADERS
  server/server.h
  server/ai.h
  server/embed.h)

# Server logic lives once here; dedicated binary and client both link it.
add_library(fighter_server STATIC
  platform.c
  crypto_session.c
  server/embed.c
  server/util.c
  server/state.c
  server/combat.c
  server/sim.c
  server/net.c
  ${SERVER_HEADERS}
  ${SHARED_HEADERS})
target_include_directories(fighter_server PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_compile_options(fighter_server PRIVATE ${WARNINGS})
target_link_libraries(fighter_server PUBLIC Threads::Threads ${SODIUM_TARGET})
if(WIN32)
  target_link_libraries(fighter_server PUBLIC ws2_32)
endif()

# Dedicated server binary: desktop only (iOS ships the embedded client).
if(NOT BF_IOS)
  add_executable(server server/main.c)
  target_link_libraries(server PRIVATE fighter_server)
  target_compile_options(server PRIVATE ${WARNINGS})
endif()

# ---------------------------------------------------------------------------
# Server tests: Unix-only (fork/waitpid harness). Skip on Windows and iOS.
# ---------------------------------------------------------------------------
if(NOT WIN32 AND NOT BF_IOS)
  enable_testing()

  add_executable(server_test tests/test.c crypto_session.c platform.c)
  target_compile_options(server_test PRIVATE ${WARNINGS})
  target_link_libraries(server_test PRIVATE ${SODIUM_TARGET})
  target_include_directories(server_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_dependencies(server_test server)

  add_test(NAME server_protocol
    COMMAND server_test -s $<TARGET_FILE:server>)

  add_test(NAME server_udp_timeouts
    COMMAND server_test -s $<TARGET_FILE:server>
            udp_timeout_drops_player udp_keepalive_prevents_timeout)

  set_tests_properties(server_protocol server_udp_timeouts PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120)
  set_tests_properties(server_udp_timeouts PROPERTIES
    LABELS slow)
endif()

# SDL3: on iOS prefer XCFramework (device + Simulator). Never use Homebrew.
# On desktop, prefer the normal CONFIG package.
set(SDL3_FOUND FALSE)
if(BF_IOS)
  set(_bf_sdl_xcf "${CMAKE_SOURCE_DIR}/deps/ios/SDL3.xcframework")
  set(BF_IOS_SDL_DEV "")
  set(BF_IOS_SDL_SIM "")
  if(EXISTS "${_bf_sdl_xcf}/ios-arm64/libSDL3.a" AND
     EXISTS "${_bf_sdl_xcf}/ios-arm64-simulator/libSDL3.a")
    add_library(SDL3::SDL3 INTERFACE IMPORTED GLOBAL)
    set_property(TARGET SDL3::SDL3 PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES
      "${CMAKE_SOURCE_DIR}/deps/ios/SDL3-iphoneos/include")
    set(BF_IOS_SDL_DEV "${_bf_sdl_xcf}/ios-arm64/libSDL3.a")
    set(BF_IOS_SDL_SIM "${_bf_sdl_xcf}/ios-arm64-simulator/libSDL3.a")
    set(SDL3_FOUND TRUE)
    message(STATUS "iOS SDL3: ${_bf_sdl_xcf}")
  else()
    if(NOT SDL3_DIR AND DEFINED ENV{SDL3_DIR})
      set(SDL3_DIR "$ENV{SDL3_DIR}")
    endif()
    if(NOT SDL3_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/deps/ios/SDL3-iphoneos/include/SDL3/SDL.h")
      set(SDL3_DIR "${CMAKE_SOURCE_DIR}/deps/ios/SDL3-iphoneos")
    endif()
    if(SDL3_DIR AND EXISTS "${SDL3_DIR}/lib/cmake/SDL3/SDL3Config.cmake")
      find_package(SDL3 CONFIG QUIET PATHS "${SDL3_DIR}/lib/cmake/SDL3" NO_DEFAULT_PATH)
    endif()
    if(NOT SDL3_FOUND)
      find_path(SDL3_INCLUDE_DIR SDL3/SDL.h
        HINTS ${SDL3_DIR} ENV SDL3_DIR
        PATH_SUFFIXES include
        NO_CMAKE_FIND_ROOT_PATH)
      find_library(SDL3_LIBRARY
        NAMES SDL3
        HINTS ${SDL3_DIR} ENV SDL3_DIR
        PATH_SUFFIXES lib
        NO_CMAKE_FIND_ROOT_PATH)
      if(SDL3_INCLUDE_DIR AND SDL3_LIBRARY)
        add_library(SDL3::SDL3 UNKNOWN IMPORTED)
        set_target_properties(SDL3::SDL3 PROPERTIES
          IMPORTED_LOCATION "${SDL3_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}")
        set(SDL3_FOUND TRUE)
        message(STATUS "iOS SDL3: ${SDL3_LIBRARY}")
      endif()
    endif()
  endif()
  unset(_bf_sdl_xcf)
  if(NOT SDL3_FOUND)
    message(WARNING
      "iOS SDL3 not found.\n"
      "  Build XCFrameworks (device + Simulator):\n"
      "    ./tools/build_ios_xcframeworks.sh --build")
  endif()
else()
  find_package(SDL3 CONFIG QUIET)
endif()

if(SDL3_FOUND)
  file(GLOB CLIENT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/*.h)

  add_executable(client
    client/main.c
    client/audio.c
    client/input.c
    client/lan.c
    client/network.c
    client/render.c
    client/settings.c
    ${CLIENT_HEADERS}
    ${SHARED_HEADERS})
  target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} client)
  target_compile_options(client PRIVATE ${WARNINGS})
  target_link_libraries(client PRIVATE SDL3::SDL3 fighter_server)
  if(UNIX AND NOT APPLE)
    target_link_libraries(client PRIVATE m)
  endif()
  if(BF_IOS)
    target_compile_definitions(client PRIVATE BF_IOS=1)
    # Frameworks required by a static SDL3.a (from SDL's iOS sdl3.pc).
    target_link_libraries(client PRIVATE
      "-framework UIKit"
      "-framework Foundation"
      "-framework CoreFoundation"
      "-framework CoreGraphics"
      "-framework QuartzCore"
      "-framework Metal"
      "-framework OpenGLES"
      "-framework AudioToolbox"
      "-framework AVFoundation"
      "-framework CoreAudio"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework CoreBluetooth"
      "-framework CoreMotion"
      "-framework GameController"
      "-framework CoreHaptics"
      "-framework UserNotifications"
      "-framework Security"
      "-lpthread"
      "-lm")
    # Per-SDK static libs: Xcode substitutes the matching slice at link time.
    if(BF_IOS_SDL_DEV AND BF_IOS_SDL_SIM AND BF_IOS_SODIUM_DEV AND BF_IOS_SODIUM_SIM)
      set_target_properties(client PROPERTIES
        XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphoneos*]
          "${BF_IOS_SDL_DEV} ${BF_IOS_SODIUM_DEV} $(inherited)"
        XCODE_ATTRIBUTE_OTHER_LDFLAGS[sdk=iphonesimulator*]
          "${BF_IOS_SDL_SIM} ${BF_IOS_SODIUM_SIM} $(inherited)")
    endif()
  endif()

  # Windows: embed icons/icon.ico into the .exe
  if(WIN32)
    set(BANANA_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/icons/icon.ico")
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/icons/client.rc.in
      ${CMAKE_CURRENT_BINARY_DIR}/client.rc @ONLY)
    target_sources(client PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/client.rc)
  endif()

  # Apple (macOS + iOS): app bundle; assets live in Resources/
  if(APPLE)
    set_target_properties(client PROPERTIES
      MACOSX_BUNDLE TRUE
      OUTPUT_NAME BananaFighter
      MACOSX_BUNDLE_BUNDLE_NAME "BananaFighter"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.bananafighter.client"
      MACOSX_BUNDLE_INFO_STRING "BananaFighter"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
      MACOSX_BUNDLE_BUNDLE_VERSION "1"
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.bananafighter.client"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
      XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0")

    if(BF_IOS)
      # Prefer -DBF_DEVELOPMENT_TEAM=... or env BF_DEVELOPMENT_TEAM so CMake
      # regenerate does not wipe a team chosen for signing / Run.
      if(NOT BF_DEVELOPMENT_TEAM AND DEFINED ENV{BF_DEVELOPMENT_TEAM})
        set(BF_DEVELOPMENT_TEAM "$ENV{BF_DEVELOPMENT_TEAM}")
      endif()
      set(_bf_ios_attrs
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_INFOPLIST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE NO
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.bananafighter.client"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
        XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator")
      if(BF_DEVELOPMENT_TEAM)
        list(APPEND _bf_ios_attrs
          XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${BF_DEVELOPMENT_TEAM}")
      endif()
      set_target_properties(client PROPERTIES ${_bf_ios_attrs})
      unset(_bf_ios_attrs)
    else()
      set_target_properties(client PROPERTIES
        MACOSX_BUNDLE_ICON_FILE BananaFighter.icns)
      set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/BananaFighter.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
      target_sources(client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/BananaFighter.icns)
    endif()

    # Bundle asset layout:
    #   macOS → Contents/Resources/{sprites,sounds,icons}
    #   iOS   → .app/{sprites,sounds,icons}  (a top-level Resources/ folder is
    #           forbidden and makes CFBundle return an empty Info.plist, which
    #           surfaces as IXErrorDomain "Missing bundle ID" on sim install)
    if(BF_IOS)
      set(_bf_asset_root "")
    else()
      set(_bf_asset_root "Resources/")
    endif()
    foreach(dir sprites sounds)
      file(GLOB _bf_dir_files CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*")
      foreach(_bf_f IN LISTS _bf_dir_files)
        if(IS_DIRECTORY "${_bf_f}")
          continue()
        endif()
        set_source_files_properties("${_bf_f}" PROPERTIES
          MACOSX_PACKAGE_LOCATION "${_bf_asset_root}${dir}")
        target_sources(client PRIVATE "${_bf_f}")
      endforeach()
    endforeach()
    set_source_files_properties(
      "${CMAKE_CURRENT_SOURCE_DIR}/icons/icon.png"
      PROPERTIES MACOSX_PACKAGE_LOCATION "${_bf_asset_root}icons")
    target_sources(client PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/icons/icon.png")
    unset(_bf_asset_root)
    unset(_bf_dir_files)
    unset(_bf_f)
  else()
    foreach(dir sprites sounds)
      add_custom_command(TARGET client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
                $<TARGET_FILE_DIR:client>/${dir})
    endforeach()
    add_custom_command(TARGET client POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:client>/icons
      COMMAND ${CMAKE_COMMAND} -E copy
              ${CMAKE_CURRENT_SOURCE_DIR}/icons/icon.png
              $<TARGET_FILE_DIR:client>/icons/icon.png)
  endif()
else()
  message(WARNING "SDL3 not found: building the server only")
endif()
