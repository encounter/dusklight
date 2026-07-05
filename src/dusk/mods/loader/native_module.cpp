#include "native_module.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
#if defined(_WIN32)
void* pl_dlopen(const std::filesystem::path& p) {
    return LoadLibraryW(p.wstring().c_str());
}
void* pl_dlsym(void* h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name));
}
void pl_dlclose(void* h) {
    FreeLibrary(static_cast<HMODULE>(h));
}
std::string pl_dlerror() {
    const DWORD err = GetLastError();
    char buf[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err, 0,
        buf, sizeof(buf), nullptr);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    // Mods link the DLL CRT, whose vcruntime/msvcp are the toolset-versioned DLLs vendored
    // beside the game exe and are not forward-compatible: a mod built with a newer MSVC
    // toolset than the vendored runtime fails here with one of these two codes.
    if (err == ERROR_MOD_NOT_FOUND || err == ERROR_PROC_NOT_FOUND) {
        s += " (a dependent DLL or entry point is missing; if this mod was built with a newer "
             "MSVC toolset than the game's bundled VC runtime, update the game or rebuild the "
             "mod with a matching toolset)";
    }
    return s;
}
#else
#include <dlfcn.h>
static void* pl_dlopen(const std::filesystem::path& p) {
    int flags = RTLD_LAZY | RTLD_LOCAL;
#if defined(RTLD_DEEPBIND)
    flags |= RTLD_DEEPBIND;
#endif
    return dlopen(p.c_str(), flags);
}
static void* pl_dlsym(void* h, const char* name) {
    return dlsym(h, name);
}
static void pl_dlclose(void* h) {
    dlclose(h);
}
static std::string pl_dlerror() {
    const char* e = dlerror();
    return e ? e : "(unknown error)";
}
#endif
}

namespace dusk::mods::loader {
NativeModule::NativeModule() noexcept : handle(nullptr) {
}

NativeModule::NativeModule(NativeModule&& other) noexcept {
    handle = other.handle;
    other.handle = nullptr;
}

NativeModule& NativeModule::operator=(NativeModule&& other) noexcept {
    handle = other.handle;
    other.handle = nullptr;
    return *this;
}

NativeModule::NativeModule(const std::filesystem::path& path) {
    handle = pl_dlopen(path);
    if (!handle) {
        throw std::runtime_error(pl_dlerror());
    }
}

NativeModule::~NativeModule() {
    if (handle) {
        pl_dlclose(handle);
    }
}

void* NativeModule::LookupSymbol(const char* name) const {
    return pl_dlsym(handle, name);
}
}  // namespace dusk::mods::loader
