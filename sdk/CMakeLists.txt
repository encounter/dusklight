# Dusklight Mod SDK entry point
#
# Provides game/service headers, compile definitions, version.h and webgpu.h
# without configuring the full game tree. Dawn (WebGPU) headers and implib
# are fetched via Aurora's prebuilt Dawn packages.
#
# Usage (from a mod project):
#   add_subdirectory(<dusk>/sdk dusk-sdk EXCLUDE_FROM_ALL)
#   add_dusk_mod(my_mod SOURCES ... MOD_JSON mod.json)
#
# On Windows, pass -DDUSK_GAME_IMPLIB=<path to sdk/dusklight.lib> from the
# matching game release. TODO: auto-download from tag

cmake_minimum_required(VERSION 3.25)

if (TARGET dusklight_game_headers)
    message(FATAL_ERROR "Mod SDK already configured")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/WindowsTargetProcessor.cmake")

if (ANDROID OR IOS OR TVOS or (WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64"))
    message(FATAL_ERROR "Mod SDK is not supported on this platform")
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Build type options: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DUSK_MOD_SDK ON)
set(DUSK_MOD_SDK ON PARENT_SCOPE)

# Version detection & version.h
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskVersion.cmake")
dusk_detect_version()
dusk_configure_version_header()

# Provides dawn::webgpu_dawn: header-only for macOS/Linux, import lib on Windows.
include("${CMAKE_CURRENT_SOURCE_DIR}/../extern/aurora/cmake/AuroraDependencyVersions.cmake")
set(AURORA_DAWN_PROVIDER "package" CACHE STRING "How to provide Dawn for the mod SDK")
include("${CMAKE_CURRENT_SOURCE_DIR}/../extern/aurora/cmake/AuroraDawnProvider.cmake")

# Game ABI headers & compile definitions
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskGameABI.cmake")
dusk_add_game_headers_target("${CMAKE_CURRENT_BINARY_DIR}")

if (WIN32)
    # The import library from the matching game build (regular win32 CI/release
    # artifact: sdk/dusklight.lib). It binds by symbol name against
    # dusklight.exe, so any build of the same tag's sources is valid. Left
    # empty, header-only consumers still configure; add_dusk_mod fails with
    # remedies when a mod target is actually created.
    set(DUSK_GAME_IMPLIB "" CACHE FILEPATH "Path to the dusklight import library (sdk/dusklight.lib)")
    if (DUSK_GAME_IMPLIB AND NOT EXISTS "${DUSK_GAME_IMPLIB}")
        message(FATAL_ERROR "Mod SDK: DUSK_GAME_IMPLIB does not exist: ${DUSK_GAME_IMPLIB}")
    endif ()
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskModSDK.cmake")
