#pragma once

#include <cstdint>
#include <vector>

namespace dusk::mods::manifest {

// Symbol flags mirrored from tools/symgen (manifest.rs).
constexpr uint32_t kFlagCode = 1u << 0;
constexpr uint32_t kFlagData = 1u << 1;
constexpr uint32_t kFlagLocal = 1u << 2;
constexpr uint32_t kFlagMultiName = 1u << 3;
constexpr uint32_t kFlagDupName = 1u << 4;

enum class ResolveStatus {
    Ok,
    Unavailable,  // no manifest loaded (missing, stale, or malformed)
    NotFound,
    Ambiguous,  // name maps to multiple addresses (overloads / per-TU statics)
};

// Maps the symbol manifest next to the game binary and validates it against the
// running image's build id (PDB GUID+age / Mach-O UUID / GNU build-id). A missing or
// stale manifest logs and leaves by-name resolution unavailable; hooks by address are
// unaffected. Safe to call more than once.
void initialize();

bool available();

// Build id of the running executable image (PDB GUID+age / Mach-O UUID / GNU
// build-id), computed once on first use; empty if it couldn't be determined.
// Independent of whether a manifest file was loaded.
const std::vector<uint8_t>& image_build_id();

// Resolve a symbol name to its address in the running image. Names use the dlsym
// convention (no Mach-O leading underscore); on Windows both decorated names (publics)
// and undecorated display names (module records, may be Ambiguous) are present.
ResolveStatus resolve(const char* name, void** outAddr, uint32_t* outFlags = nullptr);

}  // namespace dusk::mods::manifest
