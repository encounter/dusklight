#pragma once

#include <cstddef>
#include <cstdint>

// Actor-edit seam entry points for the mod stage service, called from stage placement loading
// (d_stage.cpp). Kept free of mods/svc/stage.h and game headers; the registry lives in
// src/dusk/mods/loader/stage.cpp. Records are raw wire-format placement data (32-byte ACTR /
// 35-byte TGSC), matched by CRC-32 of their bytes.

namespace dusk::mods {

// Apply registered patch/delete edits for the current stage/layer to the record about to
// spawn (actorData = stage_actor_data_class*, actorPrm = fopAcM_prm_class*; both may be
// rewritten in place). roomNo is the spawn's room (-1 = start-stage room). Returns false when
// a delete suppresses the spawn.
bool stage_apply_actor_edits(void* actorData, void* actorPrm, int8_t roomNo);

// Visit registered actor additions for (current stage, roomNo, current layer); visit is
// called once per record with its wire bytes, valid for the duration of the call.
void stage_visit_additions(
    int8_t roomNo, void (*visit)(void* user, const void* record, size_t size), void* user);

}  // namespace dusk::mods
