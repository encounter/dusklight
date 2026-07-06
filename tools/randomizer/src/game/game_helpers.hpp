#pragma once

#include <cstring>

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_draw.h"

// Mod-local equivalents of small helpers the randomizer branch added to the game
// headers. They only walk exported game state, so they live here instead of in-tree.

// Region flags live in the per-stage memBit block; set one bit by absolute index.
inline void dComIfGs_onRegionFlag(int i_stageNo, int i_no) {
    auto regionFlags = reinterpret_cast<u8*>(&g_dComIfG_gameInfo.info.getSavedata().getSave(i_stageNo).getBit());
    const int offset = i_no / 8;
    const int shift = i_no % 8;
    regionFlags[offset] |= (0x80 >> shift);
}

inline void dComIfGs_setRegionBit(u8 i_region) {
    g_dComIfG_gameInfo.info.getPlayer().getPlayerFieldLastStayInfo().mRegion |= i_region;
}

// Sky-character (Ancient Sky Book letter) counter. The branch relabeled a save-file
// padding byte (unk5 -> mAncientDocumentNum); the port stores it as a SaveService blob
// instead (implemented in seed_session.cpp).
u8 dComIfGs_getAncientDocumentNum();
void dComIfGs_setAncientDocumentNum(u8 i_num);

inline void dComIfGs_setAllLetterRead() {
    g_dComIfG_gameInfo.info.getPlayer().getLetterInfo().mLetterReadFlags[0] |= 0xFFFF;
}

inline s8 dComIfGp_getLayerNo() {
    return g_dComIfG_gameInfo.play.getLayerNo(0);
}

inline void dComIfGp_setEnableNextStage() {
    // dStage_nextStage_c::enabled is private (branch added a setter); dStage_startStage_c
    // is 0x28 bytes and `enabled` is the byte right after it (see offEnable/isEnable).
    auto* nextStage = &g_dComIfG_gameInfo.play.mNextStage;
    reinterpret_cast<u8*>(nextStage)[sizeof(dStage_startStage_c)] = 1;
}

// dMeter2Draw_c::mButtonZAlpha is private (branch added a getter): documented offset 0x720.
inline f32 meter_draw_z_button_alpha(const dMeter2Draw_c* draw) {
    f32 alpha;
    std::memcpy(&alpha, reinterpret_cast<const u8*>(draw) + 0x720, sizeof(alpha));
    return alpha;
}

// Branch: daAlink_c::getEventId() { return mMsgFlow.getEventId(); } (added overload)
inline u16 alink_msg_flow_event_id(daAlink_c* alink) {
    int item_id = 0;
    return alink->mMsgFlow.getEventId(&item_id);
}

// Key count for an arbitrary stage (the header-provided overloads only cover the
// current stage).
inline u8 dComIfGs_getKeyNum(int i_stageNo) {
    if (dComIfGp_getStageStagInfo()) {
        if (i_stageNo == dStage_stagInfo_GetSaveTbl(dComIfGp_getStageStagInfo())) {
            return dComIfGs_getKeyNum();
        }
    }
    return g_dComIfG_gameInfo.info.getSavedata().getSave(i_stageNo).getBit().getKeyNum();
}
