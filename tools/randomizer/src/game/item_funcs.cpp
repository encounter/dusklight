// Randomizer item dispatch, extracted from the randomizer branch's src/d/d_item.cpp.
// The item functions and both 256-entry tables are copied verbatim; entries naming
// vanilla item_func_* / item_getcheck_func_* symbols resolve against the game binary
// via d/d_item.h.

#include "item_funcs.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_item_data.h"

#include "flags.h"
#include "item_ids.hpp"
#include "randomizer_context.hpp"
#include "stages.h"
#include "tools.h"
#include "verify_item_functions.h"

// PORT GAP: the randomizer branch added a stage-scoped dComIfGs_getKeyNum(int i_stageNo)
// overload to src/d/d_com_inf_game.cpp; the game binary doesn't provide it, so it is
// replicated here (body verbatim from the branch) as a file-static overload.
static u8 dComIfGs_getKeyNum(int i_stageNo) {
    // If we're on the current stage for this key, take the current stage info
    if (dComIfGp_getStageStagInfo()) {
        if (i_stageNo == dStage_stagInfo_GetSaveTbl(dComIfGp_getStageStagInfo())) {
            return dComIfGs_getKeyNum();
        }
    }

    // Otherwise take info from the save
    return g_dComIfG_gameInfo.info.getSavedata().getSave(i_stageNo).getBit().getKeyNum();
}

static void item_func_FOOLISH_ITEM() {
    // Failsafe: Make sure the count does not somehow exceed 100
    if (g_randomizerState.mFoolishItemCount < 100)
    {
        g_randomizerState.mFoolishItemCount += 1;
    }
}

static void item_func_ORDON_PORTAL() {
    dComIfGs_onStageSwitch(0x0, 0x34); // Unlock Ordon Portal
}

static void item_func_SOUTH_FARON_PORTAL() {
    dComIfGs_onStageSwitch(0x2, 0x47); // Unlock S Faron Portal
}

static void item_func_UPPER_ZORAS_RIVER_PORTAL() {
    dComIfGs_onStageSwitch(0x4, 0x17); // Talked to Iza before portal
    dComIfGs_onStageSwitch(0x4, 0x37); // Talked to Iza after portal
    dComIfGs_onStageSwitch(0x4, 0x15); // Unlock UZR Portal
    dComIfGs_onEventBit(0xB80); // Declined to help Iza
    dComIfGs_onEventBit(0x1304); // Talked to Iza before UZR portal
    dComIfGs_onEventBit(0xB02); // Iza 1 Minigame Unlocked
}

static void item_func_CASTLE_TOWN_PORTAL() {
    dComIfGs_onStageSwitch(0x6, 0x3); // Unlock Castle Town Portal
}

static void item_func_GERUDO_DESERT_PORTAL() {
    dComIfGs_onStageSwitch(0xA, 0x15); // Unlock Desert Portal
}

static void item_func_NORTH_FARON_PORTAL() {
    dComIfGs_onStageSwitch(0x2, 0x2); // Unlock N Faron Portal
}

static void item_func_KAKARIKO_GORGE_PORTAL() {
    dComIfGs_onStageSwitch(0x6, 0x15); // Unlock Gorge Portal
}

static void item_func_KAKARIKO_VILLAGE_PORTAL() {
    dComIfGs_onStageSwitch(0x3, 0x1F); // Unlock Kak Portal
}

static void item_func_DEATH_MOUNTAIN_PORTAL() {
    dComIfGs_onStageSwitch(0x3, 0x15); // Unlock DM Portal
}

static void item_func_ZORAS_DOMAIN_PORTAL() {
    dComIfGs_onStageSwitch(0x4, 0x2); // Unlock ZD Portal
}

static void item_func_FOREST_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x10);
    dComIfGs_setKeyNum(0x10, currentKeys + 1);
}

static void item_func_MINES_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x11);
    dComIfGs_setKeyNum(0x11, currentKeys + 1);
}

static void item_func_LAKEBED_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x12);
    dComIfGs_setKeyNum(0x12, currentKeys + 1);
}

static void item_func_ARBITERS_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x13);
    dComIfGs_setKeyNum(0x13, currentKeys + 1);
}

static void item_func_SNOWPEAK_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x14);
    dComIfGs_setKeyNum(0x14, currentKeys + 1);
}

static void item_func_TEMPLE_OF_TIME_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x15);
    dComIfGs_setKeyNum(0x15, currentKeys + 1);
}

static void item_func_CITY_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x16);
    dComIfGs_setKeyNum(0x16, currentKeys + 1);
}

static void item_func_PALACE_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x17);
    dComIfGs_setKeyNum(0x17, currentKeys + 1);
}

static void item_func_HYRULE_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0x18);
    dComIfGs_setKeyNum(0x18, currentKeys + 1);
}

static void item_func_CAMP_SMALL_KEY() {
    u8 currentKeys = dComIfGs_getKeyNum(0xA);
    dComIfGs_setKeyNum(0xA, currentKeys + 1);
}

static void item_func_LAKE_HYLIA_PORTAL() {
    dComIfGs_onStageSwitch(0x4, 0xA); // Unlock Lake Portal
}

static void item_func_FOREST_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x10);
}

static void item_func_LAKEBED_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x12);
}

static void item_func_ARBITERS_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x13);
}

static void item_func_TEMPLE_OF_TIME_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x15);
}

static void item_func_CITY_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x16);
}

static void item_func_PALACE_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x17);
}

static void item_func_HYRULE_BOSS_KEY() {
    dComIfGs_onDungeonItemBossKey(0x18);
}

static void item_func_FOREST_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x10);
}

static void item_func_MINES_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x11);
}

static void item_func_LAKEBED_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x12);
}

static void item_func_ARBITERS_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x13);
}

static void item_func_SNOWPEAK_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x14);
}

static void item_func_TEMPLE_OF_TIME_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x15);
}

static void item_func_CITY_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x16);
}

static void item_func_PALACE_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x17);
}

static void item_func_HYRULE_COMPASS() {
    dComIfGs_onDungeonItemCompass(0x18);
}

static void item_func_MIRROR_CHAMBER_PORTAL() {
    dComIfGs_onStageSwitch(0xA, 0x28); // Unlock MC Portal
}

static void item_func_SNOWPEAK_PORTAL() {
    dComIfGs_onStageSwitch(0x8, 0x15); // Unlock Snowpeak Portal
}

static void item_func_FOREST_MAP() {
    dComIfGs_onDungeonItemMap(0x10);
}

static void item_func_MINES_MAP() {
    dComIfGs_onDungeonItemMap(0x11);
}

static void item_func_LAKEBED_MAP() {
    dComIfGs_onDungeonItemMap(0x12);
}

