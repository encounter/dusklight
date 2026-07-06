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
13. **`dStage_changeScene4Event` / `dStage_playerInit` branch hunks**: reviewed July 6.
    The actor patch/reload hunks are covered by StageService. Two real remainders:
    (a) `dStage_playerInit` rewrites player-spawn entrance-type bytes (wolf form can't
    use door entrances 0x80/0xA0/0xB0; Lake Hylia 0xD0 pre-meteor has no water) →
    StageService spawn-record rewriter, see plan below; (b) `dStage_changeScene4Event`
    overrides `timeH` with `mStartHour` on the first spawn (F_SP103 room 1 start 1) —
    needs no seam: mod post-hook re-calling `dKy_set_nexttime`.
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

## origin/randomizer catch-up (July 2026)

The port was first cut against the local `randomizer` branch (June 8); the sources were
then caught up to `origin/randomizer` (June 26, 61 rando commits) with a second
pure-rename centralize + merge. `d_item.cpp`/`d_item_data.cpp`/`d_save.cpp`/`d_stage.cpp`
were unchanged upstream, so the extracted item funcs/tables and save hooks stayed
current. Newly ported from the delta:

- **Entrance randomizer**: `randomizer_checkAndOverrideEntranceData` wired as a pre-hook
  on the 11-arg `dComIfGp_setNextStage` (rewrites stage/room/point/layer in place).
- **Mirror Chamber wall** (`daObj_Gb_Create`): raw-install hook via symbol resolve,
  suppressing the spawn per `randomizer_mirrorChamberWallShouldExist`.
- **Shadow-crystal item icon**: post-hook on `dMeter2Info_c::readItemTexture` overwrites
  the texture buffer with the mod-embedded `assets/textures/shadow_crystal.bti`.
- `setupRandomizerSave` state reset; the d_msg_flow refactor upstream was already
  covered by the FlowService seam (all three dispatch sites).

Additional gaps from the delta:

18. **File-select "Play Type" flow** (the Vanilla/Randomizer dialog +
    `FileSelectRandomizerWindow` on new save): origin adds a new file-select proc
    (`selectDataPlayTypeMove`), a `mDusk` state member on `dFile_select_c`, and
    menu-pointer integration — a structural game change (member add = ABI break) that
    can't be hook-ported. Needs an in-tree seam design: either a generic "new-save
    interstitial" extension point (host runs a mod-supplied UiService dialog/window
    between file selection and name entry) or landing the origin proc in-tree behind
    a mod-facing callout. Until then, the pending seed is chosen via the Randomizer
    menu tab / `mod.dev_twilitrealm_randomizer.seed` config var.
19. **File-select slot info ("Randomizer" + seed hash per file)**: origin stores per-file
    seed hashes in host settings; the port keeps them in SaveService blobs, which only
    expose the *current* slot — displaying other slots' seeds at file select needs a
    SaveService peek API (or the host reading the sidecar) plus a d_file_sel_info seam.
20. **Presets / permalink / seed-menu restyle UI**: generator support is ported
    (`Config::SetPermalink` etc.); the origin `rando_config.cpp` UI for them (+765
    lines) is not re-expressed in the UiService window yet.
21. **UiService toast API**: origin toasts "Loaded Randomizer Seed" via the host UI;
    the mod logs instead. A toast primitive would be a natural UiService minor.

## Extension-point plan (decided July 6 2026)

Gap-fill design, decided with Luke. The mod API is unshipped, so breaking changes to
existing services and game ABI are in scope. Landing order: ItemService 2.0 → derived
funnels → TextService 1.1 → SaveService → StageService 1.2 → minors as convenient.

