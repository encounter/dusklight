#include "dusk/mod_loader.hpp"
#include "dusk/hook_system.hpp"
#include "dusk/logging.h"
#include "loader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "aurora/dvd.h"
#include "dusk/config.hpp"
#include "dusk/io.hpp"
#include "miniz.h"
#include "native_module.hpp"
#include "nlohmann/json.hpp"

static aurora::Module Log("dusk::modLoader");

using namespace dusk::mods::loader;
using namespace std::string_literals;
using namespace std::string_view_literals;

#if defined(_M_ARM64) || defined(__aarch64__)
static constexpr std::string_view k_archSuffix = "_arm64"sv;
#elif defined(_M_X64) || defined(__x86_64__)
static constexpr std::string_view k_archSuffix = "_x64"sv;
#elif defined(_M_IX86) || defined(__i386__)
static constexpr std::string_view k_archSuffix = "_x86"sv;
#else
static constexpr std::string_view k_archSuffix = ""sv;
#endif

static dusk::ModLoader g_modLoader;

// We cannot delete config vars registered by mods until the game shuts down fully.
// Therefore, orphan them during shutdown.
static std::vector<std::unique_ptr<dusk::ConfigVarBase>> OrphanedConfigVars;

namespace dusk {

ModLoader& ModLoader::instance() {
    return g_modLoader;
}

static std::unique_ptr<ModBundle> loadBundle(const std::filesystem::path& modPath, bool fromDir) {
    if (fromDir) {
        return std::make_unique<ModBundleDisk>(modPath);
    } else {
        std::vector<u8> data = io::FileStream::ReadAllBytes(modPath);
        return std::make_unique<ModBundleZip>(std::move(data));
    }
}

struct DllLocateResult {
    std::string primary;
    std::string fallback;
};

static std::string_view getFileNameWithoutExtension(const std::string_view fileName) {
    return fileName.substr(0, fileName.find_last_of('.'));
}

static DllLocateResult LocateDllInBundle(ModBundle& bundle) {
    std::string dllEntry, dllFallback;
    for (const auto& name : bundle.getFileNames()) {
        if (!name.ends_with(".dll"sv) && !name.ends_with(".dylib"sv) && !name.ends_with(".so"sv)) {
            continue;
        }

        if (!k_archSuffix.empty() && getFileNameWithoutExtension(name).ends_with(k_archSuffix)) {
            dllEntry = name;
        } else if (dllFallback.empty()) {
            dllFallback = name;
        }
    }

    return DllLocateResult{dllEntry, dllFallback};
}

class InvalidModDataException : public std::runtime_error {
public:
    explicit InvalidModDataException(const std::string& msg) : runtime_error(msg) {}
    explicit InvalidModDataException(const char* msg) : runtime_error(msg) {}
};

static void validateModId(std::string_view const str) {
    if (str.empty()) {
        throw InvalidModDataException("Missing ID value in mod metadata!");
    }

    bool lastWasPeriod = false;
    for (auto const chr : str) {
        if (chr == '.') {
            if (lastWasPeriod) {
                throw InvalidModDataException("Cannot have two consecutive periods in mod ID!");
            }
            lastWasPeriod = true;
            continue;
        }

        lastWasPeriod = false;

        if (chr == '_')
            continue;

        if (chr >= '0' && chr <= '9')
            continue;

        if (chr >= 'a' && chr <= 'z')
            continue;

        if (chr >= 'A' && chr <= 'Z')
            continue;

        throw InvalidModDataException(fmt::format("Invalid character '{}' in mod ID. Valid characters are period, underscore, and alphanumerics.", chr));
    }
}

static ModMetadata loadMetadata(const std::filesystem::path& modPath, ModBundle& bundle) {
    const auto metaJson = bundle.readFile("mod.json");
    auto j = nlohmann::json::parse(metaJson);

    std::string metaId = j.value("id", "");
    std::string metaName = j.value("name", "");
    std::string metaVersion = j.value("version", "");
    std::string metaAuthor = j.value("author", "");
    std::string metaDescription = j.value("description", "");
    const bool hasCode = j.value("has_code", false);

    validateModId(metaId);

    if (metaName.empty()) {
        metaName = io::fs_path_to_string(modPath.stem());
    }
    if (metaVersion.empty()) {
        metaVersion = "?"s;
    }
    if (metaAuthor.empty()) {
        metaAuthor = "unknown"s;
    }

    return ModMetadata{
        std::move(metaId),
        std::move(metaName),
        std::move(metaVersion),
        std::move(metaAuthor),
        std::move(metaDescription),
        hasCode,
    };
}

template <std::ranges::input_range TIter>
bool checkDuplicateMod(
    const ModMetadata& metadata, TIter mods) {
    return std::ranges::any_of(mods,
        [&](const LoadedMod& mod) { return mod.metadata.id == metadata.id; });
}

static bool validateManifest(const ModManifest* manifest, LoadedMod& mod) {
    if (manifest == nullptr) {
        Log.error("{} returned a null mod manifest", mod.metadata.id);
        mod.native_status = NativeModStatus::MissingExport;
        return false;
    }
    if (manifest->struct_size != sizeof(ModManifest)) {
        Log.error("{} manifest has invalid size {} (expected {})", mod.metadata.id,
            manifest->struct_size, sizeof(ModManifest));
        mod.native_status = NativeModStatus::ApiVersionMismatch;
        return false;
    }
    if (manifest->abi_version != MOD_ABI_VERSION) {
        Log.error("{} expects ABI v{} but engine is v{}, skipping", mod.metadata.id,
            manifest->abi_version, MOD_ABI_VERSION);
        mod.native_status = NativeModStatus::ApiVersionMismatch;
        return false;
    }
    if ((manifest->import_count > 0 && manifest->imports == nullptr) ||
        (manifest->export_count > 0 && manifest->exports == nullptr))
    {
        Log.error("{} manifest has invalid import/export arrays", mod.metadata.id);
        mod.native_status = NativeModStatus::MissingExport;
        return false;
    }
    return true;
}

static bool validateContextSymbol(ModContext** contextSymbol, LoadedMod& mod) {
    if (contextSymbol == nullptr) {
        Log.error("{} missing required mod_context export", mod.metadata.id);
        mod.native_status = NativeModStatus::MissingExport;
        return false;
    }
    return true;
}

static std::string lifecycleErrorMessage(
    const char* fnName, const ModResult result, const ModError& error) {
    if (error.message[0] != '\0') {
        return error.message;
    }
    return fmt::format("{} failed with result {}", fnName, static_cast<int>(result));
}

static std::string nativeStatusMessage(const NativeModStatus status) {
    switch (status) {
    case NativeModStatus::BuildDisabled:
        return "native code mods are disabled in this build";
    case NativeModStatus::ModMissingPlatform:
        return "no native library for this platform";
    case NativeModStatus::ApiVersionMismatch:
        return "mod ABI version mismatch";
    case NativeModStatus::MissingExport:
        return "missing required native API export";
    case NativeModStatus::Unknown:
        return "unknown native load failure";
    case NativeModStatus::None:
    case NativeModStatus::Loaded:
        break;
    }
    return "native mod failed to load";
}

void ModLoader::tryLoadNativeMod(LoadedMod& mod) {
    if (!EnableCodeMods) {
        Log.error("Code mods are not available in this build");
        mod.native_status = NativeModStatus::BuildDisabled;
        return;
    }

    namespace fs = std::filesystem;

    auto [dllEntry, dllFallback] = LocateDllInBundle(*mod.bundle);
    if (dllEntry.empty()) {
        dllEntry = dllFallback;
    }

    if (dllEntry.empty()) {
        Log.error(
            "no *{} found in {} — skipping", NativeModule::LibraryExtension, mod.metadata.id);
        mod.native_status = NativeModStatus::ModMissingPlatform;
        return;
    }

    const fs::path cacheDir = m_modsDir / ".cache" / mod.metadata.id;
    std::error_code ec;
    fs::create_directories(cacheDir, ec);

    const fs::path dllCachePath = cacheDir / fs::path(dllEntry).filename();

    std::vector<u8> dllData;
    try {
        dllData = mod.bundle->readFile(dllEntry);
    } catch (const std::runtime_error& e) {
        Log.error(
            "failed to extract {} from {}", dllEntry, mod.metadata.id);
        return;
    }

    {
        std::ofstream out(dllCachePath, std::ios::binary | std::ios::out);
        if (!out) {
            Log.error("failed to write {}", io::fs_path_to_string(dllCachePath));
            return;
        }

        out.write(
            reinterpret_cast<const char*>(dllData.data()),
            static_cast<std::streamsize>(dllData.size()));
    }

    auto nativeMod = std::make_unique<NativeMod>();
    try {
        nativeMod->handle = std::make_unique<NativeModule>(dllCachePath);
    } catch (const std::runtime_error& e) {
        Log.error("failed to open {}: {}", io::fs_path_to_string(dllCachePath), e.what());
        return;
    }

    const auto getManifest =
        nativeMod->handle->LookupSymbol<ModGetManifestFn>("mod_get_manifest");
    nativeMod->contextSymbol = nativeMod->handle->LookupSymbol<ModContext**>("mod_context");
    nativeMod->fn_initialize =
        nativeMod->handle->LookupSymbol<ModInitializeFn>("mod_initialize");
    nativeMod->fn_update = nativeMod->handle->LookupSymbol<ModUpdateFn>("mod_update");
    nativeMod->fn_shutdown =
        nativeMod->handle->LookupSymbol<ModShutdownFn>("mod_shutdown");

    if (!getManifest || !nativeMod->contextSymbol || !nativeMod->fn_initialize ||
        !nativeMod->fn_update || !nativeMod->fn_shutdown)
    {
        Log.error("{} missing required mod API exports — skipping",
            io::fs_path_to_string(fs::path(dllEntry).filename()));
        mod.native_status = NativeModStatus::MissingExport;
        return;
    }

    nativeMod->manifest = getManifest();
    if (!validateManifest(nativeMod->manifest, mod)) {
        return;
    }

    if (!validateContextSymbol(nativeMod->contextSymbol, mod)) {
        return;
    }
    *nativeMod->contextSymbol = mod.context.get();

    mod.dir = io::fs_path_to_string(fs::absolute(cacheDir));
    mod.native = std::move(nativeMod);
    mod.native_status = NativeModStatus::Loaded;
}

static std::string escapeModIdForConfig(std::string_view const id) {
    std::string buf;

    // Simple escaping. All characters in mod IDs literal, except for '.' and '_'.
    // '.' -> '_', '_' -> '__'
    for (char const chr : id) {
        if (chr == '.') {
            buf.push_back('_');
        } else if (chr == '_') {
            buf.push_back('_');
            buf.push_back('_');
        } else {
            buf.push_back(chr);
        }
    }

    return buf;
}

static std::string modEnabledCVarName(std::string_view const id) {
    return fmt::format("mod.{}.enabled", escapeModIdForConfig(id));
}

void ModLoader::tryLoadDusk(const std::filesystem::path& modPath, bool fromDir) {
    namespace fs = std::filesystem;

    std::unique_ptr<ModBundle> bundle;
    try {
        bundle = loadBundle(modPath, fromDir);
    } catch (const std::runtime_error& e) {
        Log.error("Failed to open {} bundle: {}", io::fs_path_to_string(modPath.filename()), e.what());
        return;
    }

    ModMetadata metadata;
    try
    {
        metadata = loadMetadata(modPath, *bundle);
    }
    catch (const std::runtime_error& e) {
        Log.error(
            "bad mod.json in {}: {}", io::fs_path_to_string(modPath.filename()), e.what());
        return;
    }

    if (checkDuplicateMod(metadata, mods())) {
        Log.error(
            "mod with id '{}' already exists, not loading {}",
            metadata.id,
            io::fs_path_to_string(modPath.filename()));
        return;
    }

    const auto& inserted = m_mods.emplace_back(std::make_unique<LoadedMod>());

    auto& mod = *inserted;
    mod.active = true;
    mod.mod_path = io::fs_path_to_string(fs::absolute(modPath));
    mod.metadata = std::move(metadata);
    mod.bundle = std::move(bundle);
    mod.context = std::make_unique<ModContext>();
    mod.context->mod = &mod;
    mod.cvarIsEnabled = std::make_unique<ConfigVar<bool>>(modEnabledCVarName(mod.metadata.id), true);

    if (mod.metadata.hasCode) {
        mod.native_status = NativeModStatus::Unknown;
        tryLoadNativeMod(mod);
        // Native mod lod failure DOES NOT block insertion into m_mods.
        // We still want to be able to present the failed load in the UI!

        if (mod.native_status != NativeModStatus::Loaded) {
            Log.error("Native mod '{}' failed to load, disabling", metadata.id);
            mod.active = false;
            mod.load_failed = true;
            mod.failure_reason = nativeStatusMessage(mod.native_status);
        }
    }


    Log.info(
        "found '{}' ('{}') v{} by {} ({})",
        mod.metadata.name,
        mod.metadata.id,
        mod.metadata.version,
        mod.metadata.author,
        io::fs_path_to_string(modPath.filename()));
}

void ModLoader::init() {
    if (m_initialized) {
        return;
    }
    m_initialized = true;

    namespace fs = std::filesystem;
    if (!fs::is_directory(m_modsDir)) {
        Log.info(
            "mods directory '{}' not found — mod loading skipped", io::fs_path_to_string(m_modsDir));
        return;
    }

    std::error_code ec;
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(m_modsDir, ec)) {
        if (e.is_directory() && std::filesystem::exists(e.path() / "mod.json")) {
            entries.push_back(e);
        } else if (e.is_regular_file() && e.path().extension() == ".dusk") {
            entries.push_back(e);
        }
    }
    std::sort(entries.begin(), entries.end(),
        [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        });