static void item_func_ARBITERS_MAP() {
    dComIfGs_onDungeonItemMap(0x13);
}

static void item_func_SNOWPEAK_MAP() {
    dComIfGs_onDungeonItemMap(0x14);
}

static void item_func_TEMPLE_OF_TIME_MAP() {
    dComIfGs_onDungeonItemMap(0x15);
}

static void item_func_CITY_MAP() {
    dComIfGs_onDungeonItemMap(0x16);
}

static void item_func_PALACE_MAP() {
    dComIfGs_onDungeonItemMap(0x17);
}

static void item_func_HYRULE_MAP() {
    dComIfGs_onDungeonItemMap(0x18);
}

static void item_func_SACRED_GROVE_PORTAL() {
    dComIfGs_onStageSwitch(0x7, 0x64); // Unlock Grove Portal
}

static void item_func_FUSED_SHADOW_1() {
    dComIfGs_onCollectCrystal(0);
    /*
    Adding rando code until framework is implemented
    // Check if the requirement for the HC barrier is set to shadows, and if so, set the flag
    rando::gRandomizer->checkSetHCBarrierFlag(rando::HC_Fused_Shadows, 1);

    // Check if the requirement for the HC BK is set to shadows, and if so, set the flag
    rando::gRandomizer->checkSetHCBkFlag(rando::HC_BK_Fused_Shadows, 1);
    */
}

static void item_func_FUSED_SHADOW_2() {
    if (randomizer_IsActive()) {
        dComIfGs_onCollectCrystal(1);
        /*
        Adding rando code until framework is implemented
        // Check if the requirement for the HC barrier is set to shadows, and if so, set the flag
        rando::gRandomizer->checkSetHCBarrierFlag(rando::HC_Fused_Shadows, 2);

        // Check if the requirement for the HC BK is set to shadows, and if so, set the flag
        rando::gRandomizer->checkSetHCBkFlag(rando::HC_BK_Fused_Shadows, 2);
        */
    }
}

static void item_func_FUSED_SHADOW_3() {
    if (randomizer_IsActive()) {
        dComIfGs_onCollectCrystal(2);
        /*
        Adding rando code until framework is implemented
        // If the player has the palace requirement set to Fused Shadows.
        if (headerPtr->getPalaceRequirements() == rando::PalaceEntryRequirements::PoT_Fused_Shadows)
        {
            events::setSaveFileEventFlag(libtp::data::flags::FIXED_THE_MIRROR_OF_TWILIGHT);
        }

        // Check if the requirement for the HC barrier is set to shadows, and if so, set the flag
        rando::gRandomizer->checkSetHCBarrierFlag(rando::HC_Fused_Shadows, 3);

        // Check if the requirement for the HC BK is set to shadows, and if so, set the flag
        rando::gRandomizer->checkSetHCBkFlag(rando::HC_BK_Fused_Shadows, 3);
        */
    }
}

static void item_func_MIRROR_PIECE_1() {
    if (randomizer_IsActive()) {
        dComIfGs_onCollectMirror(0);
    }
}

static void item_func_ENDING_BLOW() {
    dComIfGs_onEventBit(0x2904);
}

static void item_func_SHIELD_ATTACK() {
    dComIfGs_onEventBit(0x2908);
}

static void item_func_BACK_SLICE() {
    dComIfGs_onEventBit(0x2902);
}

static void item_func_HELM_SPLITTER() {
    dComIfGs_onEventBit(0x2901);
}

static void item_func_MORTAL_DRAW() {
    dComIfGs_onEventBit(0x2A80);
}

static void item_func_JUMP_STRIKE() {
    dComIfGs_onEventBit(0x2A40);
}

static void item_func_GREAT_SPIN() {
    dComIfGs_onEventBit(0x2A20);
}

static void item_func_ELDIN_BRIDGE_PORTAL() {
    dComIfGs_onStageSwitch(0x6, 0x63); // Unlock Eldin Bridge Portal
}

static int item_getcheck_func_ORDON_PORTAL() {
    return dComIfGs_isStageSwitch(0x0, 0x34); // Unlock Ordon Portal
}

static int item_getcheck_func_SOUTH_FARON_PORTAL() {
    return dComIfGs_isStageSwitch(0x2, 0x47); // Unlock S Faron Portal
}

static int item_getcheck_func_UPPER_ZORAS_RIVER_PORTAL() {
    return dComIfGs_isStageSwitch(0x4, 0x15); // Unlock UZR Portal
}

static int item_getcheck_func_CASTLE_TOWN_PORTAL() {
    return dComIfGs_isStageSwitch(0x6, 0x3); // Unlock Castle Town Portal
}

static int item_getcheck_func_GERUDO_DESERT_PORTAL() {
    return dComIfGs_isStageSwitch(0xA, 0x15); // Unlock Desert Portal
}

static int item_getcheck_func_NORTH_FARON_PORTAL() {
    return dComIfGs_isStageSwitch(0x2, 0x2); // Unlock N Faron Portal
}

static int item_getcheck_func_KAKARIKO_GORGE_PORTAL() {
    return dComIfGs_isStageSwitch(0x6, 0x15); // Unlock Gorge Portal
}

static int item_getcheck_func_KAKARIKO_VILLAGE_PORTAL() {
    return dComIfGs_isStageSwitch(0x3, 0x1F); // Unlock Kak Portal
}

static int item_getcheck_func_DEATH_MOUNTAIN_PORTAL() {
    return dComIfGs_isStageSwitch(0x3, 0x15); // Unlock DM Portal
}

static int item_getcheck_func_ZORAS_DOMAIN_PORTAL() {
    return dComIfGs_isStageSwitch(0x4, 0x2); // Unlock ZD Portal
}

static int item_getcheck_func_CAMP_SMALL_KEY() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_CAMP_SMALL_KEY_e);
}

static int item_getcheck_func_LAKE_HYLIA_PORTAL() {
    return dComIfGs_isStageSwitch(0x4, 0xA); // Unlock Lake Portal
}

static int item_getcheck_func_MIRROR_CHAMBER_PORTAL() {
    return dComIfGs_isStageSwitch(0xA, 0x28); // Unlock MC Portal
}

static int item_getcheck_func_SNOWPEAK_PORTAL() {
    return dComIfGs_isStageSwitch(0x8, 0x15); // Unlock Snowpeak Portal
}

static int item_getcheck_func_SACRED_GROVE_PORTAL() {
    return dComIfGs_isStageSwitch(0x7, 0x64); // Unlock Grove Portal
}

static int item_getcheck_func_FUSED_SHADOW_1() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_FUSED_SHADOW_1_e);
}

static int item_getcheck_func_FUSED_SHADOW_2() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_FUSED_SHADOW_2_e);
}

