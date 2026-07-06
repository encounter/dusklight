#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/item_checks.hpp"
#include "loader.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_item_data.h"
#include "f_op/f_op_actor_mng.h"

#include <fmt/format.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// The item-give pipeline: grant observation (the item_granted seam under execItemGet) and the
// give queue. Queue mechanics are the documented contract in mods/svc/item.h — FIFO, one demo
// give in flight at a time via the vanilla DEFAULT_GETITEM event (the randomizer branch's
// proven RandomizerState::initGiveItemToPlayer dispatch), silent entries drained together,
// volatile per save slot.

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::item_gives");

// --- give-tag interning ------------------------------------------------------------
// Check names attached to gives are interned to stable u32 ids so the granting actor can carry
// its attribution in a fixed-size field (fopAc_ac_c::mDuskGiveTag). Append-only for the
// session; names registered by since-removed mods stay interned (harmless).

std::vector<std::string> s_giveNames;  // id - 1 -> name
std::unordered_map<std::string, uint32_t> s_giveNameIds;

const char* give_tag_name(uint32_t tag) {
    if (tag == 0 || tag > s_giveNames.size()) {
        return nullptr;
    }
    return s_giveNames[tag - 1].c_str();
}

// --- observers ---------------------------------------------------------------------

struct GiveObserver {
    uint64_t handle = 0;
    ItemGiveObserveFn fn = nullptr;
    void* userData = nullptr;
};

// Game thread only, like the check registry (see item_checks.cpp).
std::unordered_map<LoadedMod*, std::vector<GiveObserver>> s_modObservers;
uint64_t s_nextGiveHandle = 1;
// Fast-path count so per-grant work is one branch while no observers exist.
size_t s_observerCount = 0;

struct PendingObserve {
    LoadedMod* mod = nullptr;
    ItemGiveObserveFn fn = nullptr;
    void* userData = nullptr;
};

void notify_gives(const char* checkName, uint8_t itemNo, fopAc_ac_c* giver, uint8_t origin) {
    if (s_observerCount == 0) {
        return;
    }
    // Snapshot in m_mods order before any callback runs so callbacks can (un)register freely;
    // liveness re-checked per invocation because fail_mod can trip mid-chain.
    std::vector<PendingObserve> observes;
    for (auto& mod : ModLoader::instance().mods()) {
        if (!mod.active) {
            continue;
        }
        const auto recordIt = s_modObservers.find(&mod);
        if (recordIt == s_modObservers.end()) {
            continue;
        }
        for (const auto& observer : recordIt->second) {
            observes.push_back({.mod = &mod, .fn = observer.fn, .userData = observer.userData});
        }
    }

    ItemGiveInfo info{
        .check_name = checkName,
        .giver_actor = giver,
        .item = itemNo,
        .origin = origin,
    };
    for (const auto& observe : observes) {
        if (!observe.mod->active) {
            continue;
        }
        try {
            observe.fn(observe.mod->context.get(), &info, observe.userData);
        } catch (const std::exception& e) {
            fail_mod(*observe.mod, MOD_ERROR,
                fmt::format("exception in item give observer: {}", e.what()));
        } catch (...) {
            fail_mod(*observe.mod, MOD_ERROR, "unknown exception in item give observer");
        }
    }
}

// --- give queue ----------------------------------------------------------------------

constexpr size_t kGiveQueueLimit = 64;

struct QueuedGive {
    LoadedMod* owner = nullptr;  // nullptr = in-tree enqueue (never dropped by mod removal)
    uint32_t tag = 0;            // interned check name, 0 = unattributed
    uint8_t itemNo = 0;          // the vanilla item when resolveAtDispatch
    bool silent = false;
    bool resolveAtDispatch = false;
};

std::deque<QueuedGive> s_queue;

// A dispatched demo give waits here until its grant arrives through item_granted; if the
// get-item event ends without granting (demo actor deleted, event canceled), the next safe
// frame re-dispatches it.
bool s_inFlight = false;
QueuedGive s_inFlightGive{};
uint8_t s_inFlightItem = 0;
// Set once procCoGetItemInit has spawned the give's demo item (give_queue_take_tag), so the
// param0 force happens exactly once per dispatch (branch: the CLEAR_QUEUE handshake).
bool s_inFlightSpawned = false;
// Legitimate re-dispatches (canceled demo, scene change) are rare; a give that repeatedly
// fails to grant indicates a host bug — drop it loudly instead of looping forever.
int s_inFlightRetries = 0;
constexpr int kGiveMaxRetries = 5;

// Set around the direct execItemGet call of a silent dispatch so item_granted can classify it.
bool s_dispatchingSilent = false;

