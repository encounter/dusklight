#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_npc_ne.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_info.h"
#include "d/d_msg_object.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_overlap_mng.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);

namespace {

constexpr s16 kActorNpcNe = 269;
constexpr u16 kNotifyMsgId = 0xFFFE;
constexpr int kCatMaxHp = 1;
const char* kDeathMsgText = "It seems Chloe has died...";

fpc_ProcID s_cat_id = fpcM_ERROR_PROCESS_ID_e;
int s_cat_hp = kCatMaxHp;
bool s_cat_dead = false;

bool s_summon_carry = false;

bool s_has_spawn = false;
cXyz s_spawn_pos = {};
s8 s_spawn_room = -1;
char s_spawn_stage[8] = {};

UiElementHandle s_el_hp = 0;
UiElementHandle s_el_hp_bar = 0;
UiElementHandle s_el_status = 0;

ModResult require_ok(const ModResult result, ModError* error, const char* message) {
    if (result != MOD_OK) {
        return dusk::mods::set_error(error, result, message);
    }
    return MOD_OK;
}

void log_info(const char* message) {
    svc_log->info(mod_ctx, message);
}

fopAc_ac_c* get_cat() {
    if (s_cat_id == fpcM_ERROR_PROCESS_ID_e) {
        return nullptr;
    }
    fopAc_ac_c* cat = fopAcM_SearchByID(s_cat_id);
    if (cat == nullptr) {
        s_cat_id = fpcM_ERROR_PROCESS_ID_e;
    }
    return cat;
}

void kill_cat() {
    fopAc_ac_c* cat = get_cat();
    if (cat != nullptr) {
        fopAcM_delete(cat);
        s_cat_id = fpcM_ERROR_PROCESS_ID_e;
    }
    mDoAud_seStartMenu(Z2SE_CAT_CRY_ANNOY);
    s_cat_dead = true;
    dMeter2Info_setFloatingMessage(kNotifyMsgId, 150, false);
    log_info("cat_mod: the cat has died");
}

bool in_spawn_stage() {
    return std::strncmp(dComIfGp_getStartStageName(), s_spawn_stage, sizeof(s_spawn_stage)) == 0;
}

fpc_ProcID create_actor_in_play_scene(s16 procName, u32 params, const cXyz* pos, int roomNo,
    const csXyz* angle, const cXyz* scale, s8 argument) {
    layer_class* savedLayer = fpcLy_CurrentLayer();
    base_process_class* playScene = fpcM_SearchByName(fpcNm_PLAY_SCENE_e);
    if (playScene != nullptr) {
        fpcLy_SetCurrentLayer(&reinterpret_cast<process_node_class*>(playScene)->layer);
    }
    fpc_ProcID result = fopAcM_create(procName, params, pos, roomNo, angle, scale, argument);
    fpcLy_SetCurrentLayer(savedLayer);
    return result;
}

void spawn_cat(bool carry = false) {
    if (s_cat_dead || dComIfGp_event_runCheck()) {
        return;
    }
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr) {
        return;
    }

    cXyz pos;
    s8 roomNo;
    csXyz angle = {};
    if (s_has_spawn && in_spawn_stage()) {
        pos = s_spawn_pos;
        roomNo = s_spawn_room;
    } else {
        f32 yaw = link->shape_angle.y;
        pos = link->current.pos;
        pos.x += cM_ssin(yaw) * 30.0f;
        pos.z += cM_scos(yaw) * 30.0f;
        roomNo = link->current.roomNo;
        angle.y = static_cast<s16>(link->shape_angle.y + static_cast<s16>(0x8000));
    }
    cXyz scale = {1.0f, 1.0f, 1.0f};

    s_cat_id = create_actor_in_play_scene(kActorNpcNe, -1, &pos, roomNo, &angle, &scale, -1);

    if (s_cat_id != fpcM_ERROR_PROCESS_ID_e) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "cat_mod: cat spawned (hp %d/%d)", s_cat_hp, kCatMaxHp);
        log_info(buf);
        s_summon_carry = carry;
    }
}

void on_get_string_post(ModContext*, void* args, void* retval, void*) {
    if (dusk::mods::arg<u32>(args, 0) != kNotifyMsgId) {
        return;
    }

    std::strcpy(dusk::mods::arg<char*>(args, 5), kDeathMsgText);
    std::strcpy(dusk::mods::arg<char*>(args, 7), kDeathMsgText);

    if (retval != nullptr) {
        *static_cast<bool*>(retval) = true;
    }
}

