# Slim mod-SDK entry point: everything a mod needs to build against this dusk
# checkout — game/service headers, the constant ABI defines, version.h and
# webgpu.h — without configuring the full game tree. The only external fetch
# is aurora's prebuilt Dawn package (webgpu.h + the Windows import lib for
# wgpu* calls).
#
# Usage (from a mod project):
#   add_subdirectory(<dusk>/sdk dusk-sdk EXCLUDE_FROM_ALL)
#   add_dusk_mod(my_mod SOURCES ... MOD_JSON mod.json)
#
# On Windows, pass -DDUSK_GAME_IMPLIB=<path to sdk/dusklight.lib> from the
# matching game build artifact (same tag as this checkout). macOS/Linux mods
# link nothing from the game — symbols resolve against the running executable
# at load time.
#
# Falling back to the full tree (add_subdirectory(<dusk>) like
# tools/mod_template with DUSK_MOD_USE_FULL_TREE=ON) remains supported and is
# required on Windows when no prebuilt implib exists for this revision.

cmake_minimum_required(VERSION 3.25)

if (TARGET dusklight_game_headers)
    message(FATAL_ERROR "dusk sdk: the full dusk tree is already configured in this build; "
            "do not add <dusk>/sdk as well — add_dusk_mod is already available.")
endif ()

# Match the compiler's target architecture before any arch checks (mirrors the
# root CMakeLists).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/WindowsTargetProcessor.cmake")

if (ANDROID OR IOS OR TVOS)
    message(FATAL_ERROR "dusk sdk: code mods are not supported on this platform (desktop only).")
endif ()
if (WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "dusk sdk: Windows code mods are x64-only for now "
            "(got CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}).")
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Build type options: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif ()

# The game builds everything position-independent; mirrored here for any
# static helper libraries mods create in scopes that inherit from this one.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Marker for mod build scripts that want to know which mode resolved.
set(DUSK_MOD_SDK ON)
set(DUSK_MOD_SDK ON PARENT_SCOPE)

# Version detection + version.h (configured into this directory's binary dir,
# which dusk_add_game_headers_target exports as an include dir).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskVersion.cmake")
dusk_detect_version()
dusk_configure_version_header()

# Dawn: same pinned prebuilt package as the game build. Provides
# dawn::webgpu_dawn — header-only for macOS/Linux mods, import lib on Windows.
include("${CMAKE_CURRENT_SOURCE_DIR}/../extern/aurora/cmake/AuroraDependencyVersions.cmake")
set(AURORA_DAWN_PROVIDER "package" CACHE STRING "How to provide Dawn for the mod SDK")
include("${CMAKE_CURRENT_SOURCE_DIR}/../extern/aurora/cmake/AuroraDawnProvider.cmake")

# Game-ABI headers + constant defines (single source of truth shared with the
# root CMakeLists).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskGameABI.cmake")
dusk_add_game_headers_target("${CMAKE_CURRENT_BINARY_DIR}")

if (WIN32)
    # The import library from the matching game build (regular win32 CI/release
    # artifact: sdk/dusklight.lib). It binds by symbol name against
    # dusklight.exe, so any build of the same tag's sources is valid. Left
    # empty, header-only consumers still configure; add_dusk_mod fails with
    # remedies when a mod target is actually created.
    set(DUSK_GAME_IMPLIB "" CACHE FILEPATH
            "Path to the dusklight import library (sdk/dusklight.lib) from a matching game build")
    if (DUSK_GAME_IMPLIB AND NOT EXISTS "${DUSK_GAME_IMPLIB}")
        message(FATAL_ERROR "dusk sdk: DUSK_GAME_IMPLIB does not exist: ${DUSK_GAME_IMPLIB}")
    endif ()
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DuskModSDK.cmake")
