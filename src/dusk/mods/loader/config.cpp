#include "aurora/lib/logging.hpp"
#include "dusk/config.hpp"
#include "dusk/mod_loader.hpp"
#include "loader.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::config");

struct ModConfigVarEntry {
    uint64_t handle = 0;
    uint32_t type = 0;  // ConfigVarType
    std::unique_ptr<config::ConfigVarBase> var;
};

struct ModConfigSubscription {
    uint64_t handle = 0;
    uint64_t varHandle = 0;
    config::Subscription coreSubscription = 0;
};

// Translate the type-erased previous-value pointer into the C ABI snapshot struct. The string
// pointer aliases the previous std::string, which outlives the notification.
ConfigVarValue translate_previous(const uint32_t type, const void* previous) {
    ConfigVarValue value{};
    value.struct_size = sizeof(ConfigVarValue);
    value.type = static_cast<ConfigVarType>(type);
    switch (type) {
    case CONFIG_VAR_BOOL:
        value.bool_value = *static_cast<const bool*>(previous);
        break;
    case CONFIG_VAR_INT:
        value.int_value = *static_cast<const s64*>(previous);
        break;
    case CONFIG_VAR_FLOAT:
        value.float_value = *static_cast<const f64*>(previous);
        break;
    case CONFIG_VAR_STRING: {
        const auto* str = static_cast<const std::string*>(previous);
        value.string_value = str->c_str();
        value.string_length = str->size();
        break;
    }
    default:
        break;
    }
    return value;
}

// Snapshot the var's current (new) value for the notification. Strings are copied into
// stringStorage so the snapshot stays valid even if the callback writes the var again.
ConfigVarValue translate_current(
    const uint32_t type, config::ConfigVarBase& varBase, std::string& stringStorage) {
    ConfigVarValue value{};
    value.struct_size = sizeof(ConfigVarValue);
    value.type = static_cast<ConfigVarType>(type);
    switch (type) {
    case CONFIG_VAR_BOOL:
        value.bool_value = static_cast<ConfigVar<bool>&>(varBase).getValue();
        break;
    case CONFIG_VAR_INT:
        value.int_value = static_cast<ConfigVar<s64>&>(varBase).getValue();
        break;
    case CONFIG_VAR_FLOAT:
        value.float_value = static_cast<ConfigVar<f64>&>(varBase).getValue();
        break;
    case CONFIG_VAR_STRING:
        stringStorage = static_cast<ConfigVar<std::string>&>(varBase).getValue();
        value.string_value = stringStorage.c_str();
        value.string_length = stringStorage.size();
        break;
    default:
        break;
    }
    return value;
}

struct ModConfigRecord {
    std::vector<ModConfigVarEntry> vars;
    std::vector<ModConfigSubscription> subscriptions;
};

// Game thread only: all mutations happen in service calls made from mod code (init/update/hooks
// run inside ModLoader::tick), in the loader's deactivate paths, or at shutdown.
std::unordered_map<const LoadedMod*, ModConfigRecord> s_modConfig;
// Shared by var and subscription handles.
uint64_t s_nextHandle = 1;

bool s_dirty = false;
std::chrono::steady_clock::time_point s_lastSave{};
constexpr std::chrono::seconds kSaveDebounce{2};

}  // namespace

ModResult config_register_var(LoadedMod& mod, const ModConfigVarSpec& spec, uint64_t& outHandle) {
    outHandle = 0;
    const auto fullName =
        fmt::format("mod.{}.{}", escape_mod_id_for_config(mod.metadata.id), spec.fragment);
    if (config::GetConfigVar(fullName) != nullptr) {
        Log.error("[{}] config var '{}' conflicts with an existing config key", mod.metadata.id,
            fullName);
        return MOD_CONFLICT;
    }

    std::unique_ptr<config::ConfigVarBase> var;
    switch (spec.type) {
    case CONFIG_VAR_BOOL:
        var = std::make_unique<ConfigVar<bool>>(fullName, spec.defaultBool);
        break;
    case CONFIG_VAR_INT:
        var = std::make_unique<ConfigVar<s64>>(fullName, spec.defaultInt);
        break;
    case CONFIG_VAR_FLOAT:
        var = std::make_unique<ConfigVar<f64>>(fullName, spec.defaultFloat);
        break;
    case CONFIG_VAR_STRING:
        var = std::make_unique<ConfigVar<std::string>>(fullName, spec.defaultString);
        break;
    default:
        return MOD_INVALID_ARGUMENT;
    }

    // Back-fills a stashed/saved value (or a --cvar override) if one exists for this key.
    // Loads apply silently: the registering mod cannot have subscribed to this var yet, and it
    // reads the value right after registration anyway.
    config::Register(*var);

    auto& record = s_modConfig[&mod];
    auto& entry = record.vars.emplace_back();
    entry.handle = s_nextHandle++;
    entry.type = spec.type;
    entry.var = std::move(var);
    outHandle = entry.handle;
    return MOD_OK;
}

