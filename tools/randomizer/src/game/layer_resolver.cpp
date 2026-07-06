#include "layer_resolver.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"

#include "randomizer_context.hpp"

#include "flags.h"
#include "stages.h"
#include "tools.h"

/*
 * Ported from the randomizer branch's `#if TARGET_PC` block in
 * dComIfG_play_c::getLayerNo_common_common (src/d/d_com_inf_game.cpp on the `randomizer`
 * branch), reshaped onto the StageService 1.1 layer-resolver contract.
 *
 * Control-flow mapping:
 * - On the branch the block ran with `layer == -1` at entry (the darkworld check either set 14
 *   — which the block's own `if (layer < 13)` guard skipped, and in-tree the resolver is never
 *   invoked for layer 14 — or left the -1 initial value). The resolver's current_layer is
 *   likewise always -1 today, so the branch guard is vacuously true here and is not repeated.
 * - The block ended in `} else` before the vanilla `if (layer < 13)` ladder, i.e. with the
 *   randomizer active the vanilla event-bit ladder NEVER ran. This function therefore returns
 *   true unconditionally: when a case leaves `layer` untouched (-1), -1 is reported, which the
 *   resolver contract defines as "keep the previous layer" — the same fallback a vanilla
 *   no-match produces.
 *
 * R_SP107 / D_MN08A parity note: in-tree, the trailing `if (layer == 14)` special-case block
 * in getLayerNo_common_common (meteor-warp downgrade, R_SP107 room 0 -> 11 when sewers not yet
 * visited, D_MN08A room 10 -> 0/1) still runs AFTER a resolver fires. That is exactly what the
 * branch did too: its Hyrule_Castle_Sewers case deliberately sets layer = 14 (sewers not
 * finished) so the vanilla trailing block turns R_SP107 room 0 into layer 11 for a fresh save.
 * Returning 14 here reproduces that flow verbatim (R_SP107 is not in the meteor stage list, so
 * no other part of the trailing block can trigger). D_MN08A has no case in this ladder; on the
 * branch it was only ever handled by the trailing block when the darkworld check produced 14,
 * a path on which the in-tree resolver is never invoked. Parity holds with no divergence.
 */