static int item_getcheck_func_FUSED_SHADOW_3() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_FUSED_SHADOW_3_e);
}

static int item_getcheck_func_MIRROR_PIECE_1() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_MIRROR_PIECE_1_e);
}

static int item_getcheck_func_ENDING_BLOW() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_ENDING_BLOW_e);
}

static int item_getcheck_func_SHIELD_ATTACK() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_SHIELD_ATTACK_e);
}

static int item_getcheck_func_BACK_SLICE() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_BACK_SLICE_e);
}

static int item_getcheck_func_HELM_SPLITTER() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_HELM_SPLITTER_e);
}

static int item_getcheck_func_MORTAL_DRAW() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_MORTAL_DRAW_e);
}

static int item_getcheck_func_JUMP_STRIKE() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_JUMP_STRIKE_e);
}

static int item_getcheck_func_GREAT_SPIN() {
    return dComIfGs_isItemFirstBit(dItemNo_Randomizer_GREAT_SPIN_e);
}

static int item_getcheck_func_ELDIN_BRIDGE_PORTAL() {
    return dComIfGs_isStageSwitch(0x6, 0x63); // Unlock Eldin Bridge Portal
}

static BOOL isRupee(u8 i_itemNo) {
    switch (i_itemNo) {
    case dItemNo_GREEN_RUPEE_e:
    case dItemNo_BLUE_RUPEE_e:
    case dItemNo_YELLOW_RUPEE_e:
    case dItemNo_RED_RUPEE_e:
    case dItemNo_PURPLE_RUPEE_e:
    case dItemNo_ORANGE_RUPEE_e:
    case dItemNo_SILVER_RUPEE_e:
        return true;
    default:
        break;
    }

    return false;
}

