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
}

namespace dusk {

struct ModUiTabCallback {
    ModContext* context = nullptr;
    UiTab tab{};
};

struct ModMetadata {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    bool hasCode;
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
     * Mod does not have a native library suitable for this build's platform.
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
    std::string mod_path;
    std::string dir;

    std::unique_ptr<ConfigVar<bool>> cvarIsEnabled;

    bool active = false;
    bool load_failed = false;
    std::string failure_reason;

    NativeModStatus native_status = NativeModStatus::None;
    std::unique_ptr<NativeMod> native;
    std::unique_ptr<ModContext> context;

    std::unique_ptr<mods::loader::ModBundle> bundle;

    std::vector<ModUiTabCallback> ui_tabs;
};

class ModLoader {
public:
    static ModLoader& instance();

    void setModsDir(std::filesystem::path dir) { m_modsDir = std::move(dir); }
    void init();
    void tick();
    void shutdown();

    [[nodiscard]] auto mods() const {
        return m_mods | std::views::transform([](const auto& m) -> LoadedMod& { return *m; });
    }

    [[nodiscard]] auto active_mods() const {
        return mods() | std::views::filter([](const auto& m) { return m.active; });
    }

private:
    std::vector<std::unique_ptr<LoadedMod>> m_mods;
    std::filesystem::path m_modsDir;
    bool m_initialized = false;

    void tryLoadDusk(const std::filesystem::path& modPath, bool fromDir);
    void tryLoadNativeMod(LoadedMod& mod);
    void initializeServices();
    bool registerStaticServiceExports(LoadedMod& mod);
    bool resolveServiceImports(LoadedMod& mod);
    void clearServices();
    void failMod(LoadedMod& mod, ModResult code, std::string_view message);
    void initOverlayFiles();
};

using ModIndex = std::ranges::range_difference_t<decltype(std::declval<ModLoader>().mods())>;

}  // namespace dusk
