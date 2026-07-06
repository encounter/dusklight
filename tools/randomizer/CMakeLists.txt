cmake_minimum_required(VERSION 3.25)
project(randomizer CXX)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to dusk source root")
    add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)
endif ()

set(DUSK_MODS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/mods" CACHE PATH "Directory to write .dusk packages into")

# Generator dependencies, linked statically into the mod library (no CRT crossing).
include(FetchContent)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.9.0
)
FetchContent_Declare(
        base64pp
        GIT_REPOSITORY https://github.com/matheusgomes28/base64pp.git
        GIT_TAG v0.2.0-rc0
)
FetchContent_Declare(
        battery-embed
        GIT_REPOSITORY https://github.com/batterycenter/embed.git
        GIT_TAG fdbae3f
)
FetchContent_MakeAvailable(yaml-cpp base64pp battery-embed)

set(RANDOMIZER_GENERATOR_SOURCES
        generator/logic/area.cpp
        generator/logic/dungeon.cpp
        generator/logic/entrance.cpp
        generator/logic/entrance_shuffle.cpp
        generator/logic/fill.cpp
        generator/logic/flatten/bits.cpp
        generator/logic/flatten/flatten.cpp
        generator/logic/flatten/simplify_algebraic.cpp
        generator/logic/hints.cpp
        generator/logic/item.cpp
        generator/logic/item_pool.cpp
        generator/logic/location.cpp
        generator/logic/plandomizer.cpp
        generator/logic/requirement.cpp
        generator/logic/search.cpp
        generator/logic/spoiler_log.cpp
        generator/logic/world.cpp
        generator/randomizer.cpp
        generator/seedgen/config.cpp
        generator/seedgen/seed.cpp
        generator/seedgen/settings.cpp
        generator/utility/color.cpp
        generator/utility/common.cpp
        generator/utility/endian.cpp
        generator/utility/file.cpp
        generator/utility/log.cpp
        generator/utility/path.cpp
        generator/utility/platform.cpp
        generator/utility/random.cpp
        generator/utility/string.cpp
        generator/utility/text.cpp
        generator/utility/time.cpp
)

add_dusk_mod(randomizer
        SOURCES
        src/mod.cpp
        src/paths.cpp
        ${RANDOMIZER_GENERATOR_SOURCES}
        MOD_JSON mod.json
)

target_link_libraries(randomizer PRIVATE yaml-cpp::yaml-cpp base64pp fmt::fmt)

string(LENGTH "${CMAKE_CURRENT_SOURCE_DIR}/" RANDOMIZER_SOURCE_PATH_SIZE)
target_compile_definitions(randomizer PRIVATE
        RANDO_DATA_PATH="generator/data/"
        SOURCE_PATH_SIZE=${RANDOMIZER_SOURCE_PATH_SIZE}
)

# Embed the generator's YAML data into the mod library. Paths are relative to this
# directory and must match the RANDO_DATA_PATH-based b::embed<> identifiers in code.
file(GLOB_RECURSE RANDOMIZER_DATA RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/generator/data/*")
foreach (RANDOMIZER_FILE IN LISTS RANDOMIZER_DATA)
    if (RANDOMIZER_FILE MATCHES "^generator/data/tests")
        continue()
    endif ()
    b_embed(randomizer "${RANDOMIZER_FILE}")
endforeach ()
