#include "seed_session.hpp"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mods/api.h"

#include "d/actor/d_a_player.h"
#include "d/d_com_inf_game.h"
#include "d/d_msg_flow.h"

#include "dusk/logging.h"

#include "game/item_data.hpp"
#include "game/layer_resolver.hpp"
#include "game/messages.hpp"
#include "game/randomizer_context.hpp"
#include "game/save_setup.hpp"
#include "game/stages.h"
#include "game/tools.h"
#include "game/verify_item_functions.h"

extern "C" MOD_EXPORT ModContext* mod_ctx;

// Registration flow: seed data (RandomizerContext) is loaded per save slot — the slot's
// SaveService blob records the seed hash at new-save time, and loading the slot
// re-activates that seed. Activation pushes every override the branch used to consult
// at runtime into the corresponding service; deactivation (returning to title, or
// loading a vanilla save) removes them so a vanilla session is untouched.

namespace randomizer::session {

namespace {

constexpr const char* kSeedHashBlob = "seed_hash";
constexpr const char* kSkyCharactersBlob = "sky_characters";
constexpr const char* kPendingSeedVar = "seed";

// In-memory sky-book letter count, mirrored to the slot's SaveService blob (persisted
// when the game saves, like vanilla save state).
u8 s_sky_characters = 0;

Services s_svc{};
bool s_seed_active = false;

// Live registration handles, released on deactivate.
ItemCheckHandle s_check_resolver{};
ItemGiveHandle s_check_observer{};
StageLayerHandle s_layer_resolver{};
std::vector<FlowNodeHandle> s_flow_nodes{};
std::vector<StageActorHandle> s_stage_edits{};
std::vector<std::pair<uint16_t, uint16_t>> s_text_keys{};
uint16_t s_query054_id = 0;
uint8_t s_event043_id = 0;
uint8_t s_event044_id = 0;

ConfigVarHandle s_pending_seed_var{};
SaveObserverHandle s_save_observer{};

// --- item checks -----------------------------------------------------------------

// Derived funnel names are "<tag>:<stage>:<n>"; the branch keyed the matching override
// maps by (stageID << 8) | n.
struct DerivedKey {
    int stage_id;
    u16 key;
};

std::optional<DerivedKey> parse_derived(const char* name, std::string_view prefix) {
    if (std::strncmp(name, prefix.data(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const char* stage_begin = name + prefix.size();
    const char* stage_end = std::strchr(stage_begin, ':');
    if (stage_end == nullptr) {
        return std::nullopt;
    }
    const std::string stage{stage_begin, stage_end};
    const int stage_id = getStageID(stage.c_str());
    if (stage_id < 0) {
        return std::nullopt;
    }
    const int n = std::atoi(stage_end + 1);
    return DerivedKey{stage_id, static_cast<u16>((stage_id << 8) | (n & 0xFF))};
}

// progressive: the branch ran verifyProgressiveItem on this funnel's overrides.
template <typename Map>
bool lookup_override(const Map& map, u16 key, uint8_t* out_item, bool progressive) {
    const auto it = map.find(key);
    if (it == map.end()) {
        return false;
    }
    *out_item = progressive ? static_cast<uint8_t>(verifyProgressiveItem(it->second)) : it->second;
    return true;
}

bool resolve_check(ModContext*, const ItemCheckInfo* info, uint8_t* out_item, void*) {
    if (!s_seed_active) {
        return false;
    }
    auto& ctx = randomizer_GetContext();

    // Named NPC/location checks carry the rando's own location names.
    if (auto it = ctx.mItemLocations.find(info->name); it != ctx.mItemLocations.end()) {
        *out_item = static_cast<uint8_t>(it->second.itemId);
        return true;
    }

    if (auto key = parse_derived(info->name, "chest:")) {
        return lookup_override(ctx.mTreasureChestOverrides, key->key, out_item, false);
    }
    if (auto key = parse_derived(info->name, "freestanding:")) {
        // The Gale Boomerang pedestal in Ook is a named location, not a save-bit key
        // (branch: the stage-wide special case in daObjLife_c::create).
        if (key->stage_id == Ook) {
            if (auto it = ctx.mItemLocations.find("Forest Temple Gale Boomerang");
                it != ctx.mItemLocations.end()) {
                *out_item = static_cast<uint8_t>(verifyProgressiveItem(it->second.itemId));
                return true;
            }
            return false;
        }
        return lookup_override(ctx.mFreestandingItemOverrides, key->key, out_item, true);
    }
    if (auto key = parse_derived(info->name, "poe:")) {
        return lookup_override(ctx.mPoeOverrides, key->key, out_item, false);
    }
    if (auto key = parse_derived(info->name, "shop:")) {
        return lookup_override(ctx.mShopOverrides, key->key, out_item, true);
    }
    if (auto key = parse_derived(info->name, "sky:")) {
        return lookup_override(ctx.mSkyCharacterOverrides, key->key, out_item, true);
    }
    // Bug rewards are stage-independent: "bug:<insect item id>".
    if (std::strncmp(info->name, "bug:", 4) == 0) {
        const u8 insect = static_cast<u8>(std::atoi(info->name + 4));
        if (auto it = ctx.mBugRewardOverrides.find(insect); it != ctx.mBugRewardOverrides.end()) {
            *out_item = static_cast<uint8_t>(verifyProgressiveItem(it->second));
            return true;
        }
        return false;
    }

    return false;
}

void observe_give(ModContext*, const ItemGiveInfo* info, void*) {
    if (!s_seed_active || info->check_name == nullptr) {
        return;
    }
    auto& ctx = randomizer_GetContext();
    if (ctx.mItemLocations.contains(info->check_name)) {
        randomizer_setTempFlagForLocation(info->check_name);
    }

    // Branch custom collection flags: Stallord's dungeon reward (queued from
    // daB_DS_c::executeBattle2Dead) and the Gale Boomerang pedestal in Ook.
    if (std::strcmp(info->check_name, "Arbiters Grounds Dungeon Reward") == 0) {
        dComIfGs_onItem(0x9E, -1);
    } else if (auto key = parse_derived(info->check_name, "freestanding:");
               key && key->stage_id == Ook) {
        dComIfGs_onItem(0x9D, -1);
        randomizer_setTempFlagForLocation("Forest Temple Gale Boomerang");
    }
}

// --- flow ------------------------------------------------------------------------

uint16_t flow_query054(ModContext*, uint16_t param, void*, int, void*) {
    // Branch query054: like query001, but returns 1 if the event bit is set, 0 if not.
    return dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[param]) ? 1 : 0;
}

void flow_event043(ModContext*, const uint8_t*, void*, void*) {
    // Branch event043: no-op placeholder.
}

void flow_event044(ModContext*, const uint8_t*, void*, void*) {
    // Branch event044: time-of-day change, deferred while in wolf form.
    if (daPy_py_c::checkNowWolf()) {
        g_randomizerState.setHasPendingToDChange(true);
    } else {
        g_randomizerState.handleTimeOfDayChange();
    }
}

// The seed's flow patches reference the branch's hardcoded extension indices (query 53,
// events 43/44). Rewrite them to this session's runtime-allocated ids before handing
// the node to the FlowService.
void remap_flow_node(u64& node_value) {
    auto* node = reinterpret_cast<mesg_flow_node*>(&node_value);
    if (node->type == NODETYPE_BRANCH_e) {
        auto* branch = reinterpret_cast<mesg_flow_node_branch*>(&node_value);
        if (branch->query_idx == 53) {
            branch->query_idx = s_query054_id;
        }
    } else if (node->type == NODETYPE_EVENT_e) {
        auto* event = reinterpret_cast<mesg_flow_node_event*>(&node_value);
        if (event->event_idx == 43) {
            event->event_idx = s_event043_id;
        } else if (event->event_idx == 44) {
            event->event_idx = s_event044_id;
        }
    }
}

// --- text ------------------------------------------------------------------------

const char* resolve_text(ModContext*, uint16_t group, uint16_t message_id, const char*, void*) {
    if (!s_seed_active) {
        return nullptr;
    }
    // NULL passes through to the next override / vanilla text.
    return GetTextOverride(group, message_id);
}

// --- stage -----------------------------------------------------------------------

bool resolve_layer(ModContext*, const char* stage, int32_t room_no, int32_t, int32_t* out_layer,
    void*) {
    if (!s_seed_active || !randomizer_IsActive()) {
        return false;
    }
    int layer = -1;
    if (layers::resolve_layer(stage, room_no, &layer)) {
        *out_layer = layer;
        return true;
    }
    return false;
}

void register_stage_edits() {
    auto& ctx = randomizer_GetContext();
    auto stage_of = [](u32 key) -> const char* {
        const u32 stage_id = key >> 16;
        if (stage_id >= sizeof(allStages) / sizeof(allStages[0])) {
            return nullptr;
        }
        return allStages[stage_id];
    };

    for (const auto& [key, patches] : ctx.mObjectPatches) {
        const char* stage = stage_of(key);
        if (stage == nullptr) {
            continue;
        }
        const u8 room = (key >> 8) & 0xFF;
        const u8 layer = key & 0xFF;
        for (const auto& [crc, bytes] : patches) {
            StageActorHandle handle{};
            ModResult res;
            if (bytes.size() == RandomizerContext::OBJ_DELETE_SIZE) {
                res = s_svc.stage->delete_actor(mod_ctx, stage, room, layer, crc, &handle);
            } else {
                res = s_svc.stage->patch_actor(
                    mod_ctx, stage, room, layer, crc, bytes.data(), bytes.size(), &handle);
            }
            if (res == MOD_OK) {
                s_stage_edits.push_back(handle);
            }
        }
    }

    for (const auto& [key, additions] : ctx.mObjectAdditions) {
        const char* stage = stage_of(key);
        if (stage == nullptr) {
            continue;
        }
        const u8 room = (key >> 8) & 0xFF;
        const u8 layer = key & 0xFF;
        for (const auto& bytes : additions) {
            StageActorHandle handle{};
            if (s_svc.stage->add_actor(mod_ctx, stage, room, layer, bytes.data(), bytes.size(),
                    &handle) == MOD_OK) {
                s_stage_edits.push_back(handle);
            }
        }
    }
}

// --- save lifecycle ----------------------------------------------------------------

std::string pending_seed_hash() {
    size_t length = 0;
    if (s_svc.config->get_string(mod_ctx, s_pending_seed_var, nullptr, 0, &length) != MOD_OK ||
        length == 0) {
        return {};
    }
    std::string hash(length, '\0');
    if (s_svc.config->get_string(mod_ctx, s_pending_seed_var, hash.data(), length + 1, nullptr) !=
        MOD_OK) {
        return {};
    }
    return hash;
}

void on_new_save(ModContext*, uint32_t, void*) {
    const std::string hash = pending_seed_hash();
    if (hash.empty()) {
        return;
    }
    if (!activate_seed(hash.c_str())) {
        return;
    }
    // Remember the seed on the slot, then apply the seed's starting state to the fresh
    // save (branch: dComIfGs_setupRandomizerSave from dFile_select_c::nameInput2).
    s_svc.save->set_blob(mod_ctx, kSeedHashBlob, hash.data(), hash.size());
    save_setup::setup_new_save();
}

void load_sky_characters() {
    s_sky_characters = 0;
    size_t size = sizeof(s_sky_characters);
    s_svc.save->get_blob(mod_ctx, kSkyCharactersBlob, &s_sky_characters, &size);
}

void on_save_loaded(ModContext*, uint32_t, void*) {
    size_t size = 0;
    if (s_svc.save->get_blob(mod_ctx, kSeedHashBlob, nullptr, &size) != MOD_OK || size == 0) {
        // Vanilla save.
        deactivate_seed();
        return;
    }
    std::string hash(size, '\0');
    if (s_svc.save->get_blob(mod_ctx, kSeedHashBlob, hash.data(), &size) != MOD_OK) {
        deactivate_seed();
        return;
    }
    if (randomizer_GetContext().mHash != hash) {
        deactivate_seed();
        activate_seed(hash.c_str());
    }
    load_sky_characters();
}

}  // namespace

bool activate_seed(const char* hash) {
    if (s_seed_active) {
        deactivate_seed();
    }

    auto& ctx = randomizer_GetContext();
    ctx = RandomizerContext{};
    if (auto err = ctx.LoadFromHash(hash); err.has_value() || ctx.mHash.empty()) {
        DuskLog.error("randomizer: failed to load seed {}", hash);
        return false;
    }

    // Custom item models/resources: mutate the vanilla dItem_data tables in place
    // (restore on deactivate). Migrate to ItemService slot claims when those land.
    item_data::apply_tables();

    s_svc.item->set_check_resolver(mod_ctx, nullptr, resolve_check, nullptr, &s_check_resolver);
    s_svc.item->observe_gives(mod_ctx, observe_give, nullptr, &s_check_observer);

    // Flow: session-allocated extension ids, then the seed's node overrides (remapped).
    s_svc.flow->register_query(
        mod_ctx, "dev.twilitrealm.randomizer:query054", flow_query054, nullptr, &s_query054_id);
    s_svc.flow->register_event(
        mod_ctx, "dev.twilitrealm.randomizer:event043", flow_event043, nullptr, &s_event043_id);
    s_svc.flow->register_event(
        mod_ctx, "dev.twilitrealm.randomizer:event044", flow_event044, nullptr, &s_event044_id);
    for (const auto& [key, value] : ctx.mFlowPatches) {
        u64 node_value = value;
        remap_flow_node(node_value);
        FlowNodeHandle handle{};
        if (s_svc.flow->override_flow_node(mod_ctx, static_cast<uint16_t>(key >> 16),
                static_cast<uint16_t>(key & 0xFFFF), &node_value, &handle) == MOD_OK) {
            s_flow_nodes.push_back(handle);
        }
    }

    // Text: one resolver per overridden message of the active language; the callback
    // formats dynamic messages (poe count, sky characters) at display time.
    const u8 language = getLanguageForOverride();
    for (const auto& [key, text] : ctx.mTextOverrides[language]) {
        const auto group = static_cast<uint16_t>(key >> 16);
        const auto message_id = static_cast<uint16_t>(key & 0xFFFF);
        if (s_svc.text->override_message_fn(mod_ctx, group, message_id, resolve_text, nullptr) ==
            MOD_OK) {
            s_text_keys.emplace_back(group, message_id);
        }
    }

    register_stage_edits();
    s_svc.stage->register_layer_resolver(mod_ctx, resolve_layer, nullptr, &s_layer_resolver);

    s_seed_active = true;
    DuskLog.info("randomizer: seed {} active", ctx.mHash);
    return true;
}

void deactivate_seed() {
    if (!s_seed_active && randomizer_GetContext().mHash.empty()) {
        return;
    }

    if (s_check_resolver != 0) {
        s_svc.item->clear_check_resolver(mod_ctx, s_check_resolver);
        s_check_resolver = 0;
    }
    if (s_check_observer != 0) {
        s_svc.item->unobserve_gives(mod_ctx, s_check_observer);
        s_check_observer = 0;
    }
    for (auto handle : s_flow_nodes) {
        s_svc.flow->clear_flow_node_override(mod_ctx, handle);
    }
    s_flow_nodes.clear();
    if (s_query054_id != 0) {
        s_svc.flow->unregister_query(mod_ctx, s_query054_id);
        s_query054_id = 0;
    }
    if (s_event043_id != 0) {
        s_svc.flow->unregister_event(mod_ctx, s_event043_id);
        s_event043_id = 0;
    }
    if (s_event044_id != 0) {
        s_svc.flow->unregister_event(mod_ctx, s_event044_id);
        s_event044_id = 0;
    }
    for (const auto& [group, message_id] : s_text_keys) {
        s_svc.text->clear_message_override(mod_ctx, group, message_id);
    }
    s_text_keys.clear();
    for (auto handle : s_stage_edits) {
        s_svc.stage->remove_actor_edit(mod_ctx, handle);
    }
    s_stage_edits.clear();
    if (s_layer_resolver != 0) {
        s_svc.stage->unregister_layer_resolver(mod_ctx, s_layer_resolver);
        s_layer_resolver = 0;
    }

    item_data::restore_tables();
    randomizer_GetContext() = RandomizerContext{};
    g_randomizerState = RandomizerState{};
    s_seed_active = false;
}

ModResult initialize(const Services& services) {
    s_svc = services;

    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = kPendingSeedVar;
    desc.type = CONFIG_VAR_STRING;
    desc.default_string = "";
    ModResult res = s_svc.config->register_var(mod_ctx, &desc, &s_pending_seed_var);
    if (res != MOD_OK) {
        return res;
    }

    return s_svc.save->observe_saves(
        mod_ctx, on_new_save, on_save_loaded, nullptr, nullptr, &s_save_observer);
}

ConfigVarHandle pending_seed_var() {
    return s_pending_seed_var;
}

uint8_t sky_characters() {
    return s_sky_characters;
}

void set_sky_characters(uint8_t num) {
    s_sky_characters = num;
    s_svc.save->set_blob(mod_ctx, kSkyCharactersBlob, &s_sky_characters, sizeof(s_sky_characters));
}

void update() {
    // Returning to the title screen ends the slot: drop the seed session (matches the
    // branch's RandomizerState reset when inactive; on_save_loaded re-activates).
    if (s_seed_active && playerIsOnTitleScreen() && !randomizer_GetContext().mCreatingSave) {
        deactivate_seed();
        return;
    }

    // Branch fpcM_Management insertion: lazy _create + execute while active, reset
    // when not. (mod_update runs inside fapGm_Execute; see PORTING.md for the timing
    // difference vs the branch's pre-fpcEx_Handler spot.)
    if (randomizer_IsActive()) {
        if (!g_randomizerState.mInitialized) {
            g_randomizerState._create();
        }
        g_randomizerState.execute();
    } else if (g_randomizerState.mInitialized) {
        g_randomizerState = RandomizerState{};
    }
}

}  // namespace randomizer::session