    m_mods.reserve(entries.size());
    for (auto& entry : entries) {
        tryLoadDusk(entry.path(), entry.is_directory());
    }

    if (m_mods.empty()) {
        Log.info("no mods found");
        return;
    }


    Log.info("initializing {} mod(s)...", m_mods.size());
    for (auto& mod : mods()) {
        Register(*mod.cvarIsEnabled);

        if (!mod.cvarIsEnabled->getValue()) {
            Log.info("Mod '{}' is disabled by config", mod.metadata.id);
            mod.active = false;
        }
    }

    initializeServices();

    for (auto& mod : mods()) {
        if (mod.active && mod.native && !registerStaticServiceExports(mod)) {
            Log.error("'{}' failed to register service exports", mod.metadata.id);
        }
    }

    for (auto& mod : mods()) {
        if (mod.active && mod.native && !resolveServiceImports(mod)) {
            Log.error("'{}' failed to resolve service imports", mod.metadata.id);
        }
    }

    for (auto& mod : mods()) {
        if (!mod.active || !mod.native) {
            continue;
        }

        Log.debug("Initializing '{}'", mod.metadata.id);

        try {
            ModError error = MOD_ERROR_INIT;
            const auto result = mod.native->fn_initialize(&error);
            if (result == MOD_OK && !mod.load_failed) {
                Log.info("'{}' initialized", mod.metadata.id);
            } else {
                failMod(mod, result, lifecycleErrorMessage("mod_initialize", result, error));
            }
        } catch (const std::exception& e) {
            failMod(mod, MOD_ERROR, fmt::format("exception in mod_initialize: {}", e.what()));
        } catch (...) {
            failMod(mod, MOD_ERROR, "unknown exception in mod_initialize");
        }
    }

