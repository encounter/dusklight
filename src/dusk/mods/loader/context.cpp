#include "loader.hpp"

#include "dusk/logging.h"
#include "dusk/mods/svc/registry.hpp"
#include "dusk/ui/ui.hpp"
#include "fmt/format.h"

#include <chrono>

namespace dusk::mods::loader {

LoadedMod* mod_from_context(ModContext* context) {
    return context != nullptr ? context->mod : nullptr;
}

const LoadedMod* mod_from_context(const ModContext* context) {
    return context != nullptr ? context->mod : nullptr;
}

const char* mod_id_from_context(ModContext* context) {
    const auto* mod = mod_from_context(context);
    return mod != nullptr ? mod->metadata.id.c_str() : "mod";
}

bool is_safe_resource_path(std::string_view path) {
    if (path.empty() || path.starts_with('/') || path.starts_with('\\') ||
        path.find(':') != std::string_view::npos)
    {
        return false;
    }

    while (!path.empty()) {
        const auto slash = path.find_first_of("/\\");
        const auto segment = path.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }

    return true;
}

void fail_mod(LoadedMod& mod, ModResult code, std::string_view message) {
    const bool firstFailure = !mod.loadFailed;
    mod.active = false;
    mod.loadFailed = true;
    mod.failureReason = message;
    // Stop the failed mod's services from resolving; mods that required them fail in turn.
    // Pointers already handed to other mods stay callable since the library remains loaded.
    // Hooks are deliberately not touched here: fail_mod can run mid-frame (e.g. from a failing
    // hook callback), and full teardown happens via deactivateMod at a safe point.
    svc::remove_services_for_provider(mod);
    mod.servicesRegistered = false;
    // Full teardown (hooks, dylib, dependent restart) happens at the top of the next tick.
    ModLoader::instance().notify_mod_failure(mod);
    DuskLog.error("[{}] disabled: {} ({})", mod.metadata.id, message, static_cast<int>(code));
    if (firstFailure) {
        dusk::ui::push_toast({
            .type = "warning",
            .title = "Mod Disabled",
            .content = dusk::ui::escape(fmt::format("{}: {}", mod.metadata.name, message)),
            .duration = std::chrono::seconds(5),
        });
    }
}

}  // namespace dusk::mods::loader
