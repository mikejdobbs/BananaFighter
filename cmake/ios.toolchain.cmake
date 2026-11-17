# Optional helper for configuring an iOS build from the command line:
#
#   ./tools/build_ios_xcframeworks.sh --build   # once
#   cmake -B build-ios -G Xcode \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
#     -DBF_DEVELOPMENT_TEAM=YOUR_TEAM_ID
#
# Then open build-ios/fighter.xcodeproj and pick either a Simulator or a
# device destination. XCFrameworks under deps/ios/ supply both slices.
# Signing: set BF_DEVELOPMENT_TEAM or choose a Team in Xcode.

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "iOS deployment target")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH YES)

# Prefer arm64. Do NOT pin CMAKE_OSX_SYSROOT — leaving it unset lets the Xcode
# generator emit a single project that switches iphoneos / iphonesimulator
# from the Run destination.
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS architectures")
endif()

# Allow find_* / find_package to see prebuilt prefixes under deps/ios (not only
# the iOS SDK sysroot).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