static void (*item_func_ptr_randomizer[256])() = {
    /* 0x00 */ item_func_HEART,
    /* 0x01 */ item_func_GREEN_RUPEE,
    /* 0x02 */ item_func_BLUE_RUPEE,
    /* 0x03 */ item_func_YELLOW_RUPEE,
    /* 0x04 */ item_func_RED_RUPEE,
    /* 0x05 */ item_func_PURPLE_RUPEE,
    /* 0x06 */ item_func_ORANGE_RUPEE,
    /* 0x07 */ item_func_SILVER_RUPEE,
    /* 0x08 */ item_func_S_MAGIC,
    /* 0x09 */ item_func_L_MAGIC,
    /* 0x0A */ item_func_BOMB_5,
    /* 0x0B */ item_func_BOMB_10,
    /* 0x0C */ item_func_BOMB_20,
    /* 0x0D */ item_func_BOMB_30,
    /* 0x0E */ item_func_ARROW_10,
    /* 0x0F */ item_func_ARROW_20,
    /* 0x10 */ item_func_ARROW_30,
    /* 0x11 */ item_func_ARROW_1,
    /* 0x12 */ item_func_PACHINKO_SHOT,
    /* 0x13 */ item_func_FOOLISH_ITEM,
    /* 0x14 */ item_func_ORDON_PORTAL,
    /* 0x15 */ item_func_SOUTH_FARON_PORTAL,
    /* 0x16 */ item_func_WATER_BOMB_5,
    /* 0x17 */ item_func_WATER_BOMB_10,
    /* 0x18 */ item_func_WATER_BOMB_20,
    /* 0x19 */ item_func_WATER_BOMB_30,
    /* 0x1A */ item_func_BOMB_INSECT_5,
    /* 0x1B */ item_func_BOMB_INSECT_10,
    /* 0x1C */ item_func_BOMB_INSECT_20,
    /* 0x1D */ item_func_BOMB_INSECT_30,
    /* 0x1E */ item_func_RECOVER_FAILY,
    /* 0x1F */ item_func_TRIPLE_HEART,
    /* 0x20 */ item_func_SMALL_KEY,
    /* 0x21 */ item_func_KAKERA_HEART,
    /* 0x22 */ item_func_UTUWA_HEART,
    /* 0x23 */ item_func_MAP,
    /* 0x24 */ item_func_COMPUS,
    /* 0x25 */ item_func_DUNGEON_EXIT,
    /* 0x26 */ item_func_BOSS_KEY,
    /* 0x27 */ item_func_DUNGEON_BACK,
    /* 0x28 */ item_func_SWORD,
    /* 0x29 */ item_func_MASTER_SWORD,
    /* 0x2A */ item_func_WOOD_SHIELD,
    /* 0x2B */ item_func_SHIELD,
    /* 0x2C */ item_func_HYLIA_SHIELD,
    /* 0x2D */ item_func_TKS_LETTER,
    /* 0x2E */ item_func_WEAR_CASUAL,
    /* 0x2F */ item_func_WEAR_KOKIRI,
    /* 0x30 */ item_func_ARMOR,
    /* 0x31 */ item_func_WEAR_ZORA,
    /* 0x32 */ item_func_MAGIC_LV1,
    /* 0x33 */ item_func_DUNGEON_EXIT_2,
    /* 0x34 */ item_func_WALLET_LV1,
    /* 0x35 */ item_func_WALLET_LV2,
    /* 0x36 */ item_func_WALLET_LV3,
    /* 0x37 */ item_func_noentry,
    /* 0x38 */ item_func_noentry,
    /* 0x39 */ item_func_UPPER_ZORAS_RIVER_PORTAL,
    /* 0x3A */ item_func_CASTLE_TOWN_PORTAL,
    /* 0x3B */ item_func_GERUDO_DESERT_PORTAL,
    /* 0x3C */ item_func_NORTH_FARON_PORTAL,
    /* 0x3D */ item_func_ZORAS_JEWEL,
    /* 0x3E */ item_func_HAWK_EYE,
    /* 0x3F */ item_func_WOOD_STICK,
    /* 0x40 */ item_func_BOOMERANG,
    /* 0x41 */ item_func_SPINNER,
    /* 0x42 */ item_func_IRONBALL,
    /* 0x43 */ item_func_BOW,
    /* 0x44 */ item_func_HOOKSHOT,
    /* 0x45 */ item_func_HVY_BOOTS,
    /* 0x46 */ item_func_COPY_ROD,
    /* 0x47 */ item_func_W_HOOKSHOT,
    /* 0x48 */ item_func_KANTERA,
    /* 0x49 */ item_func_LIGHT_SWORD,
    /* 0x4A */ item_func_FISHING_ROD_1,
    /* 0x4B */ item_func_PACHINKO,
    /* 0x4C */ item_func_COPY_ROD_2,
    /* 0x4D */ item_func_KAKARIKO_GORGE_PORTAL,
    /* 0x4E */ item_func_KAKARIKO_VILLAGE_PORTAL,
    /* 0x4F */ item_func_BOMB_BAG_LV2,
    /* 0x50 */ item_func_BOMB_BAG_LV1,
    /* 0x51 */ item_func_BOMB_IN_BAG,
    /* 0x52 */ item_func_DEATH_MOUNTAIN_PORTAL,
    /* 0x53 */ item_func_LIGHT_ARROW,
    /* 0x54 */ item_func_ARROW_LV1,
    /* 0x55 */ item_func_ARROW_LV2,
    /* 0x56 */ item_func_ARROW_LV3,
    /* 0x57 */ item_func_ZORAS_DOMAIN_PORTAL,
    /* 0x58 */ item_func_LURE_ROD,
    /* 0x59 */ item_func_BOMB_ARROW,
    /* 0x5A */ item_func_HAWK_ARROW,
    /* 0x5B */ item_func_BEE_ROD,
    /* 0x5C */ item_func_JEWEL_ROD,
    /* 0x5D */ item_func_WORM_ROD,
    /* 0x5E */ item_func_JEWEL_BEE_ROD,
    /* 0x5F */ item_func_JEWEL_WORM_ROD,
    /* 0x60 */ item_func_EMPTY_BOTTLE,
    /* 0x61 */ item_func_RED_BOTTLE,
    /* 0x62 */ item_func_GREEN_BOTTLE,
    /* 0x63 */ item_func_BLUE_BOTTLE,
    /* 0x64 */ item_func_MILK_BOTTLE,
    /* 0x65 */ item_func_HALF_MILK_BOTTLE,
    /* 0x66 */ item_func_OIL_BOTTLE,
    /* 0x67 */ item_func_WATER_BOTTLE,
    /* 0x68 */ item_func_OIL_BOTTLE2,
    /* 0x69 */ item_func_RED_BOTTLE2,
    /* 0x6A */ item_func_UGLY_SOUP,
    /* 0x6B */ item_func_HOT_SPRING,
    /* 0x6C */ item_func_FAIRY_BOTTLE,
    /* 0x6D */ item_func_HOT_SPRING2,
    /* 0x6E */ item_func_OIL2,
    /* 0x6F */ item_func_OIL,
    /* 0x70 */ item_func_NORMAL_BOMB,
    /* 0x71 */ item_func_WATER_BOMB,
    /* 0x72 */ item_func_POKE_BOMB,
    /* 0x73 */ item_func_FAIRY_DROP,
    /* 0x74 */ item_func_WORM,
    /* 0x75 */ item_func_DROP_BOTTLE,
    /* 0x76 */ item_func_BEE_CHILD,
    /* 0x77 */ item_func_CHUCHU_RARE,
    /* 0x78 */ item_func_CHUCHU_RED,
    /* 0x79 */ item_func_CHUCHU_BLUE,
    /* 0x7A */ item_func_CHUCHU_GREEN,
    /* 0x7B */ item_func_CHUCHU_YELLOW,
    /* 0x7C */ item_func_CHUCHU_PURPLE,
    /* 0x7D */ item_func_LV1_SOUP,
    /* 0x7E */ item_func_LV2_SOUP,
    /* 0x7F */ item_func_LV3_SOUP,
    /* 0x80 */ item_func_LETTER,
    /* 0x81 */ item_func_BILL,
    /* 0x82 */ item_func_WOOD_STATUE,
    /* 0x83 */ item_func_IRIAS_PENDANT,
    /* 0x84 */ item_func_HORSE_FLUTE,
    /* 0x85 */ item_func_FOREST_SMALL_KEY,
    /* 0x86 */ item_func_MINES_SMALL_KEY,
    /* 0x87 */ item_func_LAKEBED_SMALL_KEY,
    /* 0x88 */ item_func_ARBITERS_SMALL_KEY,
    /* 0x89 */ item_func_SNOWPEAK_SMALL_KEY,
    /* 0x8A */ item_func_TEMPLE_OF_TIME_SMALL_KEY,
    /* 0x8B */ item_func_CITY_SMALL_KEY,
    /* 0x8C */ item_func_PALACE_SMALL_KEY,
    /* 0x8D */ item_func_HYRULE_SMALL_KEY,
    /* 0x8E */ item_func_CAMP_SMALL_KEY,
    /* 0x8F */ item_func_LAKE_HYLIA_PORTAL,
    /* 0x90 */ item_func_RAFRELS_MEMO,
    /* 0x91 */ item_func_ASHS_SCRIBBLING,
    /* 0x92 */ item_func_FOREST_BOSS_KEY,
    /* 0x93 */ item_func_LAKEBED_BOSS_KEY,
    /* 0x94 */ item_func_ARBITERS_BOSS_KEY,
    /* 0x95 */ item_func_TEMPLE_OF_TIME_BOSS_KEY,
    /* 0x96 */ item_func_CITY_BOSS_KEY,
    /* 0x97 */ item_func_PALACE_BOSS_KEY,
    /* 0x98 */ item_func_HYRULE_BOSS_KEY,
    /* 0x99 */ item_func_FOREST_COMPASS,
    /* 0x9A */ item_func_MINES_COMPASS,
    /* 0x9B */ item_func_LAKEBED_COMPASS,
    /* 0x9C */ item_func_CHUCHU_YELLOW2,
    /* 0x9D */ item_func_OIL_BOTTLE3,
    /* 0x9E */ item_func_SHOP_BEE_CHILD,
    /* 0x9F */ item_func_CHUCHU_BLACK,
    /* 0xA0 */ item_func_LIGHT_DROP,
    /* 0xA1 */ item_func_DROP_CONTAINER,
    /* 0xA2 */ item_func_DROP_CONTAINER02,
    /* 0xA3 */ item_func_DROP_CONTAINER03,
    /* 0xA4 */ item_func_FILLED_CONTAINER,
    /* 0xA5 */ item_func_MIRROR_PIECE_2,
    /* 0xA6 */ item_func_MIRROR_PIECE_3,
    /* 0xA7 */ item_func_MIRROR_PIECE_4,
    /* 0xA8 */ item_func_ARBITERS_COMPASS,
    /* 0xA9 */ item_func_SNOWPEAK_COMPASS,
    /* 0xAA */ item_func_TEMPLE_OF_TIME_COMPASS,
    /* 0xAB */ item_func_CITY_COMPASS,
    /* 0xAC */ item_func_PALACE_COMPASS,
    /* 0xAD */ item_func_HYRULE_COMPASS,
    /* 0xAE */ item_func_MIRROR_CHAMBER_PORTAL,
    /* 0xAF */ item_func_SNOWPEAK_PORTAL,
    /* 0xB0 */ item_func_SMELL_YELIA_POUCH,
    /* 0xB1 */ item_func_SMELL_PUMPKIN,
    /* 0xB2 */ item_func_SMELL_POH,
    /* 0xB3 */ item_func_SMELL_FISH,
    /* 0xB4 */ item_func_SMELL_CHILDREN,
    /* 0xB5 */ item_func_SMELL_MEDICINE,
    /* 0xB6 */ item_func_FOREST_MAP,
    /* 0xB7 */ item_func_MINES_MAP,
    /* 0xB8 */ item_func_LAKEBED_MAP,
    /* 0xB9 */ item_func_ARBITERS_MAP,
    /* 0xBA */ item_func_SNOWPEAK_MAP,
    /* 0xBB */ item_func_TEMPLE_OF_TIME_MAP,
    /* 0xBC */ item_func_CITY_MAP,
    /* 0xBD */ item_func_PALACE_MAP,
    /* 0xBE */ item_func_HYRULE_MAP,
    /* 0xBF */ item_func_SACRED_GROVE_PORTAL,
    /* 0xC0 */ item_func_M_BEETLE,
    /* 0xC1 */ item_func_F_BEETLE,
    /* 0xC2 */ item_func_M_BUTTERFLY,
    /* 0xC3 */ item_func_F_BUTTERFLY,
    /* 0xC4 */ item_func_M_STAG_BEETLE,
    /* 0xC5 */ item_func_F_STAG_BEETLE,
    /* 0xC6 */ item_func_M_GRASSHOPPER,
    /* 0xC7 */ item_func_F_GRASSHOPPER,
    /* 0xC8 */ item_func_M_NANAFUSHI,
    /* 0xC9 */ item_func_F_NANAFUSHI,
    /* 0xCA */ item_func_M_DANGOMUSHI,
    /* 0xCB */ item_func_F_DANGOMUSHI,
    /* 0xCC */ item_func_M_MANTIS,
    /* 0xCD */ item_func_F_MANTIS,
    /* 0xCE */ item_func_M_LADYBUG,
    /* 0xCF */ item_func_F_LADYBUG,
    /* 0xD0 */ item_func_M_SNAIL,
    /* 0xD1 */ item_func_F_SNAIL,
    /* 0xD2 */ item_func_M_DRAGONFLY,
    /* 0xD3 */ item_func_F_DRAGONFLY,
    /* 0xD4 */ item_func_M_ANT,
    /* 0xD5 */ item_func_F_ANT,
    /* 0xD6 */ item_func_M_MAYFLY,
    /* 0xD7 */ item_func_F_MAYFLY,
    /* 0xD8 */ item_func_FUSED_SHADOW_1,
    /* 0xD9 */ item_func_FUSED_SHADOW_2,
    /* 0xDA */ item_func_FUSED_SHADOW_3,
    /* 0xDB */ item_func_MIRROR_PIECE_1,
    /* 0xDC */ item_func_noentry,
    /* 0xDD */ item_func_noentry,
    /* 0xDE */ item_func_noentry,
    /* 0xDF */ item_func_noentry,
    /* 0xE0 */ item_func_POU_SPIRIT,
    /* 0xE1 */ item_func_ENDING_BLOW,
    /* 0xE2 */ item_func_SHIELD_ATTACK,
    /* 0xE3 */ item_func_BACK_SLICE,
    /* 0xE4 */ item_func_HELM_SPLITTER,
    /* 0xE5 */ item_func_MORTAL_DRAW,
    /* 0xE6 */ item_func_JUMP_STRIKE,
    /* 0xE7 */ item_func_GREAT_SPIN,
    /* 0xE8 */ item_func_ELDIN_BRIDGE_PORTAL,
    /* 0xE9 */ item_func_ANCIENT_DOCUMENT,
    /* 0xEA */ item_func_AIR_LETTER,
    /* 0xEB */ item_func_ANCIENT_DOCUMENT2,
    /* 0xEC */ item_func_LV7_DUNGEON_EXIT,
    /* 0xED */ item_func_LINKS_SAVINGS,
    /* 0xEE */ item_func_SMALL_KEY2,
    /* 0xEF */ item_func_POU_FIRE1,
    /* 0xF0 */ item_func_POU_FIRE2,
    /* 0xF1 */ item_func_POU_FIRE3,
    /* 0xF2 */ item_func_POU_FIRE4,
    /* 0xF3 */ item_func_BOSSRIDER_KEY,
    /* 0xF4 */ item_func_TOMATO_PUREE,
    /* 0xF5 */ item_func_TASTE,
    /* 0xF6 */ item_func_LV5_BOSS_KEY,
    /* 0xF7 */ item_func_SURFBOARD,
    /* 0xF8 */ item_func_KANTERA2,
    /* 0xF9 */ item_func_L2_KEY_PIECES1,
    /* 0xFA */ item_func_L2_KEY_PIECES2,
    /* 0xFB */ item_func_L2_KEY_PIECES3,
    /* 0xFC */ item_func_KEY_OF_CARAVAN,
    /* 0xFD */ item_func_LV2_BOSS_KEY,
    /* 0xFE */ item_func_KEY_OF_FILONE,
    /* 0xFF */ item_func_noentry,
};

