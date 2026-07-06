#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/stage.hpp"
#include "loader.hpp"

#include "d/d_com_inf_game.h"

#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::stage");

constexpr size_t kActorRecordSize = 32;  // static_asserted against the svc header in stage svc
constexpr size_t kTgscRecordSize = 35;
constexpr uint8_t kRoomStageFile = 0xFF;
constexpr uint8_t kLayerAny = 0xFF;

enum class EditKind : uint8_t { Patch, Delete, Add };

struct ActorEditRecord {
    uint64_t handle = 0;
    LoadedMod* mod = nullptr;
    uint64_t seq = 0;
    uint8_t room = 0;
    uint8_t layer = kLayerAny;
    EditKind kind = EditKind::Patch;
    uint32_t crc = 0;                 // Patch/Delete
    std::vector<uint8_t> record;      // Patch/Add
};

// Game thread only. Keyed by stage name; the per-stage lists stay small.
std::unordered_map<std::string, std::vector<ActorEditRecord>> s_edits;
uint64_t s_nextHandle = 1;
uint64_t s_nextSeq = 1;
std::unordered_set<uint64_t> s_warnedRecords;  // (crc | seq-of-stage-hash) collision warnings

// Position in m_mods (dependency-sorted load order) + 1; later-loaded mods win.
int32_t compute_mod_priority(const LoadedMod& mod) {
    int32_t index = 0;
    for (const auto& other : ModLoader::instance().mods()) {
        ++index;
        if (&other == &mod) {
            return index;
        }
    }
    return index + 1;
}

constexpr std::array<uint32_t, 256> generate_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t ch = i;
        for (size_t j = 0; j < 8; ++j) {
            ch = (ch & 1) != 0 ? 0xEDB88320 ^ ch >> 1 : ch >> 1;
        }
        table[i] = ch;
    }
    return table;
}

constexpr std::array<uint32_t, 256> kCrc32Table = generate_crc32_table();

const std::vector<ActorEditRecord>* current_stage_edits() {
    if (s_edits.empty()) {
        return nullptr;
    }
    const char* stageName = dComIfGp_getStartStageName();
    if (stageName == nullptr) {
        return nullptr;
    }
    const auto it = s_edits.find(stageName);
    return it != s_edits.end() ? &it->second : nullptr;
}

bool room_matches(const ActorEditRecord& record, uint8_t roomNo) {
    return record.room == roomNo || record.room == kRoomStageFile;
}

bool layer_matches(const ActorEditRecord& record, uint8_t layer) {
    return record.layer == layer || record.layer == kLayerAny;
}

}  // namespace

// CRC-32 (IEEE, reflected) shared by the stage-edit matching and the save-sidecar snapshots.
uint32_t mods_crc32(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = ~0u;
    for (size_t i = 0; i < size; ++i) {
        crc = crc >> 8 ^ kCrc32Table[static_cast<uint8_t>(crc ^ bytes[i])];
    }
    return ~crc;
}

bool stage_apply_actor_edits(void* actorData, void* actorPrm, int8_t roomNo) {
    const auto* edits = current_stage_edits();
    if (edits == nullptr) {
        return true;
    }
    // PLYR-style spawns carry room -1; they belong to the start room (rando-branch parity).
    uint8_t room = static_cast<uint8_t>(roomNo);
    if (roomNo == -1) {
        room = static_cast<uint8_t>(dComIfGp_getStartStageRoomNo());
    }
    const auto layer = static_cast<uint8_t>(g_dComIfG_gameInfo.play.getLayerNo(0));
    const auto crcActor = mods_crc32(actorData, kActorRecordSize);
    const auto crcTgsc = mods_crc32(actorData, kTgscRecordSize);

    const ActorEditRecord* winner = nullptr;
    int32_t winnerPriority = 0;
    const LoadedMod* firstOwner = nullptr;
    for (const auto& record : *edits) {
        if (record.kind == EditKind::Add || !record.mod->active || !layer_matches(record, layer) ||
            !room_matches(record, room))
        {
            continue;
        }
        // A patch's record size selects which CRC it targets; deletes match either.
        const bool matches = record.kind == EditKind::Delete
            ? record.crc == crcActor || record.crc == crcTgsc
            : record.crc == (record.record.size() == kTgscRecordSize ? crcTgsc : crcActor);
        if (!matches) {
            continue;
        }
        if (firstOwner == nullptr) {
            firstOwner = record.mod;
        } else if (firstOwner != record.mod && s_warnedRecords.insert(record.crc).second) {
            Log.warn("actor record {:#010x} edited by multiple mods; the later-loaded one wins",
                record.crc);
        }
        const auto priority = compute_mod_priority(*record.mod);
        if (winner == nullptr || priority > winnerPriority ||
            (priority == winnerPriority && record.seq > winner->seq))
        {
            winner = &record;
            winnerPriority = priority;
        }
    }
    if (winner == nullptr) {
        return true;
    }
    if (winner->kind == EditKind::Delete) {
        return false;
    }
    // Rando-branch-proven application: the record overwrites the placement data, and its
    // post-name bytes (params/position/angle[/scale]) overwrite the spawn params.
    std::memcpy(actorData, winner->record.data(), winner->record.size());
    std::memcpy(actorPrm, winner->record.data() + 8, winner->record.size() - 8);
    return true;
}

