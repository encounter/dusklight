#include "save_setup.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_meter2_info.h"

#include "dusk/logging.h"

#include "flags.h"
#include "game_helpers.hpp"
#include "item_funcs.hpp"
#include "item_ids.hpp"
#include "randomizer_context.hpp"

namespace randomizer::save_setup {

void setup_new_save() {
    // Setup file based on randomizer data
    auto& randoData = randomizer_GetContext();
    randoData.mCreatingSave = true;

    // Set starting flags
    // Event Flags
    for (const auto& flag : randoData.mStartEventFlags) {
        dComIfGs_onEventBit(flag);
    }
    // Region Flags
    for (const auto& [region, flags] : randoData.mStartRegionFlags) {
        for (const auto& flag : flags) {
            dComIfGs_onRegionFlag(region, flag);
        }
    }

    // Map bits (fills in overworld on map)
    dComIfGs_setRegionBit(randoData.mMapBits);

    // Other flags based on starting flags
    if (dComIfGs_isEventBit(CLEARED_FARON_TWILIGHT)) {
        dComIfGs_onDarkClearLV(0);
        dComIfGs_setLightDropNum(0, 0x10);
        item_funcs::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER_e);
        item_funcs::exec_item_get(dItemNo_Randomizer_WEAR_KOKIRI_e);
    }

    if (dComIfGs_isEventBit(CLEARED_ELDIN_TWILIGHT)) {
        dComIfGs_onDarkClearLV(1);
        dComIfGs_setLightDropNum(1, 0x10);
        item_funcs::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER02_e);
    }

    if (dComIfGs_isEventBit(CLEARED_LANAYRU_TWILIGHT)) {
        dComIfGs_onDarkClearLV(2);
        dComIfGs_setLightDropNum(2, 0x10);
        item_funcs::exec_item_get(dItemNo_Randomizer_DROP_CONTAINER03_e);
    }

    if (randoData.mSettings[RandomizerContext::SKIP_MINOR_CUTSCENES] == RandomizerContext::ON) {
        // Add letter data in this order to more or less reflect an order they can be obtained in game
        static const int letterOrder[] = {3, 2, 4, 7, 5, 6, 13, 12, 10, 9, 8, 15, 0, 14, 11};
        int letterNum = 0;
        for (int i : letterOrder) {
            if (dMenu_Letter::getLetterName(i) != 0) {
                dComIfGs_onLetterGetFlag(i);
                dComIfGs_setGetNumber(letterNum++, i + 1);
            }
        }
        dComIfGs_setAllLetterRead();
    }

    // If MDH and the twilights are pre-completed
    if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED)) {
        // branch: (dComIfGs_getDarkClearLV() & 0x7) == 0x7 (accessor not in this tree)
        if (dComIfGs_isDarkClearLV(0) && dComIfGs_isDarkClearLV(1) && dComIfGs_isDarkClearLV(2)) {
            dComIfGs_onDarkClearLV(3);
            dComIfGs_onTransformLV(3); // Puts Midna on players back
        }
    }

    // Set starting inventory
    for (const auto& itemId : randoData.mStartingInventory) {
        item_funcs::exec_item_get(itemId);
    }

    DuskLog.debug("Created Rando Save");
    randoData.mCreatingSave = false;
}

}  // namespace randomizer::save_setup
