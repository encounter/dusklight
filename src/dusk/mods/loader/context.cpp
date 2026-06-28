#include "loader.hpp"

#include "dusk/logging.h"
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

void fail_mod(LoadedMod& mod, ModResult code, std::string_view message) {
    const bool firstFailure = !mod.load_failed;
    mod.active = false;
    mod.load_failed = true;
    mod.failure_reason = message;
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