void stage_visit_additions(
    int8_t roomNo, void (*visit)(void* user, const void* record, size_t size), void* user) {
    const auto* edits = current_stage_edits();
    if (edits == nullptr) {
        return;
    }
    const auto layer = static_cast<uint8_t>(g_dComIfG_gameInfo.play.getLayerNo(0));
    for (const auto& record : *edits) {
        if (record.kind == EditKind::Add && record.mod->active &&
            record.room == static_cast<uint8_t>(roomNo) && layer_matches(record, layer))
        {
            visit(user, record.record.data(), record.record.size());
        }
    }
}

namespace {

ModResult register_edit(LoadedMod& mod, const char* stage, ActorEditRecord&& record,
    uint64_t& outHandle) {
    record.handle = s_nextHandle++;
    record.mod = &mod;
    record.seq = s_nextSeq++;
    outHandle = record.handle;
    s_edits[stage].push_back(std::move(record));
    return MOD_OK;
}

}  // namespace

ModResult stage_patch_actor(LoadedMod& mod, const char* stage, uint8_t room, uint8_t layer,
    uint32_t crc, const void* record, size_t recordSize, uint64_t& outHandle) {
    const auto* bytes = static_cast<const uint8_t*>(record);
    return register_edit(mod, stage,
        ActorEditRecord{.room = room,
            .layer = layer,
            .kind = EditKind::Patch,
            .crc = crc,
            .record = {bytes, bytes + recordSize}},
        outHandle);
}

ModResult stage_delete_actor(LoadedMod& mod, const char* stage, uint8_t room, uint8_t layer,
    uint32_t crc, uint64_t& outHandle) {
    return register_edit(mod, stage,
        ActorEditRecord{.room = room, .layer = layer, .kind = EditKind::Delete, .crc = crc},
        outHandle);
}

ModResult stage_add_actor(LoadedMod& mod, const char* stage, uint8_t room, uint8_t layer,
    const void* record, size_t recordSize, uint64_t& outHandle) {
    const auto* bytes = static_cast<const uint8_t*>(record);
    return register_edit(mod, stage,
        ActorEditRecord{.room = room,
            .layer = layer,
            .kind = EditKind::Add,
            .record = {bytes, bytes + recordSize}},
        outHandle);
}

ModResult stage_remove_actor_edit(LoadedMod& mod, uint64_t handle) {
    for (auto it = s_edits.begin(); it != s_edits.end(); ++it) {
        const auto removed = std::erase_if(it->second, [&](const auto& record) {
            return record.handle == handle && record.mod == &mod;
        });
        if (removed != 0) {
            if (it->second.empty()) {
                s_edits.erase(it);
            }
            return MOD_OK;
        }
    }
    return MOD_INVALID_ARGUMENT;
}

void stage_remove_mod(LoadedMod& mod) {
    for (auto it = s_edits.begin(); it != s_edits.end();) {
        std::erase_if(it->second, [&](const auto& record) { return record.mod == &mod; });
        it = it->second.empty() ? s_edits.erase(it) : std::next(it);
    }
}

}  // namespace dusk::mods
