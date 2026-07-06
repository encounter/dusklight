#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include <string_view>

namespace dusk::mods::svc {
namespace {

constexpr size_t kMaxCheckNameLength = 256;

bool is_valid_check_name(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const std::string_view view{name};
    return !view.empty() && view.size() <= kMaxCheckNameLength;
}

ModResult item_set_check_override(ModContext* context, const char* name, uint8_t itemNo) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_check_name(name)) {
        return MOD_INVALID_ARGUMENT;
    }
    return item_check_set_override(*mod, name, itemNo);
}

ModResult item_clear_check_override(ModContext* context, const char* name) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_check_name(name)) {
        return MOD_INVALID_ARGUMENT;
    }
    return item_check_clear_override(*mod, name);
}

ModResult item_set_check_resolver(ModContext* context, const char* name, ItemCheckResolveFn fn,
    void* userData, ItemCheckHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    // NULL name = catch-all; a non-NULL name must be valid.
    if (mod == nullptr || fn == nullptr || (name != nullptr && !is_valid_check_name(name))) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = item_check_add_resolver(*mod, name, fn, userData, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult item_clear_check_resolver(ModContext* context, ItemCheckHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return item_check_remove_resolver(*mod, handle);
}

ModResult item_observe_checks(
    ModContext* context, ItemCheckObserveFn fn, void* userData, ItemCheckHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || fn == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = item_check_add_observer(*mod, fn, userData, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult item_unobserve_checks(ModContext* context, ItemCheckHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return item_check_remove_observer(*mod, handle);
}

constexpr ItemService s_itemService{
    .header = SERVICE_HEADER(ItemService, ITEM_SERVICE_MAJOR, ITEM_SERVICE_MINOR),
    .set_check_override = item_set_check_override,
    .clear_check_override = item_clear_check_override,
    .set_check_resolver = item_set_check_resolver,
    .clear_check_resolver = item_clear_check_resolver,
    .observe_checks = item_observe_checks,
    .unobserve_checks = item_unobserve_checks,
};

}  // namespace

const ItemService& item_service() {
    return s_itemService;
}
}  // namespace dusk::mods::svc
