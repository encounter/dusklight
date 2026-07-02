#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace dusk::mods::svc {
namespace {

bool valid_var_fragment(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const std::string_view fragment{name};
    if (fragment.empty() || fragment.size() > 64) {
        return false;
    }
    return std::ranges::all_of(fragment, [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '-';
    });
}

template <typename T>
ConfigVar<T>* find_typed_var(ModContext* context, ConfigVarHandle handle, uint32_t type) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return nullptr;
    }
    // The type tag was checked, so the downcast is safe.
    return static_cast<ConfigVar<T>*>(config_find_var(*mod, handle, type));
}

ModResult config_register_var(
    ModContext* context, const ConfigVarDesc* desc, ConfigVarHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(ConfigVarDesc) ||
        !valid_var_fragment(desc->name))
    {
        return MOD_INVALID_ARGUMENT;
    }

    ModConfigVarSpec spec;
    spec.fragment = desc->name;
    spec.type = desc->type;
    switch (desc->type) {
    case CONFIG_VAR_BOOL:
        spec.defaultBool = desc->default_bool;
        break;
    case CONFIG_VAR_INT:
        spec.defaultInt = desc->default_int;
        break;
    case CONFIG_VAR_FLOAT:
        spec.defaultFloat = desc->default_float;
        break;
    case CONFIG_VAR_STRING:
        spec.defaultString = desc->default_string != nullptr ? desc->default_string : "";
        break;
    default:
        return MOD_INVALID_ARGUMENT;
    }

    uint64_t handle = 0;
    const auto result = config_register_var(*mod, spec, handle);
    if (result != MOD_OK) {
        return result;
    }
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult config_unregister_var(ModContext* context, ConfigVarHandle var) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || var == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = config_unregister_var(*mod, var);
    if (result != MOD_OK) {
        DuskLog.error("[{}] config unregister failed: unknown handle {}", mod->metadata.id, var);
    }
    return result;
}

ModResult config_get_bool(ModContext* context, ConfigVarHandle var, bool* outValue) {
    if (outValue != nullptr) {
        *outValue = false;
    }
    auto* cvar = find_typed_var<bool>(context, var, CONFIG_VAR_BOOL);
    if (cvar == nullptr || outValue == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    *outValue = cvar->getValue();
    return MOD_OK;
}

ModResult config_set_bool(ModContext* context, ConfigVarHandle var, bool value) {
    auto* cvar = find_typed_var<bool>(context, var, CONFIG_VAR_BOOL);
    if (cvar == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    cvar->setValue(value);
    config_mark_dirty();
    return MOD_OK;
}

ModResult config_get_int(ModContext* context, ConfigVarHandle var, int64_t* outValue) {
    if (outValue != nullptr) {
        *outValue = 0;
    }
    auto* cvar = find_typed_var<s64>(context, var, CONFIG_VAR_INT);
    if (cvar == nullptr || outValue == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    *outValue = cvar->getValue();
    return MOD_OK;
}

ModResult config_set_int(ModContext* context, ConfigVarHandle var, int64_t value) {
    auto* cvar = find_typed_var<s64>(context, var, CONFIG_VAR_INT);
    if (cvar == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    cvar->setValue(value);
    config_mark_dirty();
    return MOD_OK;
}

ModResult config_get_float(ModContext* context, ConfigVarHandle var, double* outValue) {
    if (outValue != nullptr) {
        *outValue = 0.0;
    }
    auto* cvar = find_typed_var<f64>(context, var, CONFIG_VAR_FLOAT);
    if (cvar == nullptr || outValue == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    *outValue = cvar->getValue();
    return MOD_OK;
}

ModResult config_set_float(ModContext* context, ConfigVarHandle var, double value) {
    auto* cvar = find_typed_var<f64>(context, var, CONFIG_VAR_FLOAT);
    if (cvar == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    cvar->setValue(value);
    config_mark_dirty();
    return MOD_OK;
}

ModResult config_get_string(
    ModContext* context, ConfigVarHandle var, char* buffer, size_t bufferSize, size_t* outLength) {
    if (outLength != nullptr) {
        *outLength = 0;
    }
    auto* cvar = find_typed_var<std::string>(context, var, CONFIG_VAR_STRING);
    if (cvar == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto& value = cvar->getValue();
    if (outLength != nullptr) {
        *outLength = value.size();
    }
    if (buffer == nullptr) {
        // Length query; any other use of a null buffer is a caller bug.
        return bufferSize == 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
    }
    if (bufferSize < value.size() + 1) {
        return MOD_INVALID_ARGUMENT;
    }
    std::memcpy(buffer, value.c_str(), value.size() + 1);
    return MOD_OK;
}

ModResult config_set_string(ModContext* context, ConfigVarHandle var, const char* value) {
    auto* cvar = find_typed_var<std::string>(context, var, CONFIG_VAR_STRING);
    if (cvar == nullptr || value == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    cvar->setValue(std::string{value});
    config_mark_dirty();
    return MOD_OK;
}

ModResult config_subscribe(ModContext* context, ConfigVarHandle var, ConfigChangedFn callback,
    void* userData, ConfigSubscriptionHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || var == 0 || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = config_subscribe(*mod, var, callback, userData, handle);
    if (result != MOD_OK) {
        return result;
    }
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult config_unsubscribe(ModContext* context, ConfigSubscriptionHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = config_unsubscribe(*mod, handle);
    if (result != MOD_OK) {
        DuskLog.error(
            "[{}] config unsubscribe failed: unknown handle {}", mod->metadata.id, handle);
    }
    return result;
}

constexpr ConfigService s_configService{
    .header = SERVICE_HEADER(ConfigService, CONFIG_SERVICE_MAJOR, CONFIG_SERVICE_MINOR),
    .register_var = config_register_var,
    .unregister_var = config_unregister_var,
    .get_bool = config_get_bool,
    .set_bool = config_set_bool,
    .get_int = config_get_int,
    .set_int = config_set_int,
    .get_float = config_get_float,
    .set_float = config_set_float,
    .get_string = config_get_string,
    .set_string = config_set_string,
    .subscribe = config_subscribe,
    .unsubscribe = config_unsubscribe,
};

}  // namespace

const ConfigService& config_service() {
    return s_configService;
}

}  // namespace dusk::mods::svc
