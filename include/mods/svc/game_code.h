#pragma once

#include "mods/api.h"

/*
 * The game-code ABI contract (MODS_LINKING.md §8). Mods that link or hook game code
 * directly (game/JSystem/Dolphin-SDK symbols — anything beyond the service APIs)
 * import this service; service-only and asset-only mods must not.
 *
 * MAJOR is the game-code ABI *epoch*: it is bumped manually when game-visible struct
 * layouts or calling conventions change incompatibly (e.g. a TARGET_PC field added to
 * a shipping struct). The loader's ordinary version check then fails mods built
 * against the old epoch with a clear message instead of letting them corrupt memory.
 *
 * The payload identifies the exact running build for diagnostics and for mods that
 * want to gate on more than the epoch.
 */
#define GAME_CODE_SERVICE_ID "dev.twilitrealm.dusklight.game_code"
#define GAME_CODE_SERVICE_MAJOR 1u
#define GAME_CODE_SERVICE_MINOR 0u

typedef struct GameCodeService {
    ServiceHeader header;

    /* Build id of the running game binary: PDB GUID+age on Windows, LC_UUID on
     * macOS, GNU build-id on Linux. Matches the symbol manifest's key. May be
     * empty (len 0) if the identity could not be determined. */
    const uint8_t* build_id;
    uint32_t build_id_len;

    /* Toolchain/platform ABI tag, e.g. "msvc-x64-windows", "clang-arm64-macos". */
    const char* abi_tag;
} GameCodeService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<GameCodeService> {
    static constexpr const char* id = GAME_CODE_SERVICE_ID;
    static constexpr uint16_t major_version = GAME_CODE_SERVICE_MAJOR;
};
#endif