// The safe-moment gate, lifted from the randomizer branch's initGiveItemToPlayer: Link exists
// and is in a plain wait/move proc (human or wolf), no event/cutscene runs, and no message
// flow is active.
bool safe_to_dispatch() {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr) {
        return false;
    }
    switch (link->mProcID) {
    case daAlink_c::PROC_WAIT:
    case daAlink_c::PROC_TIRED_WAIT:
    case daAlink_c::PROC_MOVE:
    case daAlink_c::PROC_WOLF_WAIT:
    case daAlink_c::PROC_WOLF_TIRED_WAIT:
    case daAlink_c::PROC_WOLF_MOVE:
    case daAlink_c::PROC_ATN_MOVE:
    case daAlink_c::PROC_WOLF_ATN_AC_MOVE:
        break;
    default:
        return false;
    }
    if (link->checkEventRun()) {
        return false;
    }
    int itemId = 0;
    if (link->mMsgFlow.getEventId(&itemId) != 0) {
        return false;
    }
    return true;
}

// Resolve a resolve-at-dispatch entry against the current inventory. Returns false when the
// give is suppressed (NONE resolution) — observers still see the suppressed give.
bool resolve_entry(const QueuedGive& give, uint8_t origin, uint8_t& outItem) {
    outItem = give.itemNo;
    if (give.resolveAtDispatch) {
        outItem = item_check(give_tag_name(give.tag), give.itemNo, nullptr);
    }
    if (outItem == dItemNo_NONE_e) {
        notify_gives(give_tag_name(give.tag), dItemNo_NONE_e, nullptr, origin);
        return false;
    }
    return true;
}

void dispatch_silent(const QueuedGive& give) {
    uint8_t item = 0;
    if (!resolve_entry(give, ITEM_GIVE_ORIGIN_QUEUE_SILENT, item)) {
        return;
    }
    Log.debug("silent give dispatched: item {:#x} ('{}')", item,
        give_tag_name(give.tag) != nullptr ? give_tag_name(give.tag) : "");
    s_dispatchingSilent = true;
    execItemGet(item, give.tag, nullptr);
    s_dispatchingSilent = false;
}

void dispatch_demo(uint8_t item) {
    Log.debug("demo give dispatched: item {:#x} ('{}')", item,
        give_tag_name(s_inFlightGive.tag) != nullptr ? give_tag_name(s_inFlightGive.tag) : "");
    daAlink_c* link = daAlink_getAlinkActorClass();
    // The branch's give path: hand the item to the event system and reprioritize the built-in
    // get-item event; the grant comes back through message flow -> execItemGet.
    g_dComIfG_gameInfo.play.getEvent()->setGtItm(item);
    link->mProcID = daAlink_c::PROC_GET_ITEM;
    const s16 eventIdx =
        dComIfGp_getEventManager().getEventIdx((fopAc_ac_c*)link, "DEFAULT_GETITEM", 0xFF);
    fopAcM_orderChangeEventId(link, eventIdx, 1, 0xFFFF);
}

}  // namespace

uint32_t give_tag(const char* name) {
    if (name == nullptr || *name == '\0') {
        return 0;
    }
    if (const auto it = s_giveNameIds.find(name); it != s_giveNameIds.end()) {
        return it->second;
    }
    s_giveNames.emplace_back(name);
    const auto tag = static_cast<uint32_t>(s_giveNames.size());
    s_giveNameIds.emplace(s_giveNames.back(), tag);
    return tag;
}

uint32_t give_tag_chest(uint8_t boxNo) {
    return give_tag(fmt::format("chest:{}:{}", dComIfGp_getStartStageName(), boxNo).c_str());
}

uint32_t give_tag_boss() {
    return give_tag(fmt::format("boss:{}", dComIfGp_getStartStageName()).c_str());
}

void item_check_enqueue(const char* name, uint8_t itemNo, fopAc_ac_c* giver) {
    (void)giver;  // reserved: dispatch happens later, the giver is gone by then
    if (s_queue.size() >= kGiveQueueLimit) {
        Log.warn("give queue full; dropping in-tree give '{}'", name != nullptr ? name : "");
        return;
    }
    s_queue.push_back({
        .owner = nullptr,
        .tag = give_tag(name),
        .itemNo = itemNo,
        .silent = false,
        .resolveAtDispatch = true,
    });
}