ModResult config_unregister_var(LoadedMod& mod, uint64_t handle) {
    const auto recordIt = s_modConfig.find(&mod);
    if (recordIt == s_modConfig.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    auto& record = recordIt->second;
    const auto entry =
        std::ranges::find_if(record.vars, [&](const auto& e) { return e.handle == handle; });
    if (entry == record.vars.end()) {
        return MOD_INVALID_ARGUMENT;
    }

    std::erase_if(record.subscriptions, [&](const ModConfigSubscription& sub) {
        if (sub.varHandle != handle) {
            return false;
        }
        config::unsubscribe(sub.coreSubscription);
        return true;
    });

    // The persisted value is stashed and restored by a future registration of the same name.
    config::unregister(*entry->var);
    record.vars.erase(entry);
    return MOD_OK;
}

config::ConfigVarBase* config_find_var(LoadedMod& mod, uint64_t handle, uint32_t expectedType) {
    const auto recordIt = s_modConfig.find(&mod);
    if (recordIt == s_modConfig.end()) {
        return nullptr;
    }
    const auto& vars = recordIt->second.vars;
    const auto entry = std::ranges::find_if(vars, [&](const auto& e) { return e.handle == handle; });
    if (entry == vars.end() || entry->type != expectedType) {
        return nullptr;
    }
    return entry->var.get();
}

ModResult config_subscribe(LoadedMod& mod, uint64_t varHandle, ConfigChangedFn callback,
    void* userData, uint64_t& outHandle) {
    outHandle = 0;
    const auto recordIt = s_modConfig.find(&mod);
    if (recordIt == s_modConfig.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    auto& record = recordIt->second;
    const auto entry =
        std::ranges::find_if(record.vars, [&](const auto& e) { return e.handle == varHandle; });
    if (entry == record.vars.end()) {
        return MOD_INVALID_ARGUMENT;
    }

    auto& sub = record.subscriptions.emplace_back();
    sub.handle = s_nextHandle++;
    sub.varHandle = varHandle;
    sub.coreSubscription = config::subscribe(entry->var->getName(),
        [modPtr = &mod, callback, userData, varHandle, type = entry->type](
            config::ConfigVarBase& varBase, const void* previous) {
            const ConfigVarValue previousValue = translate_previous(type, previous);
            std::string stringStorage;
            const ConfigVarValue currentValue = translate_current(type, varBase, stringStorage);
            try {
                callback(modPtr->context.get(), varHandle, &currentValue, &previousValue, userData);
            } catch (const std::exception& e) {
                fail_mod(*modPtr, MOD_ERROR,
                    fmt::format("exception in config change callback: {}", e.what()));
            } catch (...) {
                fail_mod(*modPtr, MOD_ERROR, "unknown exception in config change callback");
            }
        });
    outHandle = sub.handle;
    return MOD_OK;
}

ModResult config_unsubscribe(LoadedMod& mod, uint64_t handle) {
    const auto recordIt = s_modConfig.find(&mod);
    if (recordIt == s_modConfig.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    auto& subscriptions = recordIt->second.subscriptions;
    const auto sub = std::ranges::find_if(
        subscriptions, [&](const auto& s) { return s.handle == handle; });
    if (sub == subscriptions.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    config::unsubscribe(sub->coreSubscription);
    subscriptions.erase(sub);
    return MOD_OK;
}

void config_remove_mod(LoadedMod& mod) {
    const auto it = s_modConfig.find(&mod);
    if (it == s_modConfig.end()) {
        return;
    }
    for (const auto& sub : it->second.subscriptions) {
        config::unsubscribe(sub.coreSubscription);
    }
    for (const auto& entry : it->second.vars) {
        config::unregister(*entry.var);
    }
    s_modConfig.erase(it);
}

void config_mark_dirty() {
    s_dirty = true;
}

void config_flush_if_dirty(bool force) {
    if (!s_dirty) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - s_lastSave < kSaveDebounce) {
        return;
    }
    s_dirty = false;
    s_lastSave = now;
    config::save();
}

}  // namespace dusk::mods::loader
