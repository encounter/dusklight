# Randomizer mod port — status & gap log

The randomizer was ported from the in-tree `randomizer` branch (86 modified game files)
onto the Dusklight mod API (July 2026). Design/audit record: repo-root `RANDOMIZER.md`.
Branch history is preserved: sources were moved here with pure-rename commits
(`git log --follow` works), and every stripped game-side edit is either re-expressed
below or listed as a gap.

## What is ported (and how)

| Branch mechanism | Port |
|---|---|
| `randomizer_getItemAtLocation` named sites | ItemService catch-all resolver over `mItemLocations` (the in-tree `DUSK_ITEM_CHECK` callouts use the branch's exact location names) + observer for tracker temp flags |
| Chests (`mTreasureChestOverrides`) | resolver for derived names `chest:<stage>:<boxNo>` → branch key `(stageID<<8)\|boxNo` |
| Custom items (portals/keys/maps/compasses/…) | branch item funcs + 256-entry dispatch tables compiled into the mod (`src/game/item_funcs.cpp`); `execItemGet`/`checkItemGet` pre-hooks dispatch while a seed is active; `dItem_data` rows applied by in-place table mutation (`src/game/item_data.cpp`) |
| Flow node patches (`mFlowPatches`) | FlowService `override_flow_node`; branch extension indices (query 53, events 43/44) re-registered per session and remapped in the node bytes |
| Vanilla flow tweaks (query001/022/025/049, event035) | pre/post hooks (`src/hooks.cpp`) |
| Text overrides (`mTextOverrides`) | TextService `override_message_fn` per key of the active language; dynamic messages (poe count, sky letters) formatted at display time (`src/game/messages.cpp`) |
| Object patches/additions (`mObjectPatches`/`mObjectAdditions`) | StageService patch/delete/add_actor (branch CRC mechanism is the service's native keying) |
| `getLayerNo_common_common` ladder (~570 lines) | StageService layer resolver (`src/game/layer_resolver.cpp`, verbatim logic; R_SP107/D_MN08A parity verified) |
| `dComIfGs_setupRandomizerSave` (nameInput2) | SaveService `on_new_save` → `src/game/save_setup.cpp`; pending seed selected via config var `mod.<id>.seed` |
| Seed ↔ save association | per-slot SaveService blob `seed_hash`; `on_save_loaded` auto-activates, title-screen return deactivates (improvement over the branch's global UI selection) |
| `mAncientDocumentNum` padding-byte grab | SaveService blob `sky_characters` (in-memory mirror; helpers keep the branch call names, `src/game/game_helpers.hpp`) |
| d_save/d_com_inf_game mid-function edits (isEventBit/onEventBit/isSwitch/onSwitch/dungeon-item/darkclear/bottle/lineup/pedestal switch) | pre/post hooks; early-return cases skip the original with a written retval |
| `fpcM_Management` state driver | `mod_update` (see “timing” below) |
| Seed generation UI (`rando_seed_generation.cpp`) | worker thread + UiService dialog with `dialog_set_body/set_icon` polling — 1:1 |
| Settings window (`rando_config.cpp`, 1165 lines) | data-driven UiService window: one SELECT per generator setting, help text from setting descriptions |
| ToD-change-after-textbox (`talkEnd`) | post-hook |
| In-tree seams added for the port | `onStageBossEnemy` out-of-line (hookability), NULL guards in 9 `dComIfGs_` stage accessors, `mDoAud_getZelAudio()` |

Branch-added private-field getters that can't exist mod-side are replicated as
documented-offset reads (`Z2SceneMgr::loadedSeWave_1/2` @0x0E/0x10,
`dMeter2Draw_c::mButtonZAlpha` @0x720) — see `src/game/game_helpers.hpp` /
`randomizer_context.cpp`; revisit if those classes change.

## Gaps (not yet ported) — revisit list

Ordered roughly by player impact:

1. **Freestanding items & boss heart containers** (`mFreestandingItemOverrides`, keyed
   `(stageID<<8)|saveBitNo`): the branch's `M_ITEMNO_MODEL_ITEM_ID` macro +
   `daItemBase_c::setRandomizerItem` + `d_a_obj_life_container.cpp` edits (incl. the
   hovering-checks zero-gravity list). Needs an in-tree derived-name funnel
   (`freestanding:<stage>:<flag>`, like chests) or replace hooks. The in-tree
   `boss:<stage>` funnel exists but the seed data doesn't key by it.
2. **Modified-in-place vanilla item funcs**: the extraction pass captured branch-*added*
   functions; functions the branch *edited* (e.g. the sky-letter func at branch
   `d_item.cpp:2235` calling `dComIfGs_setAncientDocumentNum`) still point at vanilla in
   the randomizer dispatch tables. Diff every vanilla `item_func_*` body against the
   branch, copy changed ones into `item_funcs.cpp`, and swap the table entries.
3. **FLW message item gives** (`mFlowItemMessageOverrides` + `mFlowMessageItemId`):
   the branch's `dMsgFlow_c::setNormalMsg` mid-function hijack (swap msg to the item
   text, give on textbox close). "Ilia Memory Reward" class. Needs a Flow/Text seam
   (message-id override at flow time) or a replace hook on `setNormalMsg`.
4. **Poes**: `e_po_dead` mid-function edits (suppress `addPohSpiritNum`, spawn override
   item from `mPoeOverrides`, `handlePoeItem`, and the 60-poe cap = audit soft break #6).
   Replace hook requires checking the static-callee closure of `e_po_dead`; an in-tree
   poe funnel callout is likely cleaner.
5. **Shops** (`mShopOverrides`): `M_SHOP_DATA` macro, const `daShopItem_c::mData[23]`,
   funnels `CheckShopItemCreateHeap` / `seq_start` / `seq_decide_yes`; hook/callout
   territory per the audit.
6. **Bug rewards** (Agitha, `mBugRewardOverrides` + `daNpcIns_c::mGivenInsectId`) and
   **sky characters** (`mSkyCharacterOverrides`) and **golden wolves**
   (`mGoldenWolfOverrides`; `flags.cpp:getCurrentGoldenWolfFlags` is already in the mod):
   game-side dispatch unported (branch NPC-actor edits).
7. **Sacred Grove pedestals + Arbiters (Stallord) reward**: by design replace hooks on
   `daObjMasterSword_c::executeWait/execute` and `daB_DS_c::executeBattle2Dead` plus the
   mod's give queue (`RandomizerState::addItemToEventQueue` is ported and ready);
   `randomizer_checkTempleOfTimeRequirement` is compiled but its caller isn't hooked yet.
8. **Cutscene auto-skip** (`dEvt_control_c::skipper`, SKIP_MAJOR_CUTSCENES): mid-function
   condition; replace hook or a small in-tree seam.
9. **Ring menu D-pad icon** (audit hard break #1, `dMenu_Ring_c::mDpadIcon` +
   `updateSlotImage`): unported; the audit's rewrite is a file-static side table in
   `d_menu_ring.cpp` (in-tree) driven by a mod hook.
10. **`dComIfGs_getCollectSmell`** (audit soft break #4, reekfish scent in Snowpeak):
    unported — the accessor is a header inline (unhookable). Options: write-through
    `setCollectSmell` on Snowpeak entry from `session::update`, or out-of-line the
    accessor in-tree like `onStageBossEnemy`.
11. **alink demo tweaks** (`decideDoStatus`, `checkGroundSpecialMode`, `setGetItemFace`,
    `getSeTypeRandomizer` table in `alink_demo.inc`): accepted-duplication bucket,
    undecided (replace hooks vs tiny seams).
12. **BGM/scene tweaks** (`Z2SceneMgr::setSceneName`, 5 sites): deferred by design —
    vanilla BGM quirks accepted until a scene/BGM resolver seam exists.
13. **`dStage_changeScene4Event` / `dStage_playerInit` branch hunks**: not yet reviewed
    in detail (the actor patch/reload hunks are covered by StageService).
14. **ImGui debug menu + tracker** (`src/imgui/ImGuiMenuRandomizer.*`, archived,
    not built): no ImGui service by design; tracker state (`mShowTracker`,
    `mUpdateTracker`, tools.cpp tracker helpers) compiles but has no surface.
    Re-express on UiService when wanted.
15. **Settings UI curation**: the data-driven settings tab lists everything
    alphabetically; the branch window had curated groups, conditional sub-settings,
    plandomizer/spoiler-log controls, and 12 lines of window.rcss styling
    (`register_styles` when needed).
16. **Headless generator CLI / logic tests** (`RANDOMIZER_ONLY`, `LOGIC_TESTS`,
    `generator/test/test.cpp`): not built; add a standalone executable target in this
    directory if wanted.
17. **ItemService slot claims**: the mod mutates the vanilla `dItem_data` tables and
    dispatch directly (single-mod safe, branch-equivalent). Migrate to
    `claim_item_slot`/override chains when the ItemService append group lands.

## Behavioral deviations (deliberate)

- Seed activation is per save slot (blob) instead of a global UI selection; loading a
  vanilla save deactivates all overrides.
- The branch's `isDarkClearLV`/`dSv_info_c::onSwitch` PC edits ran unconditionally;
  the mod gates every hook on an active seed.
- `mod_update` runs at a different point inside the frame than the branch's
  `fpcM_Management` insertion (before-`fpcEx_Handler` execute / after-`fpcDw_Handler`
  draw). `RandomizerState::draw()` was a no-op, and `execute()` is not visibly
  order-sensitive, but watch the event-item queue timing in gameplay.
- Sky-letter count lives in a mod blob, not the save file; vanilla saves are untouched.

## Layout

- `generator/` — seed generator (yaml-cpp + base64pp statically linked; data
  `b_embed`ded from `generator/data/`, tests excluded). Bundle-resource loading via
  ResourceService is a possible follow-up.
- `src/game/` — ported game runtime (branch `src/dusk/randomizer/game/` + code
  re-homed from stripped game files: `item_funcs`, `item_data`, `item_ids`,
  `layer_resolver`, `save_setup`, `game_helpers`).
- `src/hooks.cpp` — every mid-function branch edit as a pre/post hook.
- `src/seed_session.cpp` — service registration lifecycle + save/config wiring.
- `src/ui/` — UiService menu tab/window + generator config store; `src/imgui/` is
  archival (unbuilt).
