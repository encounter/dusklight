#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/text.hpp"
#include "loader.hpp"

#include "JSystem/JMessage/control.h"
#include "JSystem/JMessage/processor.h"
#include "d/d_com_inf_game.h"
#include "d/d_msg_class.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::text");

struct TextOverrideRecord {
    LoadedMod* mod = nullptr;
    uint64_t seq = 0;
    bool isCallback = false;
    std::string text;
    TextMessageFn fn = nullptr;
    void* userData = nullptr;
};

// Game thread only (same rule as flow/config/textures).
std::unordered_map<uint32_t, std::vector<TextOverrideRecord>> s_textOverrides;
uint64_t s_nextSeq = 1;
std::unordered_set<uint32_t> s_warnedKeys;

// Applied text is copied per control so a displayed message never dangles when its override
// (or owning mod) goes away; entries refresh on every message-set for that control.
std::unordered_map<const void*, std::string> s_appliedText;
// Storage for text_lookup (tests/host UI); valid until the next call.
std::string s_lookupText;

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

// Resolve the winning override chain for key into storage; returns false when no active
// override yields text (callbacks may pass with NULL, falling through by priority).
bool resolve_text(uint32_t key, const char* original, std::string& storage) {
    const auto it = s_textOverrides.find(key);
    if (it == s_textOverrides.end()) {
        return false;
    }
    // Order candidates by (priority, seq) descending; the list is tiny in practice.
    std::vector<const TextOverrideRecord*> candidates;
    const LoadedMod* firstOwner = nullptr;
    for (const auto& record : it->second) {
        if (!record.mod->active) {
            continue;
        }
        if (firstOwner == nullptr) {
            firstOwner = record.mod;
        } else if (firstOwner != record.mod && s_warnedKeys.insert(key).second) {
            Log.warn("message {:#x} overridden by multiple mods; the later-loaded one wins", key);
        }
        candidates.push_back(&record);
    }
    std::ranges::sort(candidates, [](const auto* a, const auto* b) {
        const auto pa = compute_mod_priority(*a->mod);
        const auto pb = compute_mod_priority(*b->mod);
        return pa != pb ? pa > pb : a->seq > b->seq;
    });

    for (const auto* record : candidates) {
        if (!record->isCallback) {
            storage = record->text;
            return true;
        }
        // Copy the callback fields: fail_mod (or the callback itself) may mutate the registry.
        const auto local = *record;
        const char* resolved = nullptr;
        try {
            resolved = local.fn(local.mod->context.get(), static_cast<uint16_t>(key >> 16),
                static_cast<uint16_t>(key & 0xFFFF), original, local.userData);
        } catch (const std::exception& e) {
            fail_mod(*local.mod, MOD_ERROR,
                fmt::format("exception in text override for {:#x}: {}", key, e.what()));
            continue;
        } catch (...) {
            fail_mod(*local.mod, MOD_ERROR,
                fmt::format("unknown exception in text override for {:#x}", key));
            continue;
        }
        if (resolved != nullptr) {
            storage = resolved;
            return true;
        }
    }
    return false;
}

}  // namespace

void text_apply_override(void* control, const void* processor, int groupID, int index) {
    if (s_textOverrides.empty() || control == nullptr || processor == nullptr) {
        return;
    }
    auto* tControl = static_cast<JMessage::TControl*>(control);
    const auto* tProcessor = static_cast<const JMessage::TProcessor*>(processor);

    // Derive the stable key from the message entry: ids < 5000 belong to the shared bmg
    // (group 0), anything else to the current stage's message group.
    const auto* entry = static_cast<const JMSMesgEntry_c*>(
        tProcessor->getMessageEntry_messageCode(groupID, index));
    if (entry == nullptr) {
        return;
    }
    const uint16_t messageId = entry->message_id;
    uint16_t group = 0;
    if (messageId >= 5000) {
        const auto* stagInfo = dComIfGp_getStageStagInfo();
        if (stagInfo == nullptr) {
            return;
        }
        group = stagInfo->mMsgGroup;
    }

    std::string resolved;
    if (!resolve_text(static_cast<uint32_t>(group) << 16 | messageId,
            tControl->getMessageText_begin(), resolved))
    {
        return;
    }
    auto& applied = s_appliedText[control];
    applied = std::move(resolved);
    tControl->pMessageText_begin_ = applied.c_str();
}

const char* text_lookup(uint16_t group, uint16_t messageId, const char* original) {
    if (!resolve_text(static_cast<uint32_t>(group) << 16 | messageId, original, s_lookupText)) {
        return nullptr;
    }
    return s_lookupText.c_str();
}

namespace {

ModResult set_override(LoadedMod& mod, uint16_t group, uint16_t messageId,
    TextOverrideRecord&& incoming) {
    incoming.mod = &mod;
    incoming.seq = s_nextSeq++;
    auto& records = s_textOverrides[static_cast<uint32_t>(group) << 16 | messageId];
    for (auto& record : records) {
        if (record.mod == &mod) {
            record = std::move(incoming);
            return MOD_OK;
        }
    }
    records.push_back(std::move(incoming));
    return MOD_OK;
}

}  // namespace

ModResult text_override_message(
    LoadedMod& mod, uint16_t group, uint16_t messageId, const char* text) {
    return set_override(mod, group, messageId, TextOverrideRecord{.text = text});
}

ModResult text_override_message_fn(
    LoadedMod& mod, uint16_t group, uint16_t messageId, TextMessageFn fn, void* userData) {
    return set_override(mod, group, messageId,
        TextOverrideRecord{.isCallback = true, .fn = fn, .userData = userData});
}

ModResult text_clear_override(LoadedMod& mod, uint16_t group, uint16_t messageId) {
    const uint32_t key = static_cast<uint32_t>(group) << 16 | messageId;
    const auto it = s_textOverrides.find(key);
    if (it == s_textOverrides.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto removed =
        std::erase_if(it->second, [&](const auto& record) { return record.mod == &mod; });
    if (it->second.empty()) {
        s_textOverrides.erase(it);
    }
    return removed != 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
}

void text_remove_mod(LoadedMod& mod) {
    for (auto it = s_textOverrides.begin(); it != s_textOverrides.end();) {
        std::erase_if(it->second, [&](const auto& record) { return record.mod == &mod; });
        it = it->second.empty() ? s_textOverrides.erase(it) : std::next(it);
    }
}

}  // namespace dusk::mods