    initOverlayFiles();

    auto active = std::ranges::count_if(mods(), [](const LoadedMod& m) { return m.active; });
    Log.info("{}/{} mod(s) active", active, m_mods.size());
}

void ModLoader::tick() {
    for (auto& mod : mods()) {
        if (!mod.active || !mod.native) {
            continue;
        }
        try {
            ModError error = MOD_ERROR_INIT;
            const auto result = mod.native->fn_update(&error);
            if (result != MOD_OK) {
                failMod(mod, result, lifecycleErrorMessage("mod_update", result, error));
            }
        } catch (const std::exception& e) {
            failMod(mod, MOD_ERROR, fmt::format("exception in mod_update: {}", e.what()));
        } catch (...) {
            failMod(mod, MOD_ERROR, "unknown exception in mod_update");
        }
    }
}

void ModLoader::shutdown() {
    for (auto& mod : mods()) {
        if (mod.native && mod.native->fn_shutdown) {
            try {
                ModError error = MOD_ERROR_INIT;
                const auto result = mod.native->fn_shutdown(&error);
                if (result != MOD_OK) {
                    Log.error("mod_shutdown failed for '{}': {}", mod.metadata.id,
                        lifecycleErrorMessage("mod_shutdown", result, error));
                }
            } catch (...) {
            }
        }
        if (mod.context) {
            hookClearMod(mod.context.get());
        }

        OrphanedConfigVars.emplace_back(std::move(mod.cvarIsEnabled));
    }

    m_mods.clear();
    clearServices();
    Log.info("all mods unloaded");
}

}  // namespace dusk