namespace randomizer::layers {

bool resolve_layer(const char* stage_name, int room_no, int* out_layer) {
    // Block-entry state on the branch: see the control-flow mapping comment above.
    int layer = -1;
    int stageID = getStageID(stage_name);
    bool condition = false;
    bool darkIsClear = false;

    switch (stageID) {
        case Snowpeak_Ruins: {
            if (dComIfGs_isEventBit(SNOWPEAK_RUINS_CLEARED)) {
                layer = 3;
            }
            break;
        }
        case Snowpeak: {
            if (dComIfGs_isEventBit(SNOWPEAK_RUINS_CLEARED) && (room_no != 0)) {
                layer = 3;
            }
            break;
        }
        case Faron_Woods:
        case Faron_Woods_Interiors: {
            if ((room_no == 5) || (room_no == 6)) { // North Faron or Mist Area
                condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo Saved
                if (condition) {
                    layer = 3;
                } else {
                    layer = 1;
                }
            } else {
                condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo Saved
                if (condition) {
                    condition = dComIfGs_isEventBit(FOREST_TEMPLE_CLEARED); // Forest Temple Completed

                    if (condition) {
                        layer = 5;
                    }
                } else {
                    layer = 1;
                }
            }
            break;
        }

        case Kakariko_Village: {
            condition = dComIfGs_isEventBit(WATCHED_CUTSCENE_AFTER_GORON_MINES); // Cutscene after GM Watched
            if (condition == false) {
                condition = dComIfGs_isEventBit(GORON_MINES_CLEARED); // Goron Mines Completed
                if (condition == false) {
                    layer = 2;

                    // If it is night, the layer is different.
                    dComIfG_get_timelayer(&layer);
                } else {
                    layer = 12;
                }
            } else {
                layer = 2;
                dComIfG_get_timelayer(&layer);
            }

            break;
        }
        case Kakariko_Graveyard: {
            condition = dComIfGs_isEventBit(GOT_ZORA_ARMOR_FROM_RUTELA); // Got Zora Armor from Rutela
            if (condition == false) {
                condition = dComIfGs_isEventBit(ZORA_ESCORT_CLEARED); // Zora Escort Cleared

                if (condition == false) {
                    layer = 2;

                    // If it is night, the layer is different.
                    dComIfG_get_timelayer(&layer);
                } else {
                    layer = 4;
                }
            } else {
                layer = 2;
                dComIfG_get_timelayer(&layer);
            }
            break;
        }

        case Kakariko_Graveyard_Interiors: {
            if (((room_no == 1 &&
                    (condition = dComIfGs_isEventBit(LAKEBED_TEMPLE_CLEARED),
                    condition != false)))) // Lakebed Completed
            {
                layer = 4;
                dComIfG_get_timelayer(&layer);
            } else {
                layer = 2;
                dComIfG_get_timelayer(&layer);
            }
            break;
        }

        case Kakariko_Village_Interiors: {
            if (room_no == 1) { // Lakebed Completed
                layer = 4;
                dComIfG_get_timelayer(&layer);
            } else if (room_no == 3) {
                layer = 2;
            } else {
                layer = 2;
                dComIfG_get_timelayer(&layer);
            }
            break;
        }

        case Death_Mountain: {
            condition = dComIfGs_isEventBit(GORON_MINES_CLEARED); // Goron Mines Completed

            if (condition) {
                layer = 2;
            }
            break;
        }

        case Death_Mountain_Interiors: {
            layer = 0;
            break;
        }

        case Lake_Hylia: {
            if (room_no == 1) { // Lanayru Spring

                condition = dComIfGs_isEventBit(LAKEBED_TEMPLE_CLEARED); // Lakebed Temple has been completed
                if (condition) {
                    condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED); // MDH has been started
                    if (condition == false) {
                        layer = 9;
                    } else {
                        layer = 2;
                    }
                }
            } else {
                condition = dComIfGs_isEventBit(SKY_CANNON_REPAIRED); // Sky Cannon Repaired
                if (condition == false) {
                    condition = dComIfGs_isEventBit(WARPED_SKY_CANNON_TO_LAKE_HYLIA); // Sky Cannon Warped to Lake Hylia

                    if (condition == false) {
                        layer = 2;
                    } else {
                        layer = 1;
                    }
                } else {
                    layer = 3;
                }
            }
            break;
        }

        case Castle_Town_Interiors: {
            if (condition = dComIfGs_isEventBit(LAKEBED_TEMPLE_CLEARED), condition) { // Lakebed Temple Completed
                layer = 2;
                if (condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED), condition) { // MDH Completed
                    layer = 0;
                }
            }
            if (room_no == 5) { // Telma's Bar
                layer = 4;
            }
            break;
        }

        case Castle_Town: {
            condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED); // MDH Completed
            if (condition == false) {
                condition = dComIfGs_isEventBit(LAKEBED_TEMPLE_CLEARED); // Lakebed Temple Completed
                if (condition == false) {
                    if ((room_no == 3) &&
                        (condition = dComIfGs_isEventBit(ZORA_ESCORT_CLEARED), condition != false)) { // Zora Escort Cleared
                        layer = 1;
                    } else if (room_no == 4) {
                        layer = 1;
                    }
                } else {
                    layer = 2;
                }
            } else {
                if (((room_no == 4) || (room_no == 3)) || (room_no == 1)) {
                    layer = 1;
                } else {
                    layer = 0;
                }
            }

