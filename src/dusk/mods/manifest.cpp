#include "manifest.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>

#include "aurora/lib/logging.hpp"

#include "dusk/io.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <elf.h>
#include <link.h>
#endif

namespace dusk::mods::manifest {
namespace {

static aurora::Module Log("dusk::mods::manifest");

constexpr char kMagic[8] = {'D', 'U', 'S', 'K', 'M', 'A', 'N', '\0'};
constexpr uint32_t kVersion = 1;

// Mirrors the writer in tools/symgen/src/manifest.rs. 72 bytes so the entry array
// that follows is 8-aligned.
struct Header {
    char magic[8];
    uint32_t version;
    uint32_t entryCount;
    uint32_t buildIdLen;
    uint8_t buildId[32];
    uint32_t reserved;
    uint64_t stringsOff;
    uint64_t stringsLen;
};
static_assert(sizeof(Header) == 72);

struct Entry {
    uint64_t hash;
    uint64_t rva;
    uint32_t nameOff;
    uint32_t flags;
};
static_assert(sizeof(Entry) == 24);

struct State {
    std::vector<uint8_t> data;
    const Entry* entries = nullptr;
    uint32_t entryCount = 0;
    const char* strings = nullptr;
    uint64_t stringsLen = 0;
    uintptr_t imageBase = 0;
    // (rva, nameOff) of entries flagged kFlagInlineSites, sorted by rva for the
    // address-keyed lookup hookInstallByAddr does.
    std::vector<std::pair<uint64_t, uint32_t>> inlineSites;
    bool loaded = false;
    bool initialized = false;
};
State s_state;

uint64_t fnv1a64(const char* str) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const char* p = str; *p != '\0'; ++p) {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

// Build id of the running executable image, matching what symgen recorded:
// PDB GUID (RFC 4122 byte order) + age on Windows, LC_UUID on Mach-O, GNU
// build-id on ELF. Also reports the address RVAs are relative to.
bool running_image_identity(std::vector<uint8_t>& outId, uintptr_t& outBase) {
#if defined(_WIN32)
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    outBase = reinterpret_cast<uintptr_t>(base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (dir.VirtualAddress == 0) {
        return false;
    }
    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    for (size_t i = 0; i < dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY); ++i) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) {
            continue;
        }
        struct CvInfo {
            uint32_t signature;  // 'RSDS'
            uint8_t guid[16];
            uint32_t age;
        };
        if (entries[i].SizeOfData < sizeof(CvInfo)) {
            continue;
        }
        const auto* cv = reinterpret_cast<const CvInfo*>(base + entries[i].AddressOfRawData);
        if (cv->signature != 0x53445352) {  // "RSDS"
            continue;
        }
        // The GUID struct stores Data1..Data3 little-endian in memory; the manifest
        // stores RFC 4122 (big-endian) order, so swap them here.
        outId.assign(cv->guid, cv->guid + 16);
        std::swap(outId[0], outId[3]);
        std::swap(outId[1], outId[2]);
        std::swap(outId[4], outId[5]);
        std::swap(outId[6], outId[7]);
        for (int b = 0; b < 4; ++b) {
            outId.push_back(static_cast<uint8_t>(cv->age >> (8 * b)));
        }
        return true;
    }
    return false;
#elif defined(__APPLE__)
    // Image 0 is the main executable; RVAs are relative to the __TEXT vmaddr, which
    // is where the mach header lives, so the header address is the runtime base.
    const auto* header = _dyld_get_image_header(0);
    outBase = reinterpret_cast<uintptr_t>(header);
    const auto* header64 = reinterpret_cast<const mach_header_64*>(header);
    const auto* cmd = reinterpret_cast<const load_command*>(header64 + 1);
    for (uint32_t i = 0; i < header64->ncmds; ++i) {
        if (cmd->cmd == LC_UUID) {
            const auto* uuidCmd = reinterpret_cast<const uuid_command*>(cmd);
            outId.assign(uuidCmd->uuid, uuidCmd->uuid + 16);
            return true;
        }
        cmd = reinterpret_cast<const load_command*>(
            reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    return false;
#elif defined(__linux__)
    struct Ctx {
        std::vector<uint8_t>* id;
        uintptr_t base = 0;
        bool found = false;
    } ctx{&outId};
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* data) -> int {
            auto* ctx = static_cast<Ctx*>(data);
            // The first callback is the main executable.
            ctx->base = info->dlpi_addr;
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                const auto& phdr = info->dlpi_phdr[i];
                if (phdr.p_type != PT_NOTE) {
                    continue;
                }
                const auto* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + phdr.p_vaddr);
                const auto* end = p + phdr.p_memsz;
                while (p + sizeof(ElfW(Nhdr)) <= end) {
                    const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(p);
                    const auto* name = p + sizeof(ElfW(Nhdr));
                    const auto* desc = name + ((note->n_namesz + 3) & ~3u);
                    if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 &&
                        std::memcmp(name, "GNU", 4) == 0) {
                        ctx->id->assign(desc, desc + note->n_descsz);
                        ctx->found = true;
                        return 1;
                    }
                    p = desc + ((note->n_descsz + 3) & ~3u);
                }
            }
            return 1;  // only inspect the main executable
        },
        &ctx);
    outBase = ctx.base;
    return ctx.found;
#else
    (void)outId;
    (void)outBase;
    return false;
#endif
}

