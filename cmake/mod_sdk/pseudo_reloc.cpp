// Dusk mod SDK runtime (Windows): applies lld's MinGW-style runtime pseudo-relocations on
// the plain MSVC CRT. This is what lets mods reference game data (`extern` globals) with no
// __declspec(dllimport) annotations: lld-link -lldmingw auto-imports the data through IAT
// slots and records fixups, and this file applies them at load time.
//
// Ordering: the fixup pass runs as an early CRT initializer (.CRT$XCB, ahead of user
// .CRT$XCU dynamic initializers), and the IAT slots it reads were already bound by the OS
// loader — so even mod static initializers observe patched references.
//
// Failure (an out-of-range 32-bit fixup means a TU was compiled without -mcmodel=large —
// a build configuration error): nothing is patched, diagnostics go to stderr and the
// debugger, and DllMain fails the load. Do not define your own DllMain in a mod.
//
// Format reference: mingw-w64 crt/pseudo-reloc.c (v2 items only; that is all lld emits).
// See MODS_LINKING.md §4 for design and measurements.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" char __RUNTIME_PSEUDO_RELOC_LIST__;
extern "C" char __RUNTIME_PSEUDO_RELOC_LIST_END__;
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

struct HdrV2 {
    uint32_t magic1;
    uint32_t magic2;
    uint32_t version;
};
struct ItemV2 {
    uint32_t sym;     // RVA of the __imp_ slot (OS-bound IAT entry)
    uint32_t target;  // RVA of the reference to patch
    uint32_t flags;   // low 8 bits: bit width of the reference
};

bool g_relocsFailed = false;

void report(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", buf);
    OutputDebugStringA(buf);
}

bool compute_fixup(const ItemV2& item, char* base, intptr_t* out) {
    char* impSlot = base + item.sym;
    const intptr_t real = *reinterpret_cast<intptr_t*>(impSlot);
    char* target = base + item.target;
    const int bits = static_cast<int>(item.flags & 0xff);

    intptr_t reldata;
    switch (bits) {
    case 8: reldata = *reinterpret_cast<int8_t*>(target); break;
    case 16: reldata = *reinterpret_cast<int16_t*>(target); break;
    case 32: reldata = *reinterpret_cast<int32_t*>(target); break;
    case 64: reldata = *reinterpret_cast<int64_t*>(target); break;
    default:
        report("[dusk mod runtime] unsupported %d-bit pseudo-relocation at RVA 0x%x", bits,
            item.target);
        return false;
    }
    reldata -= reinterpret_cast<intptr_t>(impSlot);
    reldata += real;

    if (bits < 64) {
        const intptr_t maxUnsigned = (intptr_t{1} << bits) - 1;
        const intptr_t minSigned = -(intptr_t{1} << (bits - 1));
        if (reldata > maxUnsigned || reldata < minSigned) {
            report(
                "[dusk mod runtime] %d-bit data fixup at RVA 0x%x is out of range "
                "(delta %+lld) — was this translation unit compiled without "
                "/clang:-mcmodel=large? Failing the mod load.",
                bits, item.target, static_cast<long long>(reldata));
            return false;
        }
    }
    *out = reldata;
    return true;
}

}  // namespace

// lld refuses to emit runtime pseudo-relocs unless a function with exactly this (mingw CRT)
// name exists in the image; it is also our actual entry point.
extern "C" void _pei386_runtime_relocator() {
    char* base = reinterpret_cast<char*>(&__ImageBase);
    char* start = &__RUNTIME_PSEUDO_RELOC_LIST__;
    char* end = &__RUNTIME_PSEUDO_RELOC_LIST_END__;
    if (end - start < static_cast<ptrdiff_t>(sizeof(HdrV2))) {
        return;  // no data auto-imports in this mod
    }
    const HdrV2* hdr = reinterpret_cast<const HdrV2*>(start);
    if (hdr->magic1 != 0 || hdr->magic2 != 0 || hdr->version != 1) {
        report("[dusk mod runtime] unexpected pseudo-relocation list format");
        g_relocsFailed = true;
        return;
    }
    const ItemV2* items = reinterpret_cast<const ItemV2*>(hdr + 1);
    const ItemV2* itemsEnd = reinterpret_cast<const ItemV2*>(end);

    // Validate everything before writing anything, so a bad fixup can't leave the image
    // half-patched.
    intptr_t scratch;
    for (const ItemV2* it = items; it < itemsEnd; ++it) {
        if (!compute_fixup(*it, base, &scratch)) {
            g_relocsFailed = true;
            return;
        }
    }
    for (const ItemV2* it = items; it < itemsEnd; ++it) {
        intptr_t reldata;
        compute_fixup(*it, base, &reldata);
        char* target = base + it->target;
        const size_t len = static_cast<size_t>(it->flags & 0xff) / 8;
        DWORD old = 0;
        if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) {
            report("[dusk mod runtime] VirtualProtect failed at RVA 0x%x", it->target);
            g_relocsFailed = true;
            return;
        }
        std::memcpy(target, &reldata, len);
        VirtualProtect(target, len, old, &old);
    }
}

using PVFV = void (*)();
#pragma section(".CRT$XCB", read)
extern "C" __declspec(allocate(".CRT$XCB")) PVFV dusk_mod_pseudo_reloc_init =
    _pei386_runtime_relocator;

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && g_relocsFailed) {
        return FALSE;  // LoadLibrary fails; the mod loader reports a clean load error
    }
    return TRUE;
}