- **ItemService 2.0** (BUILT July 6, gameplay-verified: headless resolve/negative tests,
  silent + demo queue dispatch, and chest tag attribution end-to-end — `give observed: item 3
  origin 0 ('chest:D_MN05:7')`; boss-funnel tag still awaits a boss kill, expected identical
  since it shares the life-container→TrBoxDemo path). Demo-path gotcha found live: a queued give has no item event partner, so
  `daAlink_c::procCoGetItemInit` must force `mDemo.setParam0(0x100)` and spawn the item from
  the event's GtItm — a branch alink edit that was really queue machinery, now an in-tree
  seam (`give_queue_dispatching`/`give_queue_take_tag`; the spawned demo item carries the
  give's tag, and re-dispatch is bounded at 5 before dropping loudly). **Named-site give
  tags DONE (July 6)**: 32/35 sites tagged via `DUSK_GIVE_TAG` on their give funnels (shared
  gives use a PC-gated `duskCheckName` local); skipped: obj_zcloth (display-only twin of the
  tagged npc_zrz give), King Bulblin Key (gives via `changeDemoMode`, reports NULL name until
  that path is threaded), Charlo (flows through the tagged `boss:<stage>` funnel by design).
  The service: checks split into a pure/idempotent `resolve`
  (no observers — fixes chest observers firing at room load, allows display peeks and
  progressive re-resolution at pickup) and a give pipeline: `give_item(name|NULL,
  itemNo, mode)` host queue (FIFO, one demo-give in flight; `GIVE_DEMO` vs
  `GIVE_SILENT` modes — silent drains multiple per frame for Archipelago bulk
  receives; safe-moment gating lifted from the branch's proven
  `RandomizerState::addItemToEventQueue`/`execute`; volatile, cleared on slot change —
  AP resyncs from its server), `observe_gives` firing at the actual grant and seeing
  **all** gives (name=NULL for untagged), provenance carried on the item actor
  (`daItemBase_c` PC-only interned-name id threaded through the `createItemFor*`
  funnels), and `dItemNo_NONE_e` = "don't give" in both directions (vanilla-nothing
  sites resolve-and-enqueue in-tree; a real item resolving to NONE suppresses its
  give — the AP "item belongs to another world" case). This retires the
  notification-sentinel rejection honestly: sites can now always honor the resolution.
  Covers gaps 7 (the Arbiters/Stallord trigger becomes an in-tree resolve-and-enqueue
  callout; Sacred Grove keeps replace hooks only for behavior suppression), 3 (give
  half), 4 (give half), and unlocks 14/AP.
- **Five new derived-name funnels** (gaps 1, 4, 5, 6) — in-tree callouts on the
  existing check API, keys matching the branch: `freestanding:<stage>:<bitNo>`
  (`daItem_c::_daItem_create` + `daObjLife_c::create` param-init blocks; resolution
  applied by rewriting the low param byte so vanilla model machinery follows; hover/
  zero-G derived from the vanilla item type instead of a key list),
  `poe:<stage>:<bitSw>` (`e_po_dead` + `daE_HP_c::executeDead`; `addPohSpiritNum` +
  the 20-poe bit gated on resolved==vanilla; wolf-form give via the queue),
  `shop:<stage>:<itemNo>` (`dShopSystem_c::seq_decide_yes`; display side peeks the
  same name via `resolve` and builds a writable row from `dItem_data` getters —
  composes with future slot claims), `bug:<insectId>` (`daNpcIns_c::talk` + a
  PC-gated file-static insect capture in `waitPresent` — no header change),
  `sky:<stage>:<room>` (`daTagStatue_c::demoProc`; the vanilla letter-count ladder
  stays dormant under rando so its F_0796 side effect is harmless).
- **TextService 1.1** (gap 3): message-id resolver chain (key `(group<<16)|msg_no`,
  <5000 group-0 collapse) with callouts at `dMsgFlow_c::setNormalMsg` and the
  `dMsgObject_c::getRevoMessageIndex` path (Ilia); the give half enqueues via
  `give_item` at textbox close (`dMsgObject_c::waitProc` seam, `mpScrnDraw == NULL`).
- **SaveService** (gaps 18, 19): the save-creation lifecycle is redesigned as one
  flow: interactive **new-save gate chain** (host generalization of origin's
  `SELECT_DATA_PLAY_MOVE` proc — gates push UiService documents, complete
  proceed/cancel, host polls `is_any_document_visible`, any cancel backs out to file
  select) → name entry → `on_new_save`. Plus `peek_blob(slot, name, ...)` (the
  sidecar already keeps all three slots resident; warn-only staleness) and a
  **slot-info provider** (first non-pass wins) writing the `f_s_t_02`/`f_p_t_02`
  textboxes via a callout in `dFile_info_c::setSaveData` — replaces origin's
  host-settings `seedHashes` coupling.
- **StageService 1.2** (gaps 13a, 6-wolves): player-spawn record rewriter (visit
  callback over the raw spawn records in `dStage_playerInit`, mod mutates
  entrance-type bytes); **gated additions** (`should_spawn(stage, room, layer)`
  predicate evaluated at visit time) so golden-wolf rewards spawn post-howl/
  pre-obtained — the mod bakes final item ids into its ACTR records at seed load;
  collect-side flag bookkeeping is a mod post-hook on `daObjLife_c::actionGetDemo`.
- **Minors**: UiService toast wrapping `dusk::ui::push_toast` (gap 21); auto-skip
  resolver callout OR-ed onto the Start trigger in `dEvt_control_c::skipper`, passing
  `canSkip` (`mSkipFunc != NULL`) + `mSkipEventName` (gap 8); land
  `dMenu_Ring_c::updateSlotImage(u8)` in-tree as an additive member — the mod owns
  the D-pad `J2DPicture` and drives cycling via hooks (gap 9); out-of-line
  `dComIfGs_getCollectSmell` like `onStageBossEnemy` (gap 10); per-item get-demo
  params (face/SE type/doStatus) as ItemService data + three `alink_demo.inc`
  callouts (gap 11).
- **No service needed**: gap 2 (mod-side item-func diff pass), gap 12 (deferred until
  an AudioService exists), gaps 15/16/20 (mod-side UI/build work), gap 17 (slot
  claims — next ItemService unit after 2.0).

### Funnel implementation notes (July 6 branch exploration)

Branch = `origin/randomizer`, merge-base `34e1e740ab`. All override maps live in the mod
(`src/game/randomizer_context.hpp`): `mFreestandingItemOverrides`/`mPoeOverrides`
(`u16 (stageID<<8)|bitNo → u8 item`), `mShopOverrides` (`(stageID<<8)|originalItemNo`),
`mBugRewardOverrides` (`u8 bugItemId`), `mSkyCharacterOverrides` (`(stageID<<8)|roomNo`),
`mGoldenWolfOverrides` (`u16 obtainedItemFlag`). `getStageID()` is the mod's stage index
(`src/game/tools.cpp`), so in-tree callouts pass *derived names* and the mod's resolver
parses them (see the existing `chest:` parsing in `seed_session.cpp:resolve_check`).

- **Freestanding (`freestanding:<stage>:<bitNo>`)**: two callout sites, both in the one-time
  param-init blocks — `daItem_c::_daItem_create` (after `field_0x95d = true`, before
  `m_itemNo = daItem_prm::getItemNo(this)`; bitNo = `(fopAcM_GetParam(this)>>8)&0xFF`) and
  `daObjLife_c::create` (after `mIsPrmsInit = true`; bitNo = `getSaveBitNo()` =
  `fopAcM_GetParamBit(this,8,8)`). Apply resolution the branch's way: `params =
  (params & 0xFFFFFF00) | resolved; fopAcM_SetParam(...)` then re-read `m_itemNo` — all
  vanilla model/collision/arc machinery follows the param. Branch re-resolved at pickup
  (`procInitGetDemoEvent` + `itemGet`, saving/restoring the old `m_itemNo` around the
  TrBoxDemo create because the game deletes the loaded arc by old itemNo) — resolution is
  pure now, so re-resolve is legal and gives progressive correctness; tag gives with
  `give_tag("freestanding:...")`. Boss HCs (Zant/Stallord) and wall-mounted bug rewards
  need zero gravity + `mRotateSpeed = 550` when overridden — derive from the *vanilla* item
  being a heart container/insect instead of the branch's hardcoded key list. The branch's
  per-item `current.pos.y` nudges and `calcScale` targets are display polish, port last.
  The `M_ITEMNO_MODEL_ITEM_ID`/foolish-item `home.angle.z` model hack should NOT be ported —
  claimed trap-item slots with fixed `dItem_data` rows replace it. The golden-wolf sentinel
  and Ook/Gale-Boomerang blocks inside the branch's `daObjLife_c::create` belong to their
  own mechanisms (below), not this funnel.
- **Poes (`poe:<stage>:<bitSw>`)**: two sites. `e_po_dead` (file-static,
  `d_a_e_po.cpp`) — vanilla spawns `0xE0` via `createItemForPresentDemo` into
  `i_this->field_0x75C` at `a_this->current.pos`; bit = `i_this->BitSW`. `daE_HP_c::
  executeDead` (`d_a_e_hp.cpp`) — bit = local `bitSw`. At both: resolve over `0xE0`
  (POU_SPIRIT); gate `dComIfGs_addPohSpiritNum()` AND the 20-poe event bits
  (`saveBitLabels[457]` in e_po, `[0x1c9]` in e_hp) on `resolved == vanilla`. e_hp is
  collected in wolf form — the branch bypassed the present demo and queued
  (`handlePoeItem` → event queue + `procWolfAtnActorMoveInit`); prefer in-tree
  resolve-and-enqueue (`item_check_enqueue`) there if the present demo misbehaves in wolf
  form. The 60-poe cap already lives at the `addPohNum` writer.
- **Shops (`shop:<stage>:<itemNo>`)**: give side is one callout on `itemNo` in
  `dShopSystem_c::seq_decide_yes` before its `createItemForPresentDemo` (branch key uses
  the *original* event item id, not the 0-22 slot; `seq_start`'s Ordon Cat case is already
  a named check). Malo Mart sold-out (`setSoldOutFlag` when `playerIsInRoomStage(3,
  "R_SP109")`) is mod observer policy. Display side: `daShopItem_c` needs a writable row —
  the branch's `mRandoData[23]` twin of const `mData[23]` selected by an `isRandomized()`
  predicate via the `M_SHOP_DATA` macro; port that generically: peek `resolve()` at
  `getShopArcname()` time and populate the row from `dItem_data::getArcName/getBmdName/
  getBtkName/getBckName/getBrkName/getBtpName/getTevFrm(resolved)` (+ branch fixups:
  armor-slot offsetY/scale, master/light sword scale 0.35f). `CheckShopItemCreateHeap`
  (exported free fn, `d_a_shop_item_static.cpp`) must build the heap from the same row.
- **Bugs (`bug:<insectItemId>`)**: capture the handed-in insect in `daNpcIns_c::
  waitPresent` case 2 (`u8 type = dMeter2Info_getInsectSelectType()`) into a PC-gated
  file-static in `d_a_npc_ins.cpp` (do NOT port the branch's `static u8 mGivenInsectId`
  class member), then one callout on `itemNo` in `talk()` at `eventID == 1` before
  `createItemForPresentDemo`, name keyed by the captured insect id; reset the static after.
- **Sky (`sky:<stage>:<room>`)**: one callout in `daTagStatue_c::demoProc`
  `DEMO_ACTION_AWARD_ITEM` on `item` before `createItemForTrBoxDemo`; room =
  `dStage_roomControl_c::mStayNo`. Resolve over the vanilla ladder's output — under rando
  the vanilla letter count never advances (the mod's item funcs own accounting via the
  `sky_characters` blob), so the ladder's `getLetterCount()==5` → `F_0796` side effect
  stays dormant; no suppression needed.
- **Golden wolves**: no item funnel. The mod places the reward `daObjLife` actors via
  StageService `add_actor` with the final item id baked into the ACTR record at seed load;
  needs the StageService **gated-additions minor** (`should_spawn` predicate at visit
  time: `howledAtStoneFlag` set && `obtainedItemFlag` unset — flag triples from the mod's
  `getCurrentGoldenWolfFlags`, already compiled in `src/game/flags.cpp`). Collect-side
  bookkeeping (`dComIfGs_offSwitch(mapMarkerFlag)` + `onEventBit(obtainedItemFlag)`) = mod
  post-hook on `daObjLife_c::actionGetDemo` (member, hookable).
- **FLW gives (TextService 1.1)**: `dMsgFlow_c::setNormalMsg` rewrites `msg_no` →
  `getItemMessageID(item)` keyed `(dMsgObject_getGroupID()<<16)|msg_no` with the `<5000`
  group-0 collapse (same convention as flow-node overrides); second producer
  `dMsgObject_c::getRevoMessageIndex` (Ilia: `param_1 == 233 && R_SP109 room 0 && layer
  9`). The give: branch latched `g_randomizerState.mFlowMessageItemId` and granted in
  `dMsgObject_c::waitProc` when `mpScrnDraw == NULL` (textbox fully closed) — port as a
  message-id resolver chain + an in-tree enqueue (`give_item`-style) at that waitProc seam.
- **Sacred Grove / Arbiters**: Arbiters = one in-tree
  `item_check_enqueue("Arbiters Grounds Dungeon Reward", dItemNo_NONE_e, giver)` callout in
  `daB_DS_c::executeBattle2Dead` (the HC itself is the `boss:` funnel). Sacred Grove
  pedestals keep mod replace hooks on `daObjMasterSword_c::executeWait/execute` for the
  behavior suppression (map-tool event, auto-equip); their gives go through `give_item`.

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
