# ItemService checks and grants

ItemService provides named item check resolution, queued inventory grants, and completed-grant observation. Include
`mods/svc/item.h` and import the service once:

```cpp
#include "d/d_item_data.h"
#include "mods/service.hpp"
#include "mods/svc/item.h"

#include <cstring>

IMPORT_SERVICE(ItemService, svc_item);
```

All callbacks run on the game thread. Callback data and strings are host-owned and remain valid only until the callback
returns.

## Resolving checks

A fixed override replaces an item's value at every matching game check:

```cpp
svc_item->set_check_override(mod_ctx, "Ordon Sword", dItemNo_BOMB_BAG_LV1_e);
```

Use a resolver when the result depends on mod state or when one callback manages several checks:

```cpp
bool resolve_item(ModContext* ctx, const ItemCheckInfo* info, uint8_t* outItem,
    void* userData) {
    if (std::strcmp(info->name, "Ordon Sword") != 0) {
        return false;
    }

    *outItem = dItemNo_BOMB_BAG_LV1_e;
    return true;
}

ItemCheckHandle resolver = 0;
svc_item->set_check_resolver(mod_ctx, nullptr, resolve_item, nullptr, &resolver);
```

Pass a check name to `set_check_resolver` to filter in the service, or `NULL` to receive every check. A resolver returns
`true` after writing `out_item` to replace the current value. It returns `false` to leave the value unchanged.

Resolution composes in mod load order. For each mod, its fixed override runs before its resolvers. A later fixed
override replaces the previous result, while each resolver sees the result selected so far in `current_item`.
`vanilla_item` always contains the game's original value.

The game may resolve one check several times for display, actor setup, and the final grant. Resolvers must be
deterministic and must not consume items, advance state, or rely on a specific number of calls. Use a give observer to
record completion.

`resolve_check` applies the same resolution chain without granting an item or notifying observers:

```cpp
uint8_t resolved = dItemNo_NONE_e;
svc_item->resolve_check(mod_ctx, "Ordon Sword", dItemNo_SWORD_e, &resolved);
```

## Queuing grants

`give_item` queues a grant in a global 64-entry FIFO. The service dispatches it when Link is in a safe gameplay state
and no event or message flow is active.

```cpp
svc_item->give_item(mod_ctx, nullptr, dItemNo_BOMB_BAG_LV1_e, 0);

svc_item->give_item(
    mod_ctx, "Ordon Sword", dItemNo_NONE_e, ITEM_GIVE_RESOLVE);

svc_item->give_item(mod_ctx, "Ordon Sword", dItemNo_NONE_e,
    ITEM_GIVE_RESOLVE | ITEM_GIVE_SILENT);
```

Without `ITEM_GIVE_RESOLVE`, `item_no` is granted directly and may not be `dItemNo_NONE_e`. With
`ITEM_GIVE_RESOLVE`, `check_name` is required and `item_no` is the check's vanilla value. It may be
`dItemNo_NONE_e` when the resolver supplies the item. A resolved `dItemNo_NONE_e` completes without changing the
inventory and is still reported to observers.

`ITEM_GIVE_SILENT` applies the inventory change without a get-item demo. Consecutive silent requests dispatch in one
safe frame. Other requests use the standard get-item flow and remain in flight until the grant completes.

Pending requests are removed if their owning mod detaches or the active save slot changes. A get-item event that has
already started cannot be canceled safely and is allowed to finish. The queue returns `MOD_UNAVAILABLE` when full.

## Observing grants

Observers run after the inventory grant and receive both normal game grants and ItemService grants:

```cpp
void on_item_given(ModContext* ctx, const ItemGiveInfo* info, void* userData) {
    if (info->check_name != nullptr) {
        record_completed_check(info->check_name, info->item);
    }
}

ItemGiveHandle observer = 0;
svc_item->observe_gives(mod_ctx, on_item_given, nullptr, &observer);
```

`origin` is one of:

- `ITEM_GIVE_ORIGIN_GAME` for a grant initiated by game code.
- `ITEM_GIVE_ORIGIN_QUEUE` for a queued standard get-item grant.
- `ITEM_GIVE_ORIGIN_QUEUE_SILENT` for a queued silent grant.

`check_name` is `NULL` when the game grant cannot be attributed to a named check. `giver_actor` is a
`fopAc_ac_c*` stored as `const void*`; it is also `NULL` when no actor is available.

Resolver and observer registrations are removed automatically when the calling mod is detached. Store handles only
when manual removal is needed.

## Check names

Check names are case-sensitive and limited to 256 bytes. Most checks that carry stable stage metadata use a derived
name:

| Check | Name format |
| --- | --- |
| Chest | `chest:<stage>:<box number>` |
| Boss reward | `boss:<stage>` |
| Freestanding item | `freestanding:<stage>:<save bit>` |
| Poe soul | `poe:<stage>:<save bit>` |
| Shop item | `shop:<stage>:<vanilla item number>` |
| Golden bug reward | `bug:<insect id>` |
| Sky character reward | `sky:<stage>:<room>` |

Numbers are decimal. `<stage>` is the internal stage name returned by the game, such as `F_SP103`.

Checks without stable generic metadata use these explicit names:

- `Arbiters Grounds Dungeon Reward`
- `Ashei Sketch`
- `Auru Gift To Fyer`
- `Cave of Ordeals Great Fairy Reward`
- `Charlo Donation Blessing`
- `Coro Bottle`
- `Gift From Ralis`
- `Goron Mines Gor Amato Key Shard`
- `Goron Mines Gor Ebizo Key Shard`
- `Goron Mines Gor Liggs Key Shard`
- `Herding Goats Reward`
- `Hyrule Castle King Bulblin Key`
- `Ilia Charm`
- `Iza Helping Hand`
- `Iza Raging Rapids Minigame`
- `Jovani 20 Poe Soul Reward`
- `Jovani 60 Poe Soul Reward`
- `Ordon Cat Rescue`
- `Ordon Shield`
- `Ordon Sword`
- `Plumm Fruit Balloon Minigame`
- `Renados Letter`
- `Rutelas Blessing`
- `Skybook From Impaz`
- `Snowboard Racing Prize`
- `Snowpeak Ruins Ball and Chain`
- `Snowpeak Ruins Mansion Map`
- `STAR Prize 1`
- `STAR Prize 2`
- `Talo Sharpshooting`
- `Telma Invoice`
- `Uli Cradle Delivery`
- `Wooden Statue`
- `Zoras Domain Underwater Goron`

Use `resolve_check` to test a name against the active mod stack without starting its game event.