std::filesystem::path manifest_path() {
    const char* basePath = SDL_GetBasePath();
    std::filesystem::path dir = basePath != nullptr ? std::filesystem::path{basePath}
                                                    : std::filesystem::current_path();
    return dir / "dusklight.symdb";
}

std::string hex_string(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        constexpr char kHex[] = "0123456789abcdef";
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

}  // namespace

void initialize() {
    if (s_state.initialized) {
        return;
    }
    s_state.initialized = true;

    const auto path = manifest_path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        Log.info("no symbol manifest at {}; by-name resolution unavailable", dusk::io::fs_path_to_string(path));
        return;
    }
    std::vector<uint8_t> data;
    try {
        data = dusk::io::FileStream::ReadAllBytes(path);
    } catch (const std::exception& e) {
        Log.error("failed to read symbol manifest {}: {}", dusk::io::fs_path_to_string(path), e.what());
        return;
    }
    if (data.size() < sizeof(Header)) {
        Log.error("symbol manifest {} is truncated ({} bytes)", dusk::io::fs_path_to_string(path), data.size());
        return;
    }
    Header header{};
    std::memcpy(&header, data.data(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion) {
        Log.error("symbol manifest {} has wrong magic/version", dusk::io::fs_path_to_string(path));
        return;
    }
    const uint64_t entriesEnd = sizeof(Header) + uint64_t{header.entryCount} * sizeof(Entry);
    if (header.buildIdLen > sizeof(header.buildId) || entriesEnd > data.size() ||
        header.stringsOff != entriesEnd || header.stringsOff + header.stringsLen > data.size()) {
        Log.error("symbol manifest {} is malformed", dusk::io::fs_path_to_string(path));
        return;
    }

    std::vector<uint8_t> imageId;
    uintptr_t imageBase = 0;
    if (!running_image_identity(imageId, imageBase)) {
        Log.error("cannot determine the running image's build id; ignoring symbol manifest");
        return;
    }
    if (imageId.size() != header.buildIdLen ||
        std::memcmp(imageId.data(), header.buildId, imageId.size()) != 0) {
        Log.error(
            "symbol manifest {} is stale: built for {}, running image is {} — rebuild the game",
            dusk::io::fs_path_to_string(path), hex_string(header.buildId, header.buildIdLen),
            hex_string(imageId.data(), imageId.size()));
        return;
    }

    s_state.data = std::move(data);
    s_state.entries = reinterpret_cast<const Entry*>(s_state.data.data() + sizeof(Header));
    s_state.entryCount = header.entryCount;
    s_state.strings = reinterpret_cast<const char*>(s_state.data.data() + header.stringsOff);
    s_state.stringsLen = header.stringsLen;
    s_state.imageBase = imageBase;
    for (uint32_t i = 0; i < s_state.entryCount; ++i) {
        const Entry& entry = s_state.entries[i];
        if ((entry.flags & kFlagInlineSites) != 0 && entry.nameOff < s_state.stringsLen) {
            s_state.inlineSites.emplace_back(entry.rva, entry.nameOff);
        }
    }
    std::sort(s_state.inlineSites.begin(), s_state.inlineSites.end());
    s_state.inlineSites.erase(
        std::unique(s_state.inlineSites.begin(), s_state.inlineSites.end(),
            [](const auto& a, const auto& b) { return a.first == b.first; }),
        s_state.inlineSites.end());
    s_state.loaded = true;
    Log.info("symbol manifest loaded: {} symbols, build id {}", s_state.entryCount,
        hex_string(header.buildId, header.buildIdLen));
}

bool available() {
    return s_state.loaded;
}

const std::vector<uint8_t>& image_build_id() {
    static const std::vector<uint8_t> s_id = [] {
        std::vector<uint8_t> id;
        uintptr_t base = 0;
        running_image_identity(id, base);
        return id;
    }();
    return s_id;
}

ResolveStatus resolve(const char* name, void** outAddr, uint32_t* outFlags) {
    if (!s_state.loaded) {
        return ResolveStatus::Unavailable;
    }
    const uint64_t hash = fnv1a64(name);
    const Entry* begin = s_state.entries;
    const Entry* end = begin + s_state.entryCount;
    // Lower bound by hash, then walk the (tiny) equal-hash range comparing names.
    size_t lo = 0;
    size_t hi = s_state.entryCount;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (begin[mid].hash < hash) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (const Entry* entry = begin + lo; entry != end && entry->hash == hash; ++entry) {
        if (entry->nameOff >= s_state.stringsLen ||
            std::strcmp(s_state.strings + entry->nameOff, name) != 0) {
            continue;
        }
        if ((entry->flags & kFlagDupName) != 0) {
            return ResolveStatus::Ambiguous;
        }
        *outAddr = reinterpret_cast<void*>(s_state.imageBase + entry->rva);
        if (outFlags != nullptr) {
            *outFlags = entry->flags;
        }
        return ResolveStatus::Ok;
    }
    return ResolveStatus::NotFound;
}

bool has_inline_sites(const void* addr, const char** outName) {
    if (!s_state.loaded || s_state.inlineSites.empty()) {
        return false;
    }
    const auto rva = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(addr) - s_state.imageBase);
    const auto it = std::lower_bound(s_state.inlineSites.begin(), s_state.inlineSites.end(),
        std::pair<uint64_t, uint32_t>{rva, 0});
    if (it == s_state.inlineSites.end() || it->first != rva) {
        return false;
    }
    if (outName != nullptr) {
        *outName = s_state.strings + it->second;
    }
    return true;
}

}  // namespace dusk::mods::manifest