static int (*item_getcheck_func_ptr_randomizer[256])() = {
    /* 0x00 */ item_getcheck_func_HEART,
    /* 0x01 */ item_getcheck_func_GREEN_RUPEE,
    /* 0x02 */ item_getcheck_func_BLUE_RUPEE,
    /* 0x03 */ item_getcheck_func_YELLOW_RUPEE,
    /* 0x04 */ item_getcheck_func_RED_RUPEE,
    /* 0x05 */ item_getcheck_func_PURPLE_RUPEE,
    /* 0x06 */ item_getcheck_func_ORANGE_RUPEE,
    /* 0x07 */ item_getcheck_func_SILVER_RUPEE,
    /* 0x08 */ item_getcheck_func_S_MAGIC,
    /* 0x09 */ item_getcheck_func_L_MAGIC,
    /* 0x0A */ item_getcheck_func_BOMB_5,
    /* 0x0B */ item_getcheck_func_BOMB_10,
    /* 0x0C */ item_getcheck_func_BOMB_20,
    /* 0x0D */ item_getcheck_func_BOMB_30,
    /* 0x0E */ item_getcheck_func_ARROW_10,
    /* 0x0F */ item_getcheck_func_ARROW_20,
    /* 0x10 */ item_getcheck_func_ARROW_30,
    /* 0x11 */ item_getcheck_func_ARROW_1,
    /* 0x12 */ item_getcheck_func_PACHINKO_SHOT,
    /* 0x13 */ item_getcheck_func_noentry,
    /* 0x14 */ item_getcheck_func_ORDON_PORTAL,
    /* 0x15 */ item_getcheck_func_SOUTH_FARON_PORTAL,
    /* 0x16 */ item_getcheck_func_WATER_BOMB_5,
    /* 0x17 */ item_getcheck_func_WATER_BOMB_10,
    /* 0x18 */ item_getcheck_func_WATER_BOMB_20,
    /* 0x19 */ item_getcheck_func_WATER_BOMB_30,
    /* 0x1A */ item_getcheck_func_BOMB_INSECT_5,
    /* 0x1B */ item_getcheck_func_BOMB_INSECT_10,
    /* 0x1C */ item_getcheck_func_BOMB_INSECT_20,
    /* 0x1D */ item_getcheck_func_BOMB_INSECT_30,
    /* 0x1E */ item_getcheck_func_RECOVER_FAILY,
    /* 0x1F */ item_getcheck_func_TRIPLE_HEART,
    /* 0x20 */ item_getcheck_func_SMALL_KEY,
    /* 0x21 */ item_getcheck_func_KAKERA_HEART,
    /* 0x22 */ item_getcheck_func_UTUWA_HEART,
    /* 0x23 */ item_getcheck_func_MAP,
    /* 0x24 */ item_getcheck_func_COMPUS,
    /* 0x25 */ item_getcheck_func_DUNGEON_EXIT,
    /* 0x26 */ item_getcheck_func_BOSS_KEY,
    /* 0x27 */ item_getcheck_func_DUNGEON_BACK,
    /* 0x28 */ item_getcheck_func_SWORD,
    /* 0x29 */ item_getcheck_func_MASTER_SWORD,
    /* 0x2A */ item_getcheck_func_WOOD_SHIELD,
    /* 0x2B */ item_getcheck_func_SHIELD,
    /* 0x2C */ item_getcheck_func_HYLIA_SHIELD,
    /* 0x2D */ item_getcheck_func_TKS_LETTER,
    /* 0x2E */ item_getcheck_func_WEAR_CASUAL,
    /* 0x2F */ item_getcheck_func_WEAR_KOKIRI,
    /* 0x30 */ item_getcheck_func_ARMOR,
    /* 0x31 */ item_getcheck_func_WEAR_ZORA,
    /* 0x32 */ item_getcheck_func_MAGIC_LV1,
    /* 0x33 */ item_getcheck_func_DUNGEON_EXIT_2,
    /* 0x34 */ item_getcheck_func_WALLET_LV1,
    /* 0x35 */ item_getcheck_func_WALLET_LV2,
    /* 0x36 */ item_getcheck_func_WALLET_LV3,
    /* 0x37 */ item_getcheck_func_noentry,
    /* 0x38 */ item_getcheck_func_noentry,
    /* 0x39 */ item_getcheck_func_UPPER_ZORAS_RIVER_PORTAL,
    /* 0x3A */ item_getcheck_func_CASTLE_TOWN_PORTAL,
    /* 0x3B */ item_getcheck_func_GERUDO_DESERT_PORTAL,
    /* 0x3C */ item_getcheck_func_NORTH_FARON_PORTAL,
    /* 0x3D */ item_getcheck_func_ZORAS_JEWEL,
    /* 0x3E */ item_getcheck_func_HAWK_EYE,
    /* 0x3F */ item_getcheck_func_WOOD_STICK,
    /* 0x40 */ item_getcheck_func_BOOMERANG,
    /* 0x41 */ item_getcheck_func_SPINNER,
    /* 0x42 */ item_getcheck_func_IRONBALL,
    /* 0x43 */ item_getcheck_func_BOW,
    /* 0x44 */ item_getcheck_func_HOOKSHOT,
    /* 0x45 */ item_getcheck_func_HVY_BOOTS,
    /* 0x46 */ item_getcheck_func_COPY_ROD,
    /* 0x47 */ item_getcheck_func_W_HOOKSHOT,
    /* 0x48 */ item_getcheck_func_KANTERA,
    /* 0x49 */ item_getcheck_func_LIGHT_SWORD,
    /* 0x4A */ item_getcheck_func_FISHING_ROD_1,
    /* 0x4B */ item_getcheck_func_PACHINKO,
    /* 0x4C */ item_getcheck_func_COPY_ROD_2,
    /* 0x4D */ item_getcheck_func_KAKARIKO_GORGE_PORTAL,
    /* 0x4E */ item_getcheck_func_KAKARIKO_VILLAGE_PORTAL,
    /* 0x4F */ item_getcheck_func_BOMB_BAG_LV2,
    /* 0x50 */ item_getcheck_func_BOMB_BAG_LV1,
    /* 0x51 */ item_getcheck_func_BOMB_IN_BAG,
    /* 0x52 */ item_getcheck_func_DEATH_MOUNTAIN_PORTAL,
    /* 0x53 */ item_getcheck_func_LIGHT_ARROW,
    /* 0x54 */ item_getcheck_func_ARROW_LV1,
    /* 0x55 */ item_getcheck_func_ARROW_LV2,
    /* 0x56 */ item_getcheck_func_ARROW_LV3,
    /* 0x57 */ item_getcheck_func_ZORAS_DOMAIN_PORTAL,
    /* 0x58 */ item_getcheck_func_LURE_ROD,
    /* 0x59 */ item_getcheck_func_BOMB_ARROW,
    /* 0x5A */ item_getcheck_func_HAWK_ARROW,
    /* 0x5B */ item_getcheck_func_BEE_ROD,
    /* 0x5C */ item_getcheck_func_JEWEL_ROD,
    /* 0x5D */ item_getcheck_func_WORM_ROD,
    /* 0x5E */ item_getcheck_func_JEWEL_BEE_ROD,
    /* 0x5F */ item_getcheck_func_JEWEL_WORM_ROD,
    /* 0x60 */ item_getcheck_func_EMPTY_BOTTLE,
    /* 0x61 */ item_getcheck_func_RED_BOTTLE,
    /* 0x62 */ item_getcheck_func_GREEN_BOTTLE,
    /* 0x63 */ item_getcheck_func_BLUE_BOTTLE,
    /* 0x64 */ item_getcheck_func_MILK_BOTTLE,
    /* 0x65 */ item_getcheck_func_HALF_MILK_BOTTLE,
    /* 0x66 */ item_getcheck_func_OIL_BOTTLE,
    /* 0x67 */ item_getcheck_func_WATER_BOTTLE,
    /* 0x68 */ item_getcheck_func_OIL_BOTTLE2,
    /* 0x69 */ item_getcheck_func_RED_BOTTLE2,
    /* 0x6A */ item_getcheck_func_UGLY_SOUP,
    /* 0x6B */ item_getcheck_func_HOT_SPRING,
    /* 0x6C */ item_getcheck_func_FAIRY_BOTTLE,
    /* 0x6D */ item_getcheck_func_HOT_SPRING2,
    /* 0x6E */ item_getcheck_func_OIL2,
    /* 0x6F */ item_getcheck_func_OIL,
    /* 0x70 */ item_getcheck_func_NORMAL_BOMB,
    /* 0x71 */ item_getcheck_func_WATER_BOMB,
    /* 0x72 */ item_getcheck_func_POKE_BOMB,
    /* 0x73 */ item_getcheck_func_FAIRY_DROP,
    /* 0x74 */ item_getcheck_func_WORM,
    /* 0x75 */ item_getcheck_func_DROP_BOTTLE,
    /* 0x76 */ item_getcheck_func_BEE_CHILD,
    /* 0x77 */ item_getcheck_func_CHUCHU_RARE,
    /* 0x78 */ item_getcheck_func_CHUCHU_RED,
    /* 0x79 */ item_getcheck_func_CHUCHU_BLUE,
    /* 0x7A */ item_getcheck_func_CHUCHU_GREEN,
    /* 0x7B */ item_getcheck_func_CHUCHU_YELLOW,
    /* 0x7C */ item_getcheck_func_CHUCHU_PURPLE,
    /* 0x7D */ item_getcheck_func_LV1_SOUP,
    /* 0x7E */ item_getcheck_func_LV2_SOUP,
    /* 0x7F */ item_getcheck_func_LV3_SOUP,
    /* 0x80 */ item_getcheck_func_LETTER,
    /* 0x81 */ item_getcheck_func_BILL,
    /* 0x82 */ item_getcheck_func_WOOD_STATUE,
    /* 0x83 */ item_getcheck_func_IRIAS_PENDANT,
    /* 0x84 */ item_getcheck_func_HORSE_FLUTE,
    /* 0x85 */ item_getcheck_func_noentry,
    /* 0x86 */ item_getcheck_func_noentry,
    /* 0x87 */ item_getcheck_func_noentry,
    /* 0x88 */ item_getcheck_func_noentry,
    /* 0x89 */ item_getcheck_func_noentry,
    /* 0x8A */ item_getcheck_func_noentry,
    /* 0x8B */ item_getcheck_func_noentry,
    /* 0x8C */ item_getcheck_func_noentry,
    /* 0x8D */ item_getcheck_func_noentry,
    /* 0x8E */ item_getcheck_func_CAMP_SMALL_KEY,
    /* 0x8F */ item_getcheck_func_LAKE_HYLIA_PORTAL,
    /* 0x90 */ item_getcheck_func_RAFRELS_MEMO,
    /* 0x91 */ item_getcheck_func_ASHS_SCRIBBLING,
    /* 0x92 */ item_getcheck_func_noentry,
    /* 0x93 */ item_getcheck_func_noentry,
    /* 0x94 */ item_getcheck_func_noentry,
    /* 0x95 */ item_getcheck_func_noentry,
    /* 0x96 */ item_getcheck_func_noentry,
    /* 0x97 */ item_getcheck_func_noentry,
    /* 0x98 */ item_getcheck_func_noentry,
    /* 0x99 */ item_getcheck_func_noentry,
    /* 0x9A */ item_getcheck_func_noentry,
    /* 0x9B */ item_getcheck_func_noentry,
    /* 0x9C */ item_getcheck_func_CHUCHU_YELLOW2,
    /* 0x9D */ item_getcheck_func_OIL_BOTTLE3,
    /* 0x9E */ item_getcheck_func_SHOP_BEE_CHILD,
    /* 0x9F */ item_getcheck_func_CHUCHU_BLACK,
    /* 0xA0 */ item_getcheck_func_LIGHT_DROP,
    /* 0xA1 */ item_getcheck_func_DROP_CONTAINER,
    /* 0xA2 */ item_getcheck_func_DROP_CONTAINER02,
    /* 0xA3 */ item_getcheck_func_DROP_CONTAINER03,
    /* 0xA4 */ item_getcheck_func_FILLED_CONTAINER,
    /* 0xA5 */ item_getcheck_func_MIRROR_PIECE_2,
    /* 0xA6 */ item_getcheck_func_MIRROR_PIECE_3,
    /* 0xA7 */ item_getcheck_func_MIRROR_PIECE_4,
    /* 0xA8 */ item_getcheck_func_noentry,
    /* 0xA9 */ item_getcheck_func_noentry,
    /* 0xAA */ item_getcheck_func_noentry,
    /* 0xAB */ item_getcheck_func_noentry,
    /* 0xAC */ item_getcheck_func_noentry,
    /* 0xAD */ item_getcheck_func_noentry,
    /* 0xAE */ item_getcheck_func_MIRROR_CHAMBER_PORTAL,
    /* 0xAF */ item_getcheck_func_SNOWPEAK_PORTAL,
    /* 0xB0 */ item_getcheck_func_SMELL_YELIA_POUCH,
    /* 0xB1 */ item_getcheck_func_SMELL_PUMPKIN,
    /* 0xB2 */ item_getcheck_func_SMELL_POH,
    /* 0xB3 */ item_getcheck_func_SMELL_FISH,
    /* 0xB4 */ item_getcheck_func_SMELL_CHILDREN,
    /* 0xB5 */ item_getcheck_func_SMELL_MEDICINE,
    /* 0xB6 */ item_getcheck_func_noentry,
    /* 0xB7 */ item_getcheck_func_noentry,
    /* 0xB8 */ item_getcheck_func_noentry,
    /* 0xB9 */ item_getcheck_func_noentry,
    /* 0xBA */ item_getcheck_func_noentry,
    /* 0xBB */ item_getcheck_func_noentry,
    /* 0xBC */ item_getcheck_func_noentry,
    /* 0xBD */ item_getcheck_func_noentry,
    /* 0xBE */ item_getcheck_func_noentry,
    /* 0xBF */ item_getcheck_func_SACRED_GROVE_PORTAL,
    /* 0xC0 */ item_getcheck_func_M_BEETLE,
    /* 0xC1 */ item_getcheck_func_F_BEETLE,
    /* 0xC2 */ item_getcheck_func_M_BUTTERFLY,
    /* 0xC3 */ item_getcheck_func_F_BUTTERFLY,
    /* 0xC4 */ item_getcheck_func_M_STAG_BEETLE,
    /* 0xC5 */ item_getcheck_func_F_STAG_BEETLE,
    /* 0xC6 */ item_getcheck_func_M_GRASSHOPPER,
    /* 0xC7 */ item_getcheck_func_F_GRASSHOPPER,
    /* 0xC8 */ item_getcheck_func_M_NANAFUSHI,
    /* 0xC9 */ item_getcheck_func_F_NANAFUSHI,
    /* 0xCA */ item_getcheck_func_M_DANGOMUSHI,
    /* 0xCB */ item_getcheck_func_F_DANGOMUSHI,
    /* 0xCC */ item_getcheck_func_M_MANTIS,
    /* 0xCD */ item_getcheck_func_F_MANTIS,
    /* 0xCE */ item_getcheck_func_M_LADYBUG,
    /* 0xCF */ item_getcheck_func_F_LADYBUG,
    /* 0xD0 */ item_getcheck_func_M_SNAIL,
    /* 0xD1 */ item_getcheck_func_F_SNAIL,
    /* 0xD2 */ item_getcheck_func_M_DRAGONFLY,
    /* 0xD3 */ item_getcheck_func_F_DRAGONFLY,
    /* 0xD4 */ item_getcheck_func_M_ANT,
    /* 0xD5 */ item_getcheck_func_F_ANT,
    /* 0xD6 */ item_getcheck_func_M_MAYFLY,
    /* 0xD7 */ item_getcheck_func_F_MAYFLY,
    /* 0xD8 */ item_getcheck_func_FUSED_SHADOW_1,
    /* 0xD9 */ item_getcheck_func_FUSED_SHADOW_2,
    /* 0xDA */ item_getcheck_func_FUSED_SHADOW_3,
    /* 0xDB */ item_getcheck_func_MIRROR_PIECE_1,
    /* 0xDC */ item_getcheck_func_noentry,
    /* 0xDD */ item_getcheck_func_noentry,
    /* 0xDE */ item_getcheck_func_noentry,
    /* 0xDF */ item_getcheck_func_noentry,
    /* 0xE0 */ item_getcheck_func_POU_SPIRIT,
    /* 0xE1 */ item_getcheck_func_ENDING_BLOW,
    /* 0xE2 */ item_getcheck_func_SHIELD_ATTACK,
    /* 0xE3 */ item_getcheck_func_BACK_SLICE,
    /* 0xE4 */ item_getcheck_func_HELM_SPLITTER,
    /* 0xE5 */ item_getcheck_func_MORTAL_DRAW,
    /* 0xE6 */ item_getcheck_func_JUMP_STRIKE,
    /* 0xE7 */ item_getcheck_func_GREAT_SPIN,
    /* 0xE8 */ item_getcheck_func_ELDIN_BRIDGE_PORTAL,
    /* 0xE9 */ item_getcheck_func_ANCIENT_DOCUMENT,
    /* 0xEA */ item_getcheck_func_AIR_LETTER,
    /* 0xEB */ item_getcheck_func_ANCIENT_DOCUMENT2,
    /* 0xEC */ item_getcheck_func_LV7_DUNGEON_EXIT,
    /* 0xED */ item_getcheck_func_LINKS_SAVINGS,
    /* 0xEE */ item_getcheck_func_SMALL_KEY2,
    /* 0xEF */ item_getcheck_func_POU_FIRE1,
    /* 0xF0 */ item_getcheck_func_POU_FIRE2,
    /* 0xF1 */ item_getcheck_func_POU_FIRE3,
    /* 0xF2 */ item_getcheck_func_POU_FIRE4,
    /* 0xF3 */ item_getcheck_func_BOSSRIDER_KEY,
    /* 0xF4 */ item_getcheck_func_TOMATO_PUREE,
    /* 0xF5 */ item_getcheck_func_TASTE,
    /* 0xF6 */ item_getcheck_func_LV5_BOSS_KEY,
    /* 0xF7 */ item_getcheck_func_SURFBOARD,
    /* 0xF8 */ item_getcheck_func_KANTERA2,
    /* 0xF9 */ item_getcheck_func_L2_KEY_PIECES1,
    /* 0xFA */ item_getcheck_func_L2_KEY_PIECES2,
    /* 0xFB */ item_getcheck_func_L2_KEY_PIECES3,
    /* 0xFC */ item_getcheck_func_KEY_OF_CARAVAN,
    /* 0xFD */ item_getcheck_func_LV2_BOSS_KEY,
    /* 0xFE */ item_getcheck_func_KEY_OF_FILONE,
    /* 0xFF */ item_getcheck_func_noentry,
};