void on_set_damage_point_post(ModContext*, void* args, void*, void*) {
    if (s_cat_dead) {
        return;
    }
    int dmg = dusk::mods::arg<int>(args, 1);
    if (dmg <= 0) {
        return;
    }
    fopAc_ac_c* cat = get_cat();
    bool catFree = cat != nullptr && !fopAcM_checkCarryNow(cat);
    if (catFree) {
        return;
    }
    s_cat_hp -= dmg;

    char buf[96];
    std::snprintf(
        buf, sizeof(buf), "cat_mod: cat took %d damage (hp %d/%d)", dmg, s_cat_hp, kCatMaxHp);
    log_info(buf);

    if (s_cat_hp <= 0) {
        s_cat_hp = 0;
        kill_cat();
    } else {
        mDoAud_seStartMenu(Z2SE_CAT_CRY_CARRY);
    }
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, panel, "Cat");
    svc_ui->pane_add_text(mod_ctx, panel, s_cat_dead ? "Dead" : "Alive", &s_el_status);

    float fraction = static_cast<float>(s_cat_hp) / kCatMaxHp;
    svc_ui->pane_add_progress(mod_ctx, panel, fraction, &s_el_hp_bar);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d / %d HP", s_cat_hp, kCatMaxHp);
    svc_ui->pane_add_text(mod_ctx, panel, buf, &s_el_hp);
    return MOD_OK;
}

ModResult update_panel(ModContext*, void*, ModError*) {
    svc_ui->elem_set_text(mod_ctx, s_el_status, s_cat_dead ? "Dead" : "Alive");

    float fraction = static_cast<float>(s_cat_hp) / kCatMaxHp;
    svc_ui->elem_set_progress(mod_ctx, s_el_hp_bar, fraction);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d / %d HP", s_cat_hp, kCatMaxHp);
    svc_ui->elem_set_text(mod_ctx, s_el_hp, buf);
    return MOD_OK;
}

void consume_input(u32 pad, u32 buttonMask) {
    mDoCPd_c::getCpadInfo(pad).mPressedButtonFlags &= ~buttonMask;
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result =
        dusk::mods::hook_add_post<&dMsgObject_c::getString>(svc_hook, on_get_string_post);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to hook message lookup");
    }

    result =
        dusk::mods::hook_add_post<&daAlink_c::setDamagePoint>(svc_hook, on_set_damage_point_post);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to hook Link damage");
    }

    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    panel.update = update_panel;
    result = svc_ui->register_mods_panel(mod_ctx, &panel);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register UI panel");
    }

    log_info("cat_mod: ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (s_cat_dead) {
        return MOD_OK;
    }

    fopAc_ac_c* cat = get_cat();

    // Load zone detected: dismiss cat into inventory before the area unloads.
    if (cat != nullptr && fopAcM_checkCarryNow(cat) && fopOvlpM_IsDoingReq()) {
        fopAcM_delete(cat);
        s_cat_id = fpcM_ERROR_PROCESS_ID_e;
        s_has_spawn = false;
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link != nullptr) {
            link->procPreActionUnequipInit(0, nullptr);
        }
        return MOD_OK;
    }

    if (cat == nullptr) {
        if (s_has_spawn && in_spawn_stage() && !dComIfGp_event_runCheck()) {
            spawn_cat();
        } else if (mDoCPd_c::getHoldR(PAD_1) && mDoCPd_c::getTrigZ(PAD_1)) {
            consume_input(PAD_1, PAD_TRIGGER_Z);
            spawn_cat(true);
        }
        return MOD_OK;
    }

    if (s_summon_carry) {
        s_summon_carry = false;
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link != nullptr) {
            link->field_0x27f4 = cat;
            link->procGrabReadyInit();
        }
    }

    if (!fopAcM_checkCarryNow(cat)) {
        std::memcpy(s_spawn_stage, dComIfGp_getStartStageName(), sizeof(s_spawn_stage));
        s_spawn_room = cat->current.roomNo;
        s_spawn_pos = cat->current.pos;
        s_has_spawn = true;
    }

    npc_ne_class* ne = static_cast<npc_ne_class*>(cat);
    ne->mBehavior = npc_ne_class::BHV_TAME;
    ne->mNoFollow = 0;
    ne->mTexture = 0;
    ne->mBtkFrame = 0;

    if (mDoCPd_c::getHoldR(PAD_1) && mDoCPd_c::getTrigZ(PAD_1) && fopAcM_checkCarryNow(cat)) {
        consume_input(PAD_1, PAD_TRIGGER_Z);
        fopAcM_delete(cat);
        s_cat_id = fpcM_ERROR_PROCESS_ID_e;
        s_has_spawn = false;
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link != nullptr) {
            link->procPreActionUnequipInit(0, nullptr);
        }
    }

    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    s_cat_id = fpcM_ERROR_PROCESS_ID_e;
    s_cat_hp = kCatMaxHp;
    s_cat_dead = false;
    s_summon_carry = false;
    s_el_hp = 0;
    s_el_hp_bar = 0;
    s_el_status = 0;
    return MOD_OK;
}
}