void item_granted(uint8_t itemNo, uint32_t giveTag, fopAc_ac_c* giver) {
    uint8_t origin = ITEM_GIVE_ORIGIN_GAME;
    const char* name = give_tag_name(giveTag);
    if (s_dispatchingSilent) {
        origin = ITEM_GIVE_ORIGIN_QUEUE_SILENT;
    } else if (s_inFlight && itemNo == s_inFlightItem && giveTag == s_inFlightGive.tag) {
        // The in-flight queued demo's grant coming back through the demo item it spawned in
        // procCoGetItemInit (the actor carries the give's own tag).
        origin = ITEM_GIVE_ORIGIN_QUEUE;
        s_inFlight = false;
    }
    notify_gives(name, itemNo, giver, origin);
}

void item_gives_tick() {
    if (!s_inFlight && s_queue.empty()) {
        return;
    }
    if (!safe_to_dispatch()) {
        return;
    }
    if (s_inFlight) {
        // Safe again without the grant arriving: the get-item event ended without granting
        // (scene change, canceled demo). Re-dispatch the same entry, bounded.
        if (++s_inFlightRetries > kGiveMaxRetries) {
            Log.error("queued give of item {:#x} ('{}') failed {} times; dropping it",
                s_inFlightItem, give_tag_name(s_inFlightGive.tag) != nullptr
                    ? give_tag_name(s_inFlightGive.tag) : "",
                kGiveMaxRetries);
            s_inFlight = false;
            return;
        }
        Log.info("queued give of item {:#x} did not complete; re-dispatching", s_inFlightItem);
        s_inFlightSpawned = false;
        dispatch_demo(s_inFlightItem);
        return;
    }
    while (!s_queue.empty() && s_queue.front().silent) {
        const QueuedGive give = s_queue.front();
        s_queue.pop_front();
        dispatch_silent(give);
    }
    if (s_queue.empty()) {
        return;
    }
    const QueuedGive give = s_queue.front();
    uint8_t item = 0;
    if (!resolve_entry(give, ITEM_GIVE_ORIGIN_QUEUE, item)) {
        s_queue.pop_front();
        return;
    }
    s_queue.pop_front();
    s_inFlight = true;
    s_inFlightGive = give;
    s_inFlightItem = item;
    s_inFlightSpawned = false;
    s_inFlightRetries = 0;
    dispatch_demo(item);
}

bool give_queue_dispatching() {
    return s_inFlight && !s_inFlightSpawned;
}

uint32_t give_queue_take_tag() {
    if (!s_inFlight || s_inFlightSpawned) {
        return 0;
    }
    s_inFlightSpawned = true;
    return s_inFlightGive.tag;
}

void item_gives_clear() {
    if (!s_queue.empty() || s_inFlight) {
        Log.info("dropping {} queued give(s)", s_queue.size() + (s_inFlight ? 1 : 0));
    }
    s_queue.clear();
    s_inFlight = false;
}

ModResult item_give_enqueue(LoadedMod* mod, const char* checkName, uint8_t itemNo,
    uint32_t flags) {
    if (s_queue.size() >= kGiveQueueLimit) {
        return MOD_UNAVAILABLE;
    }
    s_queue.push_back({
        .owner = mod,
        .tag = give_tag(checkName),
        .itemNo = itemNo,
        .silent = (flags & ITEM_GIVE_SILENT) != 0,
        .resolveAtDispatch = (flags & ITEM_GIVE_RESOLVE) != 0,
    });
    return MOD_OK;
}

ModResult item_give_add_observer(
    LoadedMod& mod, ItemGiveObserveFn fn, void* userData, uint64_t& outHandle) {
    auto& record = s_modObservers[&mod];
    auto& observer = record.emplace_back();
    observer.handle = s_nextGiveHandle++;
    observer.fn = fn;
    observer.userData = userData;
    outHandle = observer.handle;
    ++s_observerCount;
    return MOD_OK;
}

ModResult item_give_remove_observer(LoadedMod& mod, uint64_t handle) {
    const auto recordIt = s_modObservers.find(&mod);
    if (recordIt == s_modObservers.end()) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto removed = std::erase_if(
        recordIt->second, [&](const auto& observer) { return observer.handle == handle; });
    s_observerCount -= removed;
    return removed != 0 ? MOD_OK : MOD_INVALID_ARGUMENT;
}

void item_gives_remove_mod(LoadedMod& mod) {
    if (const auto recordIt = s_modObservers.find(&mod); recordIt != s_modObservers.end()) {
        s_observerCount -= recordIt->second.size();
        s_modObservers.erase(recordIt);
    }
    std::erase_if(s_queue, [&](const QueuedGive& give) { return give.owner == &mod; });
    if (s_inFlight && s_inFlightGive.owner == &mod) {
        // Already handed to the event system; let it grant, but report it unattributed if the
        // tag's owner is gone. (Names stay interned, so attribution is actually still safe —
        // just drop ownership.)
        s_inFlightGive.owner = nullptr;
    }
}

}  // namespace dusk::mods
