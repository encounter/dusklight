#pragma once

#include <cstdint>
#include <vector>

namespace dusk::mods::manifest {

// Symbol flags mirrored from symgen.
constexpr uint32_t kFlagCode = 1u << 0;
constexpr uint32_t kFlagData = 1u << 1;
constexpr uint32_t kFlagLocal = 1u << 2;
constexpr uint32_t kFlagMultiName = 1u << 3;
constexpr uint32_t kFlagDupName = 1u << 4;
constexpr uint32_t kFlagInlineSites = 1u << 5;

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

// True if the manifest records that the function at this code address was inlined into
// at least one caller in this build — an entry hook on it only intercepts the calls
// that were not inlined (MODS_LINKING.md §6). outName receives the symbol name (valid
// for the process lifetime) when known. False when no manifest is loaded.
bool has_inline_sites(const void* addr, const char** outName = nullptr);

}  // namespace dusk::mods::manifest
