#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/item_checks.hpp"
#include "loader.hpp"

#include "d/d_com_inf_game.h"

#include <fmt/format.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::item_checks");

struct CheckValueOverride {
    std::string name;
    uint8_t itemNo = 0;
};

struct CheckResolver {
    uint64_t handle = 0;
    std::string name;  // empty = catch-all (every check)
    ItemCheckResolveFn fn = nullptr;
    void* userData = nullptr;
};

struct CheckObserver {
    uint64_t handle = 0;
    ItemCheckObserveFn fn = nullptr;
    void* userData = nullptr;
};

struct ModItemChecks {
    std::vector<CheckValueOverride> overrides;
    std::vector<CheckResolver> resolvers;
    std::vector<CheckObserver> observers;
};

// Game thread only: all mutations happen in service calls made from mod code (init/update/hooks
// run inside ModLoader::tick), in the loader's deactivate path, or at shutdown — and item_check
// itself fires from game code on the game thread.
std::unordered_map<LoadedMod*, ModItemChecks> s_modChecks;
uint64_t s_nextCheckHandle = 1;

// Same-name value overrides from two mods are a legitimate-but-suspect setup; warn once per name.
std::unordered_set<std::string> s_warnedCollisions;

// Applicable work items snapshotted before any callback runs, so callbacks can (un)register
// freely; mod liveness is re-checked per invocation because fail_mod can trip mid-chain.
struct PendingResolve {
    LoadedMod* mod = nullptr;
    bool isValue = false;
    uint8_t itemNo = 0;
    ItemCheckResolveFn fn = nullptr;
    void* userData = nullptr;
};

struct PendingObserve {
    LoadedMod* mod = nullptr;
    ItemCheckObserveFn fn = nullptr;
    void* userData = nullptr;
};

}  // namespace

uint8_t item_check(const char* name, uint8_t itemNo, fopAc_ac_c* giver) {
    if (name == nullptr || *name == '\0' || s_modChecks.empty()) {
        return itemNo;
    }

    // Collect in m_mods (load/dependency) order: the later-loaded mod's overrides apply last
    // and win, matching the texture-replacement priority rule.
    std::vector<PendingResolve> resolves;
    std::vector<PendingObserve> observes;
    LoadedMod* previousValueOwner = nullptr;
    for (auto& mod : ModLoader::instance().mods()) {
        if (!mod.active) {
            continue;
        }
        const auto recordIt = s_modChecks.find(&mod);
        if (recordIt == s_modChecks.end()) {
            continue;
        }
        const auto& record = recordIt->second;
        for (const auto& override_ : record.overrides) {
            if (override_.name == name) {
                if (previousValueOwner != nullptr && !s_warnedCollisions.contains(name)) {
                    s_warnedCollisions.emplace(name);
                    Log.warn("check '{}' overridden by both [{}] and [{}]; [{}] wins (load order)",
                        name, previousValueOwner->metadata.id, mod.metadata.id, mod.metadata.id);
                }
                previousValueOwner = &mod;
                resolves.push_back({.mod = &mod, .isValue = true, .itemNo = override_.itemNo});
            }
        }
        for (const auto& resolver : record.resolvers) {
            if (resolver.name.empty() || resolver.name == name) {
                resolves.push_back(
                    {.mod = &mod, .fn = resolver.fn, .userData = resolver.userData});
            }
        }
        for (const auto& observer : record.observers) {
            observes.push_back({.mod = &mod, .fn = observer.fn, .userData = observer.userData});
        }
    }
    if (resolves.empty() && observes.empty()) {
        return itemNo;
    }

    ItemCheckInfo info{
        .name = name,
        .giver_actor = giver,
        .vanilla_item = itemNo,
        .current_item = itemNo,
    };
    for (const auto& resolve : resolves) {
        if (!resolve.mod->active) {
            continue;
        }
        if (resolve.isValue) {
            info.current_item = resolve.itemNo;
            continue;
        }
        uint8_t resolved = info.current_item;
        try {
            if (resolve.fn(resolve.mod->context.get(), &info, &resolved, resolve.userData)) {
                info.current_item = resolved;
            }
        } catch (const std::exception& e) {
            fail_mod(*resolve.mod, MOD_ERROR,
                fmt::format("exception in item check resolver for '{}': {}", name, e.what()));
        } catch (...) {
            fail_mod(*resolve.mod, MOD_ERROR,
                fmt::format("unknown exception in item check resolver for '{}'", name));
        }
    }

    for (const auto& observe : observes) {
        if (!observe.mod->active) {
            continue;
        }
        try {
            observe.fn(observe.mod->context.get(), &info, observe.userData);
        } catch (const std::exception& e) {
            fail_mod(*observe.mod, MOD_ERROR,
                fmt::format("exception in item check observer for '{}': {}", name, e.what()));
        } catch (...) {
            fail_mod(*observe.mod, MOD_ERROR,
                fmt::format("unknown exception in item check observer for '{}'", name));
        }
    }
    return info.current_item;
}