namespace randomizer::item_funcs {

// Mirrors the randomizer branch's execItemGet + getItemFunc while a seed is
// active: resolve progressive items, flag the tracker for an update, then run
// the vanilla first-bit bookkeeping and the randomizer dispatch table.
void exec_item_get(u8 item_no) {
    item_no = verifyProgressiveItem(item_no);
    g_randomizerState.mUpdateTracker = true;
    dComIfGs_onItemFirstBit(item_no);
    item_func_ptr_randomizer[item_no]();
}

// Mirrors the randomizer branch's checkItemGet + getCheckItemFunc while a seed
// is active: special-case a handful of shop/boss checks, then dispatch through
// the randomizer getcheck table, falling back to `param` on -1.
int check_item_get(u8 item_no, int param) {
    // Check special randomizer cases
    switch (item_no) {
    case dItemNo_Randomizer_HYLIA_SHIELD_e:
        // Check if we are at Kakariko Malo mart and verify that we have not bought the shield.
        if (playerIsInRoomStage(3, allStages[Kakariko_Village_Interiors]) &&
            !dComIfGs_isEventBit(BOUGHT_HYLIAN_SHIELD_AT_MALO_MART)) {
                // Return false so we can buy the shield.
                return 0;
        }
        break;
    case dItemNo_Randomizer_HAWK_EYE_e:
        // Check if we are at Kakariko Village and that the hawkeye is currently not for sale.
        if (getStageID() == Kakariko_Village && !dComIfGs_isSwitch(0x3E, 0)) {
            // Return false so we can buy the hawkeye.
            return 0;
        }
        break;
    case dItemNo_Randomizer_SHIELD_e:
    case dItemNo_Randomizer_WOOD_SHIELD_e:
        // Check if we are at Kakariko Malo mart and that the Wooden Shield has not been bought.
        if (playerIsInRoomStage(3, allStages[Kakariko_Village_Interiors]) &&
            !dComIfGs_isSwitch(0x5, 3)) {
                // Return false so we can buy the shield.
                return 0;
            }
        break;
    case dItemNo_Randomizer_TOMATO_PUREE_e:
    case dItemNo_Randomizer_TASTE_e:
        // Check to see if currently in Snowpeak Ruins
        if (getStageID() == Snowpeak_Ruins) {
            // Return false so that yeta will give the map item no matter what.
            return 0;
        }
        break;
    case dItemNo_Randomizer_IRONBALL_e:
        // Check to see if currently in Snowpeak Ruins Darkhammer room
        if (getStageID() == Darkhammer) {
            return dComIfGs_isSwitch(0x5F, 51); // Picked up the Ball and Chain check.
        }
    }

    int result = item_getcheck_func_ptr_randomizer[item_no]();

    if (result == -1) {
        result = param;
    }

    return result;
}

} // namespace randomizer::item_funcs