            if (room_no == 0) {
                if (dComIfGs_getStartPoint() == 0xF) {
                    layer = 5;
                }
            }
            break;
        }

        case Zoras_Domain: {
            layer = 0;
            break;
        }

        case Upper_Zoras_River: {
            condition = dComIfGs_isEventBit(IZA_1_MINIGAME_UNLOCKED); // Iza 1 Unlocked
            if (condition != false) {
                layer = 1;
            }
            break;
        }

        case Gerudo_Desert: {
            layer = 8;

            condition = dComIfGs_isEventBit(VISITED_DESERT_FOR_THE_FIRST_TIME); // Have been to desert
            if (condition != false) {
                layer = 0;
            }
            break;
        }

        case Zoras_River: {
            condition = dComIfGs_isEventBit(IZA_1_MINIGAME_DONE); // Iza 1 Minigame Completed

            if (condition == false) {
                condition = dComIfGs_isEventBit(STARTED_IZA_1_MINIGAME); // Iza 1 Minigame Started
                if (condition != false) {
                    layer = 2;
                }
            } else {
                layer = 1;
            }
            break;
        }

        case Ordon_Village: {
            if (room_no == 0) {
                if (!dKy_daynight_check()) {
                    layer = 0;
                } else {
                    layer = 5;
                }
            } else {
                if (room_no == 1) {
                    condition = dComIfGs_isEventBit(ORDON_DAY_1_FINISHED); // Ordon Day 1 done

                    if (condition) {
                        condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo Saved
                        if (condition) {
                            layer = 2;
                        } else {
                            layer = 4;
                        }
                    } else {
                        layer = 3;
                    }
                }
            }
            break;
        }

        case Ordon_Village_Interiors: {
            /* not used in randomizer anymore. keeping for documentation sake
            if ( room_no == 1 )     // Sera's Shop
            {
                condition = dComIfGs_isEventBit(
                    BOUGHT_SLINGSHOT_FROM_SERA );     // Bought slinghot from Sera

                if ( condition )
                {
                    layer = 2;
                }
            }*/
            if (room_no == 2) { // Jaggle's House

                darkIsClear = dComIfGs_isDarkClearLV(0);
                if (darkIsClear == false) {
                    condition = dComIfGs_isEventBit(FINISHED_SEWERS); // First Trip to Sewers done
                    if (condition != false) {
                        layer = 1;
                    }
                } else {
                    layer = 1;
                }
            }
            /* not used in randomizer anymore. keeping for documentation sake
            else
            {
                if ( room_no == 5 )     // Rusl's House
                {
                    darkIsClear = libtp::tp::d_save::isDarkClearLV( playerStatusBPtr, 0 );
                    if ( darkIsClear != false )
                    {
                        layer = 2;
                    }
                }
            }*/

            break;
        }

        case Ordon_Spring: {
            condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo saved
            if (condition) {
                condition = dComIfGs_isEventBit(FINISHED_SEWERS); // First trip to Sewers done

                if (condition) {
                    darkIsClear = dComIfGs_isDarkClearLV(0);
                    if (darkIsClear != false) {
                        layer = 2;
                    } else {
                        layer = 4;
                    }
                } else {
                    layer = 0;
                }
            } else {
                condition = dComIfGs_isEventBit(TALO_CHASES_MONKEY); // Sword training done on Ordon Day 2
                if (condition) {
                    layer = 3;
                } else {
                    layer = 1;
                }
            }

            break;
        }

        case Ordon_Ranch: {
            condition = dComIfGs_isEventBit(ORDON_DAY_1_FINISHED); // Day 1 done
            if (condition) {
                condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo Saved
                if (condition) {
                    condition = dComIfGs_isEventBit(WATCHED_CUTSCENE_AFTER_GOATS_2); // Saw CS after Goats 2 done

                    if (condition) {
                        layer = 2;
                        dComIfG_get_timelayer(&layer);
                    } else {
                        layer = 9;
                    }
                } else {
                    layer = 2;
                }
            } else {
                layer = 12;
            }
            break;
        }

        case Hyrule_Field: {
            // First 3 twilights are cleared
            // Branch used `(dComIfGs_getDarkClearLV() & 0x7) == 0x7`, an accessor it added to
            // d_com_inf_game.h that does not exist in this tree; testing bits 0-2 individually
            // is exactly equivalent (isDarkClearLV(i) tests bit i of the same flag byte).
            if (dComIfGs_isDarkClearLV(0) && dComIfGs_isDarkClearLV(1) && dComIfGs_isDarkClearLV(2)) {
                if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED)) {
                    layer = 6;
                } else if (dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED)) {
                    layer = 4;
                } else {
                    layer = 0;
                }
            } else {
                layer = 0;
            }
            break;
        }

        case Outside_Castle_Town: {
            if (room_no == 8) {
                condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED); // MDH Completed
                if (condition == false) {
                    condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED); // MDH State Activated
                    if (condition != false) {
                        layer = 4;
                    }
                } else {
                    layer = 6;
                }
            } else {
                if (room_no == 0x10) {
                    condition = dComIfGs_isEventBit(GOT_WOOD_STATUE); // Wooden Statue Gotten
                    if (condition == false) {
                        condition = dComIfGs_isEventBit(TALKED_TO_LOUISE_ABOUT_THE_STOLEN_STATUE); // Talked to Louise after Medicine Scent
                        if (condition == false) {
                            condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED); // MDH Completed
                            if (condition == false) {
                                condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED); // MDH State Activated
                                if (condition != false) {
                                    layer = 4;
                                } else {
                                    layer = 6;
                                }
                            } else {
                                layer = 6;
                            }
                        } else {
                            layer = 1;
                        }
                    } else {
                        layer = 6;
                    }
                } else {
                    if (room_no == 0x11) {
                        condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED); // MDH Completed
                        if (condition == false) {
                            condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED); // MDH State Activated
                            if (condition != false) {
                                layer = 4;
                            }
                        } else {
                            layer = 0;
                        }
                    }
                }
            }
            break;
        }

        case Hidden_Village: {
            condition = dComIfGs_isEventBit(GAVE_ILIA_THE_WOOD_STATUE); // Ilia shown the wooden statue
            if (condition != false) {
                condition = dComIfGs_isEventBit(GOT_ILIAS_CHARM); // Ilia shown Ilia's Charm
                if (condition != false) {
                    layer = 1;
                }
            } else {
                layer = 1;
            }

            break;
        }

        case Castle_Town_Shops: {
            if (room_no == 5) {
                layer = 0;
                condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_STARTED);
                if (condition) {
                    layer = 1;
                    condition = dComIfGs_isEventBit(MIDNAS_DESPERATE_HOUR_COMPLETED);
                    if (condition) {
                        layer = 0;
                    }
                }
            } else {
                condition = dComIfGs_isEventBit(MALO_MART_CASTLE_TOWN_BRANCH_IS_OPEN); // CT Shop is Malo Mart

                if (condition != false) {
                    layer = 1;
                }
            }
            break;
        }

        case Sacred_Grove: {
            layer = 2;
            break;
        }

        case Bulblin_Camp: {
            condition = dComIfGs_isEventBit(ESCAPED_BURNING_TENT_IN_BULBLIN_CAMP); // Escaped Burning Tent in Bulblin Camp
            if (condition) {
                if (room_no == 3) // Other states for this room are very similar, but do not have the boar
                                  // in the dzx.
                { // Setting state 1 solves for any potential softlocks regarding the boar in that area.
                    layer = 1;
                } else {
                    layer = 3;
                }
            }
            break;
        }

        case Faron_Woods_Cave: {
            condition = dComIfGs_isEventBit(ORDON_DAY_2_OVER); // Talo saved
            if (condition != false) {
                layer = 1;
            }
            break;
        }

        case Hyrule_Castle_Sewers: {
            condition = dComIfGs_isEventBit(FINISHED_SEWERS); // Sewers Finished
            if (condition) {
                layer = 13;
            } else {
                // 14 intentionally feeds the trailing `if (layer == 14)` block in
                // getLayerNo_common_common (R_SP107 room 0 -> 11 for a fresh save), exactly
                // as on the branch. See the parity note at the top of this file.
                layer = 14;
            }
            break;
        }

        case Hyrule_Castle: {
            if (((room_no != 0xb) && (room_no != 0xd)) && (room_no != 0xe)) {
                layer = 1;
            }
            break;
        }

        case Fishing_Pond:
        case Fishing_Pond_Interiors: {
            switch (g_env_light.fishing_hole_season) {
                case 1:
                    layer = 0;
                    break;
                case 2:
                    layer = 1;
                    break;
                case 3:
                    layer = 2;
                    break;
                case 4:
                    layer = 3;
                    break;
            }
            break;
        }
        default: {
            break;
        }
    }

    // The branch block always suppressed the vanilla ladder while the randomizer was active
    // (its `} else` joined to the vanilla `if (layer < 13)`), so the resolver always fires;
    // an untouched layer (-1) means "keep previous layer", matching the vanilla no-match
    // fallback in getLayerNo_common.
    *out_layer = layer;
    return true;
}

} // namespace randomizer::layers