uint8_t item_check_chest(uint8_t boxNo, uint8_t itemNo, fopAc_ac_c* chest) {
    if (s_modChecks.empty()) {
        return itemNo;
    }
    const auto name = fmt::format("chest:{}:{}", dComIfGp_getStartStageName(), boxNo);
    return item_check(name.c_str(), itemNo, chest);
}

uint8_t item_check_boss(uint8_t itemNo) {
    if (s_modChecks.empty()) {
        return itemNo;
    }
    const auto name = fmt::format("boss:{}", dComIfGp_getStartStageName());
    return item_check(name.c_str(), itemNo, nullptr);
}

ModResult item_check_set_override(LoadedMod& mod, const char* name, uint8_t itemNo) {
    auto& record = s_modChecks[&mod];
    for (auto& override_ : record.overrides) {
        if (override_.name == name) {
            override_.itemNo = itemNo;
            return MOD_OK;
        }
    }
    record.overrides.push_back({.name = name, .itemNo = itemNo});
    return MOD_OK;
}

ModResult item_check_clear_override(LoadedMod& mod, const char* name) {
    const auto recordIt = s_modChecks.find(&mod);
    if (recordIt == s_modChecks.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto removed = std::erase_if(recordIt->second.overrides,
        [&](const auto& override_) { return override_.name == name; });
    return removed != 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
}

ModResult item_check_add_resolver(
    LoadedMod& mod, const char* name, ItemCheckResolveFn fn, void* userData, uint64_t& outHandle) {
    auto& record = s_modChecks[&mod];
    auto& resolver = record.resolvers.emplace_back();
    resolver.handle = s_nextCheckHandle++;
    resolver.name = name != nullptr ? name : "";
    resolver.fn = fn;
    resolver.userData = userData;
    outHandle = resolver.handle;
    return MOD_OK;
}

ModResult item_check_remove_resolver(LoadedMod& mod, uint64_t handle) {
    const auto recordIt = s_modChecks.find(&mod);
    if (recordIt == s_modChecks.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto removed = std::erase_if(recordIt->second.resolvers,
        [&](const auto& resolver) { return resolver.handle == handle; });
    return removed != 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
}

ModResult item_check_add_observer(
    LoadedMod& mod, ItemCheckObserveFn fn, void* userData, uint64_t& outHandle) {
    auto& record = s_modChecks[&mod];
    auto& observer = record.observers.emplace_back();
    observer.handle = s_nextCheckHandle++;
    observer.fn = fn;
    observer.userData = userData;
    outHandle = observer.handle;
    return MOD_OK;
}

ModResult item_check_remove_observer(LoadedMod& mod, uint64_t handle) {
    const auto recordIt = s_modChecks.find(&mod);
    if (recordIt == s_modChecks.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto removed = std::erase_if(recordIt->second.observers,
        [&](const auto& observer) { return observer.handle == handle; });
    return removed != 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
}

void item_checks_remove_mod(LoadedMod& mod) {
    s_modChecks.erase(&mod);
}

}  // namespace dusk::mods
