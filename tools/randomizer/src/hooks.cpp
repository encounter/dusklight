#include "hooks.hpp"

#include "mods/hook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_event.h"
#include "d/d_item.h"
#include "d/d_meter2_info.h"
#include "d/d_msg_flow.h"
#include "d/d_save.h"

#include <cstring>

#include "game/assets.hpp"
#include "game/flags.h"
#include "game/item_funcs.hpp"
#include "game/item_ids.hpp"
#include "game/randomizer_context.hpp"
#include "game/stages.h"
#include "game/tools.h"

// Each hook below re-expresses one of the randomizer branch's mid-function edits to
// game code as a pre/post hook: pre-hooks handle the "special case -> early return"
// insertions by writing the return value and skipping the original, and side-effect
// insertions by running before the original. See tools/randomizer/PORTING.md for the
// branch provenance of every case.

namespace randomizer::hooks {

namespace {

using dusk::mods::arg;

bool is_current_memory_bit(const dSv_memBit_c* self) {
    return self == &g_dComIfG_gameInfo.info.getMemory().getBit();
}

// --- d_save.cpp: dSv_event_c ---------------------------------------------------------

HookAction pre_is_event_bit(ModContext*, void* args, void* retval, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    const u16 i_no = arg<u16>(args, 1);
    auto& out = *static_cast<BOOL*>(retval);
    switch (i_no) {
    case BO_TALKED_TO_YOU_AFTER_OPENING_IRON_BOOTS_CHEST: {
        if (daAlink_c::checkStageName(allStages[Ordon_Village_Interiors])) {
            out = dComIfGs_isEventBit(HEARD_BO_TEXT_AFTER_SUMO_FIGHT) ? TRUE : FALSE;
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    case GAVE_ILIA_HER_CHARM:    // Gave Ilia the charm
    case CITY_OOCCOO_CS_WATCHED: // CiTS Intro CS watched
    {
        if (daAlink_c::checkStageName(allStages[Hidden_Village])) {
            if (!dComIfGs_isEventBit(GOT_ILIAS_CHARM)) {
                // If we haven't gotten the item from Impaz then we need to return false or it
                // will break her dialogue.
                out = FALSE;
                return HOOK_SKIP_ORIGINAL;
            }
        }
        break;
    }
    case GORON_MINES_CLEARED: {
        if (daAlink_c::checkStageName(allStages[Goron_Mines]) ||
            daAlink_c::checkStageName(allStages[Death_Mountain_Interiors])) {
            out = FALSE; // The gorons will not act properly if the flag is set.
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    case ZORA_ESCORT_CLEARED: {
        if (daAlink_c::checkStageName(allStages[Castle_Town])) {
            // If the flag isn't set the player will be thrown into escort when they open the door
            out = TRUE;
            return HOOK_SKIP_ORIGINAL;
        }
        if (playerIsInRoomStage(0, allStages[Kakariko_Village_Interiors])) {
            out = TRUE; // Return true to prevent Renado/Ilia crash after ToT
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    case CITY_IN_THE_SKY_CLEARED: // Would like to find where this is checked and patch it there.
    {
        if (!dComIfGs_isEventBit(FIXED_THE_MIRROR_OF_TWILIGHT)) {
            if (randomizer_GetContext().mSettings[RandomizerContext::PALACE_OF_TWILIGHT_REQUIREMENTS] !=
                RandomizerContext::VANILLA) {
                out = FALSE;
                return HOOK_SKIP_ORIGINAL;
            }
        }
        break;
    }
    case HOWLED_AT_SNOWPEAK_STONE: {
        if (daAlink_c::checkStageName(allStages[Snowpeak])) {
            // return false so the player can howl at the stone multiple times to remove map glitch
            out = FALSE;
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    case WATCHED_CUTSCENE_AFTER_GOATS_2: {
        if (playerIsInRoomStage(1, allStages[Ordon_Village_Interiors])) {
            // false -> Sera gives the milk item once they help the cat;
            // true -> the shop is always usable even if the cat is not returned.
            out = dComIfGs_isEventBit(SERAS_CAT_RETURNED_TO_SHOP) ? FALSE : TRUE;
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    case FIXED_THE_MIRROR_OF_TWILIGHT: {
        if (daAlink_c::checkStageName(allStages[Palace_of_Twilight])) {
            out = TRUE; // If the flag is not set, the player cannot leave PoT from the inside.
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    default:
        break;
    }
    return HOOK_CONTINUE;
}

HookAction pre_on_event_bit(ModContext*, void* args, void*, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    const u16 i_no = arg<u16>(args, 1);
    switch (i_no) {
    // Wolf <-> Human crash patches/bug fixes: some cutscenes/events either crash or act
    // weird if Link is in the wrong form and the game no longer auto-transforms once the
    // Shadow Crystal has been obtained.
    case ENTERED_ORDON_SPRING_DAY_3:
        if (dComIfGs_isEventBit(TRANSFORMING_UNLOCKED)) {
            dComIfGs_setTransformStatus(0);
        }
        break;

    case WATCHED_CUTSCENE_AFTER_BEING_CAPTURED_IN_FARON_TWILIGHT:
        if (dComIfGs_isEventBit(TRANSFORMING_UNLOCKED)) {
            dComIfGs_setTransformStatus(1);
        }
        break;

    case MIDNAS_DESPERATE_HOUR_COMPLETED:
        dComIfGs_onDarkClearLV(3);
        break;

    case CLEARED_FARON_TWILIGHT:
        // If we've already cleared Eldin Twilight, Lanayru Twilight, and MDH
        if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED)) {
            if (dComIfGs_isDarkClearLV(2) && dComIfGs_isDarkClearLV(3)) {
                // Set the flag for the last transformed twilight; also puts Midna on the
                // player's back
                dComIfGs_onTransformLV(3);
                dComIfGs_onDarkClearLV(3);
            }
        }
        break;

    case CLEARED_ELDIN_TWILIGHT:
        dComIfGs_onEventBit(MAP_WARPING_UNLOCKED); // in glitched logic, you can skip the gorge bridge
        if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED)) {
            if (dComIfGs_isDarkClearLV(1) && dComIfGs_isDarkClearLV(3)) {
                dComIfGs_onTransformLV(3);
                dComIfGs_onDarkClearLV(3);
            }
        }
        // Set flag for the bridge between Castle Town and Eldin field if skip bridge
        // donation is on and both Eldin and Lanayru twilight are cleared
        if (dComIfGs_isEventBit(CLEARED_LANAYRU_TWILIGHT) &&
            randomizer_GetContext().mSettings[RandomizerContext::SKIP_BRIDGE_DONATION] ==
                RandomizerContext::ON) {
            dComIfGs_onEventBit(BRIDGE_REPAIR_FUNDRAISING_COMPLETED);
            dComIfGs_onStageSwitch(6, 0x1B); // Bridge exists
        }
        break;

    case CLEARED_LANAYRU_TWILIGHT:
        if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED)) {
            if (dComIfGs_isDarkClearLV(1) && dComIfGs_isDarkClearLV(2)) {
                dComIfGs_onTransformLV(3);
                dComIfGs_onDarkClearLV(3);
            }
        }
        if (dComIfGs_isEventBit(CLEARED_ELDIN_TWILIGHT) &&
            randomizer_GetContext().mSettings[RandomizerContext::SKIP_BRIDGE_DONATION] ==
                RandomizerContext::ON) {
            dComIfGs_onEventBit(BRIDGE_REPAIR_FUNDRAISING_COMPLETED);
            dComIfGs_onStageSwitch(6, 0x1B); // Bridge exists
        }
        break;

    case REMOVE_SWORD_SHIELD_FROM_WOLF_BACK:
        if (!dComIfGs_isEventBit(CLEARED_FARON_TWILIGHT)) {
            dComIfGs_onTransformLV(0); // Set the last transformed twilight to include Faron
        }
        break;

    case GAVE_TELMA_RENADOS_LETTER:
        offWarashibeItem(dItemNo_Randomizer_LETTER_e);
        break;

    default:
        break;
    }
    return HOOK_CONTINUE;
}

// --- d_save.cpp: dSv_memBit_c --------------------------------------------------------

HookAction pre_membit_is_switch(ModContext*, void* args, void* retval, void*) {
    if (randomizer_IsActive() && getStageID() == Hidden_Village_Interiors) {
        if (arg<int>(args, 1) == 0x61) { // Is Impaz in her house
            *static_cast<BOOL*>(retval) = TRUE;
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

HookAction pre_membit_on_switch(ModContext*, void* args, void*, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    auto* self = arg<dSv_memBit_c*>(args, 0);
    const int i_no = arg<int>(args, 1);
    if (is_current_memory_bit(self)) {
        if (getStageID() == Arbiters_Grounds) {
            // Poe flame CS trigger
            if (i_no == 0x26) {
                self->offSwitch(0x45); // Open the Poe gate
                return HOOK_SKIP_ORIGINAL;
            }
        } else if (getStageID() == Lake_Hylia) {
            // Lanayru Twilight End CS trigger
            if (i_no == 0xD) {
                if (dComIfGs_isEventBit(TRANSFORMING_UNLOCKED)) {
                    // Set player to Human as the game will not do so if Shadow Crystal has
                    // been obtained.
                    dComIfGs_setTransformStatus(0);
                }
            }
        } else if (getStageID() == Kakariko_Village) {
            // Hawkeye is for sale
            if (i_no == 0x3E) {
                self->offSwitch(0xB); // Remove the coming soon sign so the hawkeye can be bought
            }
        } else if (getStageID() == Hyrule_Field) {
            // Destroyed North Eldin rocks barrier
            if (i_no == 0x11) {
                // Unlock Eldin Province on the map. Done manually rather than via
                // `onRegionBit`, which would see the rocks unbroken and skip the region.
                g_dComIfG_gameInfo.info.getPlayer().getPlayerFieldLastStayInfo().mRegion |= 0x08;
            }
        }
    }
    return HOOK_CONTINUE;
}

HookAction pre_on_dungeon_item(ModContext*, void* args, void*, void*) {
    // Don't use the stage life collection flag for rando
    if (randomizer_IsActive() && arg<int>(args, 1) == dSv_memBit_c::STAGE_LIFE) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction pre_off_dungeon_item(ModContext*, void* args, void*, void*) {
    if (randomizer_IsActive() && arg<int>(args, 1) == dSv_memBit_c::STAGE_LIFE) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction pre_is_dungeon_item(ModContext*, void* args, void* retval, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    const int i_no = arg<int>(args, 1);
    auto& out = *static_cast<s32*>(retval);
    switch (i_no) {
    case dSv_memBit_c::STAGE_LIFE:
        out = FALSE;
        return HOOK_SKIP_ORIGINAL;
    case dSv_memBit_c::STAGE_BOSS_ENEMY: {
        // If we are in a dungeon or fighting a midboss, we don't want the boss being
        // defeated to affect the gameplay.
        static const char* dungeonStages[] = {
            "D_MN05", "D_MN05B", "D_MN04", "D_MN04B", "D_MN01", "D_MN01B", "D_MN10", "D_MN10B",
            "D_MN11", "D_MN11B", "D_MN06", "D_MN06B", "D_MN07", "D_MN07B", "D_MN08", "D_MN08B",
            "D_MN08C"};
        for (const char* stage : dungeonStages) {
            if (daAlink_c::checkStageName(stage)) {
                out = FALSE;
                return HOOK_SKIP_ORIGINAL;
            }
        }
        break;
    }
    case dSv_memBit_c::STAGE_BOSS_ENEMY_2: {
        // If we are in the early rooms of FT, we don't want Ook being defeated to affect
        // gameplay
        if (daAlink_c::checkStageName("D_MN05") && dComIfGp_roomControl_getStayNo() < 4) {
            out = FALSE;
            return HOOK_SKIP_ORIGINAL;
        }
        break;
    }
    default:
        break;
    }
    return HOOK_CONTINUE;
}

HookAction pre_on_stage_boss_enemy(ModContext*, void* args, void*, void*) {
    if (randomizer_IsActive()) {
        // Don't turn Ooccoo into the note when defeating a boss
        arg<dSv_memBit_c*>(args, 0)->onDungeonItem(dSv_memBit_c::STAGE_BOSS_ENEMY);
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// --- d_save.cpp: player status/items -------------------------------------------------

HookAction pre_is_dark_clear_lv(ModContext*, void* args, void* retval, void*) {
    if (randomizer_IsActive() && arg<int>(args, 1) == 0 &&
        playerIsInRoomStage(1, allStages[Ordon_Village_Interiors])) {
        // Return false so Sera will give us the bottle if we have rescued the cat.
        *static_cast<BOOL*>(retval) = FALSE;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction pre_check_empty_bottle(ModContext*, void*, void* retval, void*) {
    if (randomizer_IsActive() && getStageID() == Cave_of_Ordeals) {
        // Return 1 to allow the player to collect the floor 50 reward, as this makes the
        // game think the player has an empty bottle.
        *static_cast<u8*>(retval) = 1;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void post_set_line_up_item(ModContext*, void* args, void*, void*) {
    // Allow rando to use all item slots. Checks the loaded hash rather than
    // randomizer_IsActive() because this runs on file select.
    if (randomizer_GetContext().mHash.empty()) {
        return;
    }
    auto* self = arg<dSv_player_item_c*>(args, 0);
    if (self->mItems[7] == dItemNo_NONE_e) {
        return;
    }
    // Mirror the branch: append slot 7 after the vanilla lineup, unless already present.
    int slot_idx = 0;
    for (; slot_idx < 24; slot_idx++) {
        const u8 lineup = self->mItemSlots[slot_idx];
        if (lineup == 7) {
            return;
        }
        if (lineup == 0xFF) {
            break;
        }
    }
    if (slot_idx < 24) {
        self->mItemSlots[slot_idx] = 7;
    }
}

// --- d_save.cpp: dSv_info_c ----------------------------------------------------------

HookAction pre_info_on_switch(ModContext*, void* args, void*, void*) {
    // Set custom flag for the Temple of Time pedestal strike
    if (randomizer_IsActive() && getStageID() == Sacred_Grove && arg<int>(args, 1) == 0xEE) {
        arg<dSv_info_c*>(args, 0)->onSwitch(0x63, arg<int>(args, 2));
    }
    return HOOK_CONTINUE;
}

// --- d_msg_flow.cpp: vanilla query/event tweaks --------------------------------------

HookAction pre_query001(ModContext*, void* args, void* retval, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    auto* node = arg<mesg_flow_node_branch*>(args, 1);
    if (node->param == 0xFA) { // MDH Completed
        // Return 0 to be able to turn souls into Jovani pre MDH
        if (playerIsInRoomStage(5, allStages[Castle_Town_Shops])) {
            *static_cast<u16*>(retval) = 0;
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

HookAction pre_query022(ModContext*, void* args, void* retval, void*) {
    if (randomizer_IsActive() && daAlink_c::checkStageName(allStages[Ordon_Village_Interiors])) {
        auto* node = arg<mesg_flow_node_branch*>(args, 1);
        if ((node->param & 0xFF) == dItemNo_Randomizer_HVY_BOOTS_e) {
            // Return false so that the door in Bo's house can be opened without the Iron Boots
            *static_cast<u16*>(retval) = 0;
            return HOOK_SKIP_ORIGINAL;
        }
    }
    return HOOK_CONTINUE;
}

HookAction pre_query025(ModContext*, void* args, void* retval, void*) {
    // 0x4461 is the key for the red potion shop item
    if (randomizer_IsActive() && playerIsInRoomStage(3, allStages[Kakariko_Village_Interiors]) &&
        randomizer_GetContext().mShopOverrides.contains(0x4461)) {
        // Return 0 so the player can buy the red potion item from the shop.
        *static_cast<u16*>(retval) = 0;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void post_query049(ModContext*, void*, void* retval, void*) {
    // Split up getting both rewards from Jovani in randomizer
    auto& out = *static_cast<u16*>(retval);
    if (randomizer_IsActive() && out == 4 && !dComIfGs_isEventBit(GOT_BOTTLE_FROM_JOVANI)) {
        out = 3;
    }
}

HookAction pre_event035(ModContext*, void* args, void* retval, void*) {
    if (!randomizer_IsActive()) {
        return HOOK_CONTINUE;
    }
    // Reimplements the whole (small) vanilla body with the rando behavior differences:
    // don't clear SLOT_19 for the memo/scribbling, and remove the specific warashibe
    // item instead of clearing the slot.
    auto* node = arg<mesg_flow_node_event*>(args, 1);
    const u8* p = node->params;
    const int prm0 = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];

    if (prm0 == dItemNo_TOMATO_PUREE_e || prm0 == dItemNo_TASTE_e) {
        dComIfGs_offItemFirstBit(prm0);
    } else if (prm0 == dItemNo_RAFRELS_MEMO_e || prm0 == dItemNo_ASHS_SCRIBBLING_e) {
        // rando: keep SLOT_19 (the items are randomized)
    } else if (prm0 == dItemNo_LETTER_e || prm0 == dItemNo_BILL_e ||
               prm0 == dItemNo_WOOD_STATUE_e || prm0 == dItemNo_IRIAS_PENDANT_e) {
        offWarashibeItem(prm0);
    }
    *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

// --- d_item.cpp: item dispatch -------------------------------------------------------

HookAction pre_exec_item_get(ModContext*, void* args, void*, void*) {
    if (randomizer_IsActive()) {
        item_funcs::exec_item_get(arg<u8>(args, 0));
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction pre_check_item_get(ModContext*, void* args, void* retval, void*) {
    if (randomizer_IsActive()) {
        *static_cast<int*>(retval) = item_funcs::check_item_get(arg<u8>(args, 0), arg<int>(args, 1));
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// --- origin/randomizer additions --------------------------------------------------------

// dComIfGp_setNextStage (11-arg overload; the 4-arg one forwards to it): entrance
// randomizer override, rewriting the destination in place.
HookAction pre_set_next_stage(ModContext*, void* args, void*, void*) {
    if (randomizer_IsActive()) {
        randomizer_checkAndOverrideEntranceData(dusk::mods::arg_ref<char const*>(args, 0),
            dusk::mods::arg_ref<s8>(args, 2), dusk::mods::arg_ref<s16>(args, 1),
            dusk::mods::arg_ref<s8>(args, 3));
    }
    return HOOK_CONTINUE;
}

// daObj_Gb_Create (file-static; resolved via the symbol manifest): suppress the added
// Mirror Chamber wall when the seed says it shouldn't exist.
int (*s_orig_obj_gb_create)(fopAc_ac_c*) = nullptr;

int obj_gb_create_trampoline(fopAc_ac_c* actor) {
    if (randomizer_IsActive() && getStageID() == StageIDs::Mirror_Chamber &&
        !randomizer_mirrorChamberWallShouldExist()) {
        return 3; // cPhs_ERROR_e
    }
    return s_orig_obj_gb_create(actor);
}

// dMeter2Info_c::readItemTexture: swap in the shadow-crystal icon for the rando
// MAGIC_LV1 item. The original loads the vanilla texture into i_texBuf1 and points the
// picture at that buffer, so overwriting the buffer contents afterwards is equivalent
// to the branch's in-body swap.
void post_read_item_texture(ModContext*, void* args, void*, void*) {
    if (!randomizer_IsActive()) {
        return;
    }
    const u8 item_no = arg<u8>(args, 1);
    void* tex_buf1 = arg<void*>(args, 2);
    if (tex_buf1 == nullptr || item_no != dItemNo_Randomizer_MAGIC_LV1_e) {
        return;
    }
    const auto bti = shadow_crystal_bti();
    std::memcpy(tex_buf1, bti.data, bti.size < 0xC00 ? bti.size : 0xC00);
}

// --- d_event.cpp ----------------------------------------------------------------------

void post_talk_end(ModContext*, void*, void*, void*) {
    if (randomizer_IsActive() && g_randomizerState.getHasPendingToDChange()) {
        g_randomizerState.setHasPendingToDChange(false);
        g_randomizerState.handleTimeOfDayChange();
    }
}

}  // namespace

ModResult install(const HookService* hooks) {
    using namespace dusk::mods;
    ModResult res = MOD_OK;
    auto check = [&res](ModResult r) {
        if (r != MOD_OK && res == MOD_OK) {
            res = r;
        }
    };

    check(hook_add_pre<&dSv_event_c::isEventBit>(hooks, pre_is_event_bit));
    check(hook_add_pre<&dSv_event_c::onEventBit>(hooks, pre_on_event_bit));
    check(hook_add_pre<&dSv_memBit_c::isSwitch>(hooks, pre_membit_is_switch));
    check(hook_add_pre<&dSv_memBit_c::onSwitch>(hooks, pre_membit_on_switch));
    check(hook_add_pre<&dSv_memBit_c::onDungeonItem>(hooks, pre_on_dungeon_item));
    check(hook_add_pre<&dSv_memBit_c::offDungeonItem>(hooks, pre_off_dungeon_item));
    check(hook_add_pre<&dSv_memBit_c::isDungeonItem>(hooks, pre_is_dungeon_item));
    check(hook_add_pre<&dSv_memBit_c::onStageBossEnemy>(hooks, pre_on_stage_boss_enemy));
    check(hook_add_pre<&dSv_player_status_b_c::isDarkClearLV>(hooks, pre_is_dark_clear_lv));
    check(hook_add_pre<&dSv_player_item_c::checkEmptyBottle>(hooks, pre_check_empty_bottle));
    check(hook_add_post<static_cast<void (dSv_player_item_c::*)()>(&dSv_player_item_c::setLineUpItem)>(
        hooks, post_set_line_up_item));
    check(hook_add_pre<static_cast<void (dSv_info_c::*)(int, int)>(&dSv_info_c::onSwitch)>(
        hooks, pre_info_on_switch));

    check(hook_add_pre<&dMsgFlow_c::query001>(hooks, pre_query001));
    check(hook_add_pre<&dMsgFlow_c::query022>(hooks, pre_query022));
    check(hook_add_pre<&dMsgFlow_c::query025>(hooks, pre_query025));
    check(hook_add_post<&dMsgFlow_c::query049>(hooks, post_query049));
    check(hook_add_pre<&dMsgFlow_c::event035>(hooks, pre_event035));

    check(hook_add_pre<&execItemGet>(hooks, pre_exec_item_get));
    check(hook_add_pre<static_cast<int (*)(u8, int)>(&checkItemGet)>(hooks, pre_check_item_get));

    check(hook_add_post<&dEvt_control_c::talkEnd>(hooks, post_talk_end));

    check(hook_add_pre<static_cast<void (*)(char const*, s16, s8, s8, f32, u32, int, s8, s16, int,
            int)>(&dComIfGp_setNextStage)>(hooks, pre_set_next_stage));
    check(hook_add_post<&dMeter2Info_c::readItemTexture>(hooks, post_read_item_texture));

    void* obj_gb_create_addr = nullptr;
    ModResult resolved = hooks->resolve(::mod_ctx, "daObj_Gb_Create", &obj_gb_create_addr, nullptr);
    if (resolved == MOD_OK) {
        check(hooks->install(::mod_ctx, obj_gb_create_addr,
            reinterpret_cast<void*>(obj_gb_create_trampoline),
            reinterpret_cast<void**>(&s_orig_obj_gb_create)));
    } else {
        check(resolved);
    }

    return res;
}

}  // namespace randomizer::hooks
