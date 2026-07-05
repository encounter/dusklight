cmake_minimum_required(VERSION 3.25)
project(my_mod CXX)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to dusk source root")
    # Default: the slim mod SDK -- game headers + the pinned prebuilt Dawn
    # package, no game-tree configure. On Windows pass
    # -DDUSK_GAME_IMPLIB=<sdk/dusklight.lib from the matching game build>.
    # The full-tree fallback configures (and on Windows builds) the game to
    # produce the implib locally -- needed for revisions without a published
    # build artifact.
    option(DUSK_MOD_USE_FULL_TREE "Use the full dusk tree instead of the slim mod SDK" OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if (DUSK_MOD_USE_FULL_TREE)
        add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)
    else ()
        add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)
    endif ()
endif ()

# Output .dusk packages next to the build directory by default.
set(DUSK_MODS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/mods" CACHE PATH "Directory to write .dusk packages into")

add_dusk_mod(template_mod
    SOURCES  src/mod.cpp
    MOD_JSON mod.json
    RES_DIR  res
)
