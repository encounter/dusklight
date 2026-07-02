#pragma once

#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "dusk/config_var.hpp"
#include "mods/api.h"
#include "mods/svc/ui.h"

namespace dusk {
struct LoadedMod;
}

struct ModContext {
    dusk::LoadedMod* mod = nullptr;
};

namespace dusk::mods::loader {
class ModBundle;
class NativeModule;
}  // namespace dusk::mods::loader

namespace dusk {

struct ModUiTabCallback {
    ModContext* context = nullptr;
    UiTab tab{};
};

struct ModDependencyEdge {
    LoadedMod* mod = nullptr;
    // At least one non-optional import contributes to this edge.
    bool required = false;
};

// The dependency-relevant parts of a mod's manifest, captured at load time. Outlives the dylib
// (a runtime-disabled mod's manifest is unmapped), so the dependency graph can be rebuilt from
// it at any point — e.g. when a reload changes a mod's imports or exports.
struct ModManifestInfo {
    struct Import {
        std::string id;
        uint16_t major = 0;
        bool required = false;
        bool operator==(const Import&) const = default;
    };
    struct Export {
        std::string id;
        uint16_t major = 0;
        bool operator==(const Export&) const = default;
    };
    std::vector<Import> imports;
    std::vector<Export> exports;
    bool operator==(const ModManifestInfo&) const = default;
};

struct ModMetadata {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
};

struct NativeMod {
    std::unique_ptr<mods::loader::NativeModule> handle;
    const ModManifest* manifest = nullptr;
    ModContext** contextSymbol = nullptr;

    ModInitializeFn fn_initialize = nullptr;
    ModUpdateFn fn_update = nullptr;
    ModShutdownFn fn_shutdown = nullptr;
};

enum class NativeModStatus : u8 {
    /**
     * Mod does not have native code included.
     */
    None,

    /**
     * Native code mod loaded successfully.
     *
     * Note that this only indicates load status of the native library. If the native lib throws in
     * its init function, it will still be disabled!
     */
    Loaded,

    /**
     * This build was compiled without native mod support!
     */
    BuildDisabled,

    /**
     * Mod ships native libraries, but none matches this build's platform and architecture.
     */
    ModMissingPlatform,

    /**
     * Mod is built for a different ABI version than this build of the game.
     */
    ApiVersionMismatch,

    /**
     * Mod is missing a required native API export.
     */
    MissingExport,

    /**
     * Unknown error loading the native mod.
     */
    Unknown,
};

struct LoadedMod {
    ModMetadata metadata;
    std::string modPath;
    std::string dir;

    std::unique_ptr<ConfigVar<bool> > cvarIsEnabled;

    bool active = false;
    bool loadFailed = false;
    std::string failureReason;

    // mod_initialize succeeded; a mod_shutdown is owed on deactivation.
    bool initialized = false;
    // Static service exports are currently present in the registry.
    bool servicesRegistered = false;
    // Lifecycle state last applied by the loader; diffed against cvarIsEnabled to pick up
    // runtime enable/disable requests.
    bool enabledApplied = false;
    // Deactivated because a provider it imports from was disabled, not by its own cvar.
    bool suspendedByProvider = false;
    // Bumped per native lib extraction so every dlopen sees a fresh path (and thus a fresh
    // image with fresh statics, even if a previous dlclose did not fully unmap).
    uint32_t cacheGeneration = 0;
    // Currently extracted native library, empty if none.
    std::string nativePath;

    NativeModStatus nativeStatus = NativeModStatus::None;
    std::unique_ptr<NativeMod> native;
    std::unique_ptr<ModContext> context;

    // Shared with overlay file registrations so in-flight DVD reads survive disable/reload.
    std::shared_ptr<mods::loader::ModBundle> bundle;

    ModManifestInfo manifestInfo;

    // Mods this mod imports services from, and mods importing services from this mod.
    // Populated before initialization; entries stay valid until ModLoader::shutdown.
    std::vector<ModDependencyEdge> dependencies;
    std::vector<ModDependencyEdge> dependents;

    std::vector<ModUiTabCallback> uiTabs;
};

class ModLoader {
public:
    static ModLoader& instance();

    void set_mods_dir(std::filesystem::path dir) { m_modsDir = std::move(dir); }
    void init();
    void tick();
    void shutdown();

    // Runtime lifecycle. Enable/disable set the mod's enabled cvar; all requests are applied at
    // the top of the next tick, when no mod code is on the stack. Disabling or reloading a
    // provider restarts its transitive dependents; dependents whose required providers stay
    // down are suspended until the providers return.
    void request_enable(std::string_view id);
    void request_disable(std::string_view id);
    void request_reload(std::string_view id);
    // Queues full teardown (with dependent restart) of a mod that failed at runtime.
    void notify_mod_failure(LoadedMod& mod);

    [[nodiscard]] auto mods() const {
        return m_mods | std::views::transform([](const auto& m) -> LoadedMod& { return *m; });
    }

    [[nodiscard]] auto active_mods() const {
        return mods() | std::views::filter([](const auto& m) { return m.active; });
    }

private:
    enum class RequestKind : u8 { Enable, Disable, Reload };
    struct Request {
        std::string modId;
        RequestKind kind;
    };
    // ModLoader::tick runs inside fapGm_Execute, so a hook trampoline in an unloading mod can be
    // live on the stack (its frame unwinds after the tick). dlclose is therefore deferred to the
    // next tick, by which point every per-frame hooked function has returned.
    struct RetiredNative {
        std::unique_ptr<NativeMod> native;
        std::string path;
    };

    std::vector<std::unique_ptr<LoadedMod> > m_mods;
    std::filesystem::path m_modsDir;
    std::vector<Request> m_pendingRequests;
    std::vector<RetiredNative> m_retiredNatives;
    bool m_initialized = false;
    bool m_startupComplete = false;

    void try_load_mod(const std::filesystem::path& modPath, bool fromDir);
    void load_native(LoadedMod& mod, const std::string& dllEntry);
    void unload_native(LoadedMod& mod);
    // Registers exports (unless already registered), resolves imports and runs mod_initialize.
    // Returns whether the mod ended up active; failures go through failMod.
    bool activate_mod(LoadedMod& mod);
    // Runs mod_shutdown (if owed), removes hooks + services + UI, and unloads the native lib.
    // Must only run with no mod code on the stack (startup, shutdown, or top of tick).
    void deactivate_mod(LoadedMod& mod);
    void init_services();
    bool register_static_service_exports(LoadedMod& mod);
    bool resolve_service_imports(LoadedMod& mod);
    [[nodiscard]] std::string describe_missing_import(
        const char* serviceId, uint16_t majorVersion, uint16_t minMinorVersion) const;
    void clear_services();
    void fail_mod(LoadedMod& mod, ModResult code, std::string_view message);
    // Re-registers active mods' overlay files with the DVD layer (replace semantics).
    void sync_overlay_files();
    // Registers newly active mods' static textures/ files with Aurora and re-registers mods
    // whose load-order priority changed (m_mods can be re-sorted by a reload).
    void sync_texture_replacements();

    LoadedMod* find_mod(std::string_view id);
    void drain_retired_natives();
    void apply_pending_requests();
    // Deactivates `target` (if needed) and its transitive dependents, optionally re-reads the
    // bundle from disk, then reactivates whatever the current cvar/provider state allows.
    void apply_lifecycle_change(LoadedMod& target, bool reload);
    // `target` plus transitive active/suspended dependents, in m_mods (init) order.
    std::vector<LoadedMod*> collect_lifecycle_set(LoadedMod& target);
    bool reload_bundle(LoadedMod& mod);
    bool ensure_native_loaded(LoadedMod& mod);
};

using ModIndex = std::ranges::range_difference_t<decltype(std::declval<ModLoader>().mods())>;

}  // namespace dusk
