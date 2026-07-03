# Dusklight Mod API

Mods are distributed as `.dusk` files: zip archives containing a `mod.json` manifest and, optionally, compiled code libraries and resources. The loader scans the mods directory at startup and initializes each mod.

Everything a mod does besides calling game code goes through **services** — small, versioned C APIs. Dusklight provides built-in services (logging, resources, hooks, ...), and mods can define their own to talk to each other.

## Table of Contents

1. [Getting Started](#getting-started)
2. [mod.json](#modjson)
3. [Anatomy of a Code Mod](#anatomy-of-a-code-mod)
4. [Services](#services)
5. [Built-in Services](#built-in-services)
6. [Hooking Game Functions](#hooking-game-functions)
7. [Asset Overlays](#asset-overlays)
8. [Runtime Lifecycle](#runtime-lifecycle)
9. [Error Handling](#error-handling)
10. [Advanced: Exporting Services](#advanced-exporting-services)

---

## Getting Started

Fork the [mod template](../tools/mod_template/), a self-contained CMake project that references Dusklight as a subdirectory:

```
my_mod/
├── CMakeLists.txt
├── mod.json
├── src/mod.cpp
├── res/       (optional bundled resources)
└── overlay/   (optional game file overrides)
```

**CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_mod CXX)

set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dusk" CACHE PATH "Path to dusk source root")
add_subdirectory("${DUSK_DIR}" dusk EXCLUDE_FROM_ALL)

add_dusk_mod(my_mod
    SOURCES      src/mod.cpp
    MOD_JSON     mod.json
    RES_DIR      res        # optional
    OVERLAY_DIR  overlay    # optional
    TEXTURES_DIR textures   # optional
)
```

Building produces `my_mod.dusk` in `mods/` next to the project root (configurable via the `DUSK_MODS_OUTPUT_DIR` cache variable). Copy it into the game's mods folder and launch:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

You can also pass `--mods <dir>` on the command line, which is handy during development.

A `.dusk` may contain one library per platform/architecture. The loader only considers libraries with the host platform's extension (`.dll` on Windows, `.dylib` on macOS, `.so` on Linux), preferring one whose name ends in the host's architecture suffix (`_arm64`, `_x64`, `_x86`); a library with no architecture suffix is treated as arch-neutral (e.g. a macOS universal binary). If the bundle contains libraries but none matches the host platform and architecture, the mod is disabled with an error rather than loading a mismatched library. A bundle with no libraries at all is loaded as an asset-only mod. Mods can be enabled, disabled, and reloaded at runtime from the Mods window; the enabled setting persists (see [Runtime Lifecycle](#runtime-lifecycle)).

---

## mod.json

```json
{
    "id":          "com.example.my_mod",
    "name":        "My Mod",
    "version":     "1.0.0",
    "author":      "Your Name",
    "description": "A short description shown in the mod manager.",
    "icon":        "res/my_icon.png",
    "banner":      "res/my_banner.png"
}
```

`id` is required: a unique, stable identifier (reverse-DNS style; periods, underscores, and alphanumerics). Everything else is optional but recommended — `name` falls back to the filename. Whether a mod has code is inferred from the libraries in the bundle; asset-overlay-only mods simply ship none.

`icon` and `banner` are bundle-relative paths to PNG images shown in the Mods window: the icon (square, e.g. 512x512) next to your mod in the list, the banner (wide, roughly 3.5:1) as the detail-page header — it is scaled and center-cropped to cover the header area, so keep important content away from the edges. Both keys are optional; if omitted, `res/icon.png` and `res/banner.png` are picked up automatically when present.

---

## Anatomy of a Code Mod

```cpp
#include "mods/service.hpp"
#include "mods/svc/log.h"

DEFINE_MOD();                          // once, in exactly one translation unit
IMPORT_SERVICE(LogService, svc_log);   // resolved by the loader before mod_initialize

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, "hello from my_mod");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError* error) {   // called every frame
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError* error) {
    return MOD_OK;
}
}
```

All three lifecycle exports are required. `mod_ctx` is your mod's identity token, set by the loader before `mod_initialize` runs — pass it as the first argument to every service call.

Mods link against the game itself: include game headers and call game functions directly, just like engine code does.

---

## Services

A service is a struct of C function pointers with a version header. You declare what you use at file scope, and the loader resolves it before your mod initializes:

```cpp
IMPORT_SERVICE(LogService, svc_log);                    // required, any minor version
IMPORT_SERVICE_VERSION(LogService, svc_log, 2);         // required, minor >= 2
IMPORT_OPTIONAL_SERVICE(SomeService, svc_maybe);        // may be null — check it
```

The rules (see `include/mods/api.h` for the full contract):

- **A required import is guaranteed valid.** If the service is missing or too old, your mod fails to load with a clear error — you never need a null check at a call site.
- **Anything at or below the minor version you imported can be called unconditionally.**
- Optional imports may be null; check once in `mod_initialize`.
- Fields newer than your imported minor must be gated behind `SERVICE_HAS(service, ServiceType, field)` plus a null check.

Service versions follow one rule: a **major** bump is a breaking change (treated as a different service entirely), a **minor** bump only appends functions.

---

## Built-in Services

### LogService (`mods/svc/log.h`)

```cpp
IMPORT_SERVICE(LogService, svc_log);

svc_log->info(mod_ctx, "spawned the thing");
svc_log->warn(mod_ctx, "that looks wrong");
svc_log->error(mod_ctx, "very bad");
svc_log->write(mod_ctx, LOG_LEVEL_DEBUG, "verbose details");
```

Messages appear in the console prefixed with your mod id. Messages are plain strings — use `snprintf` for formatting.

### ResourceService (`mods/svc/resource.h`)

Loads files from the `res/` tree of your `.dusk` archive. Paths are relative to `res/` (pass `"config.txt"`, not `"res/config.txt"`); absolute paths and `..` are rejected.

```cpp
IMPORT_SERVICE(ResourceService, svc_resource);

ResourceBuffer buf = RESOURCE_BUFFER_INIT;
if (svc_resource->load(mod_ctx, "config.txt", &buf) == MOD_OK) {
    // buf.data / buf.size
    svc_resource->free(mod_ctx, &buf);
}
```

Missing files return `MOD_UNAVAILABLE`. Always `free` what you `load`. For writable storage, use the directory from `svc_host->mod_dir(mod_ctx)`.

### HostService (`mods/svc/host.h`)

Mod metadata and runtime interaction with the loader:

```cpp
IMPORT_SERVICE(HostService, svc_host);

const char* id  = svc_host->mod_id(mod_ctx);
const char* dir = svc_host->mod_dir(mod_ctx);  // writable per-mod directory
svc_host->fail(mod_ctx, MOD_ERROR, "something unrecoverable happened");  // disables the mod
```

`get_service`/`publish_service` provide dynamic service lookup; see [Advanced](#advanced-exporting-services).

### HookService (`mods/svc/hook.h`)

Install hooks on game functions. You'll rarely call it directly — use the typed helpers in `mods/hook.hpp` described below.

### OverlayService (`mods/svc/overlay.h`)

Registers DVD file overlays at runtime — the dynamic counterpart to the static `overlay/` directory (see [Asset Overlays](#asset-overlays)). Overlay a disc path with a file from your bundle, or with a caller-owned buffer (copied on registration):

```cpp
IMPORT_SERVICE(OverlayService, svc_overlay);

OverlayHandle handle = 0;
svc_overlay->add_file(mod_ctx, "/res/Msgus.arc", "res/replacement.arc", &handle);
svc_overlay->add_buffer(mod_ctx, "/generated.txt", data, size, nullptr);
svc_overlay->remove(mod_ctx, handle);
```

`disc_path` must be absolute (leading `/`) and is matched against the disc case-insensitively; paths that don't exist on the disc are added as new files. Changes are applied at the next frame boundary, and data the game already read stays in memory until the file is re-read (usually a scene reload). Registrations follow your mod's lifecycle — disable/reload removes them. If multiple sources overlay the same path, the last one wins: your runtime registrations beat your static `overlay/` files, and later-loaded mods beat earlier ones (a cross-mod conflict logs a warning).

### TextureService (`mods/svc/texture.h`)

Registers texture replacements at runtime — the dynamic counterpart to the static `textures/` directory (see [Asset Overlays](#asset-overlays)). Two forms: raw texel data with an explicit key, or an encoded `.dds`/`.png` from your bundle whose filename encodes the key:

```cpp
IMPORT_SERVICE(TextureService, svc_texture);

// Encoded file; filename follows the replacement naming convention.
TextureReplacementHandle handle = 0;
svc_texture->register_file(mod_ctx, "res/tex1_32x32_$_6.png", &handle);

// Raw data: match by texel-data pointer or by content hash (TEXTURE_KEY_SOURCE).
TextureKey key = TEXTURE_KEY_INIT;
key.kind = TEXTURE_KEY_POINTER;
key.pointer = someTexObj.data;
TextureData data = TEXTURE_DATA_INIT;
data.data = pixels; data.size = pixelsSize;
data.width = 32; data.height = 32; data.gx_format = GX_TF_RGBA8_PC;
svc_texture->register_data(mod_ctx, &key, &data, nullptr);

svc_texture->unregister(mod_ctx, handle);
```

Filenames use the same convention as the user's `texture_replacements` directory: `tex1_{w}x{h}_{texhash}[_{tluthash}]_{fmt}.dds|.png`, where hashes may be `$` (wildcard; `TEXTURE_HASH_WILDCARD`/`TEXTURE_TLUT_WILDCARD` in `register_data` keys). `_mipN` sidecar files next to a registered file are picked up automatically. Files are decoded lazily on first use by the renderer; raw data is copied at registration. Registrations follow your mod's lifecycle. Priority when several sources replace the same texture: later-loaded mods beat earlier ones, and any mod beats the user's `texture_replacements` config directory.

### ConfigService (`mods/svc/config.h`)

Persistent, mod-scoped configuration variables. Each var is stored in the user's `config.json` under `mod.<escaped mod id>.<name>` (escaping: `.` → `_`, `_` → `__`, so `com.example.my_mod` becomes `com_example_my__mod`), next to the host's own settings:

```cpp
IMPORT_SERVICE(ConfigService, svc_config);

ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
desc.name = "speedMultiplier";  // 1-64 chars from [A-Za-z0-9_-]; "enabled" is reserved
desc.type = CONFIG_VAR_FLOAT;
desc.default_float = 1.0;
ConfigVarHandle var = 0;
svc_config->register_var(mod_ctx, &desc, &var);

double speed = 1.0;
svc_config->get_float(mod_ctx, var, &speed);
svc_config->set_float(mod_ctx, var, 2.0);

// Optional: get notified when the value changes.
void on_speed_changed(ModContext* ctx, ConfigVarHandle var, const ConfigVarValue* value,
    const ConfigVarValue* previous, void* user_data) {
    /* value->float_value is the new value, previous->float_value the old one */
}
svc_config->subscribe(mod_ctx, var, on_speed_changed, nullptr, nullptr);
```

Types: `CONFIG_VAR_BOOL` (`bool`), `CONFIG_VAR_INT` (`int64_t`), `CONFIG_VAR_FLOAT` (`double`), `CONFIG_VAR_STRING` (UTF-8; `get_string` copies into a caller buffer — pass a `NULL` buffer with size 0 to query the length). Accessors are typed and must match the registration.

If a value was saved in an earlier session (or supplied via `--cvar mod.<escaped id>.<name>=<value>`), it takes effect at registration; otherwise the var starts at its default. Writes are debounced — they reach `config.json` within a couple of seconds, not per call, and always on clean shutdown. Registrations and subscriptions follow your mod's lifecycle (removed on disable/reload/failure), but persisted values survive and are restored by the next registration of the same name.

Change callbacks fire on the game thread whenever the value actually changes at runtime — your own `set_*` calls included; writes that store the same value are silent. Values applied from `config.json` or `--cvar` at registration do **not** fire callbacks — read the value after `register_var` for your starting state. The callback receives the new value and the one it replaced as `ConfigVarValue` snapshots (valid only during the call; copy `string_value` if you need to keep it). Setting the same var from inside its own callback applies the write but is not re-notified.

### UiService (`mods/svc/ui.h`)

UI primitives built on the host's RmlUi layer: a panel inside your mod's tab of the Mods window, mod-owned tabbed windows, modal dialogs, RCSS style sheets scoped to classes of host documents, and document-stack queries. All calls must happen on the game thread, from your mod's callbacks (initialize, update, hooks, or UI callbacks). [mod_test](../tools/mod_test/src/mod.cpp) exercises the whole surface.

**Handles.** Every object is an opaque, generation-checked `uint64_t` handle (0 is never valid). A stale, foreign, or wrong-kind handle fails with `MOD_INVALID_ARGUMENT` and an error log — never undefined behavior. Element handles die with the content that owns them: a panel or tab rebuild (e.g. a tab switch) destroys the previous build's elements, so re-acquire handles inside your build callback instead of caching them across builds. Strings are UTF-8 and valid only for the duration of a call, in both directions.

**Mods-window panel.** Registers a panel rendered in your mod's tab of the host Mods window; `build` runs (again) every time the tab content is (re)built, `update` runs every frame while it is the visible tab:

```cpp
IMPORT_SERVICE_VERSION(UiService, svc_ui, 2);

UiElementHandle statusText = 0;

ModResult build(ModContext*, UiElementHandle panel, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, panel, "Status");
    svc_ui->pane_add_text(mod_ctx, panel, "starting...", &statusText);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "self-test", /*ok=*/1, nullptr);
    svc_ui->pane_add_progress(mod_ctx, panel, 0.5f, nullptr);
    return MOD_OK;
}
ModResult update(ModContext*, void*, ModError*) {
    svc_ui->elem_set_text(mod_ctx, statusText, "running");
    return MOD_OK;
}

// in mod_initialize:
UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
panel.build = build;
panel.update = update;
svc_ui->register_mods_panel(mod_ctx, &panel);
```

Element setters must match the element kind: `elem_set_text`/`elem_set_rml` on text rows, `elem_set_badge` on badge rows, `elem_set_progress` on progress bars. A non-`MOD_OK` result from `build`/`update` fails your mod, as do exceptions escaping any UI callback.

**Controls.** `pane_add_control` adds an input row described by a tagged `UiControlDesc`: `UI_CONTROL_BUTTON` (action), `UI_CONTROL_TOGGLE` (bool), `UI_CONTROL_NUMBER` (integer stepper with `min`/`max`/`step`/`prefix`/`suffix`), `UI_CONTROL_STRING` (text input), `UI_CONTROL_SELECT` (one of `options`; the value is the option index). Values bind one of two ways:

- `UI_BINDING_CALLBACKS`: you supply `get`/`set` functions trading `UiControlValue`s. Getters are polled every frame while the control is visible — keep them cheap.
- `UI_BINDING_CONFIG_VAR`: the control reads and writes one of your [ConfigService](#configservice-modssvcconfigh) vars directly — persistence, change notifications, and the modified indicator (value ≠ default) come for free. The var type must match the control: TOGGLE = bool, NUMBER and SELECT = int, STRING = string. Float vars are not bindable — use callbacks and convert.

```cpp
UiControlDesc control = UI_CONTROL_DESC_INIT;
control.kind = UI_CONTROL_TOGGLE;
control.label = "Enable rainbows";
control.help_rml = "Shown in the help pane while focused.";
control.binding = UI_BINDING_CONFIG_VAR;
control.config_var = myBoolVar;  // from svc_config->register_var
svc_ui->pane_add_control(mod_ctx, leftPane, &control, nullptr);
```

`help_rml` (and SELECT's option list) render in the help pane, so they only work inside mod window tabs; Mods-window panels have a single pane, where `help_rml` is ignored and SELECT fails with `MOD_UNSUPPORTED`.

**Windows.** `window_push` pushes a tabbed two-pane window (the same shell as the host Settings window) onto the document stack and shows it. Stacking works like host windows: the document that was on top hides while yours is open, and closing yours brings it back (if nothing was visible when you pushed — say, from a gameplay hook — closing simply returns to the game). Each tab's `build` receives the window handle plus fresh left (interactive) and right (help) pane handles on every activation; the optional per-tab `update` runs each frame while that tab is active. `on_closed` fires when the window is destroyed for any reason — user close, `window_close`, shutdown — except your own mod's teardown (your `mod_shutdown` has already run by then); the handle is already invalid inside the callback. `desc.rcss` optionally styles that window's document only; mod windows carry a `mod-window` root class so scoped RCSS can target them (`window.mod-window ...`).

```cpp
UiTabDesc tabs[1] = {UI_TAB_DESC_INIT};
tabs[0].title = "My Mod";
tabs[0].build = my_tab_build;  // (ctx, window, left_pane, right_pane, user_data, error)

UiWindowDesc desc = UI_WINDOW_DESC_INIT;
desc.tabs = tabs;
desc.tab_count = 1;
desc.on_closed = my_on_closed;
UiWindowHandle window = 0;
svc_ui->window_push(mod_ctx, &desc, &window);
```

**Dialogs.** `dialog_push` shows a modal dialog wrapping the host's dialog shell. `variant` picks the style (`UI_DIALOG_NORMAL`, `UI_DIALOG_WARNING`, `UI_DIALOG_DANGER` — red styling), `icon` optionally overrides the variant's default icon (`"warning"`, `"error"`, `"question-mark"`, ...). Actions become buttons; after an action's `on_pressed` returns the dialog closes unless `keep_open` is set. Cancel (B/Escape) fires `on_dismiss` (if any) and always closes. Dialogs are for messages and confirmation — input controls inside dialogs ("form dialogs") are not supported yet.

```cpp
UiDialogAction actions[2] = {
    {"Cancel", nullptr, nullptr, 0},
    {"Delete", on_delete_confirmed, nullptr, 0},
};
UiDialogDesc dialog = UI_DIALOG_DESC_INIT;
dialog.title = "Delete save?";
dialog.body_rml = "This cannot be undone.";
dialog.variant = UI_DIALOG_DANGER;
dialog.actions = actions;
dialog.action_count = 2;
svc_ui->dialog_push(mod_ctx, &dialog, nullptr);
```

**Scoped styles.** `register_styles(scope, rcss, &handle)` applies an RCSS sheet to every document of a scope — existing documents restyle immediately, future ones pick it up at creation — until `unregister_styles` or your mod is disabled/reloaded (styles re-apply when it comes back). `register_styles_file(scope, path, &handle)` does the same but reads the RCSS from your bundle's `res/` directory (same path rules as `ResourceService::load`; `MOD_UNAVAILABLE` if missing). Scopes: `UI_SCOPE_PRELAUNCH`, `UI_SCOPE_WINDOW` (every tabbed/small window, host and mod alike), `UI_SCOPE_MENU_BAR`, `UI_SCOPE_OVERLAY` (toasts, FPS counter), `UI_SCOPE_TOUCH_CONTROLS`, `UI_SCOPE_GRAPHICS_TUNER`. Sheets apply after the host's styles in registration order. RCSS that fails to parse is rejected with `MOD_INVALID_ARGUMENT` (note RCSS parsing is lenient; most typos only produce console warnings). This is cooperative — a `UI_SCOPE_WINDOW` sheet restyles the host Settings window too, so scope selectors tightly (e.g. to `window.mod-window`) unless changing host UI is the point.

**Document stack.** `is_any_document_visible` reports whether any focus-stack document is showing (game input is blocked while one is). `focus_top_document` shows and focuses the top of the stack. `close_top_document` asks the top document to close: window-like documents (host and mod windows, dialogs) comply; permanent documents (menu bar, pre-launch) refuse with `MOD_UNSUPPORTED`. This can close a host window the user has open — cooperative model, use judiciously.

### GfxService (`mods/svc/gfx.h`)

Raw WebGPU access integrated into the game's frame, plus hooks at named points of the render process. Mods call the `wgpu*` C API directly — the host exports Dawn's symbols (on Windows, `add_dusk_mod` links the same Dawn DLL the host loads), and the mod SDK puts `webgpu/webgpu.h` on your include path. Never link your own copy of Dawn into a mod. `get_proc_address` resolves any `wgpu*` entry point by name if direct linking is impractical (e.g. non-C++ bindings). **The service's major version also pins the WebGPU ABI**: it is bumped when a Dusklight Dawn update changes `webgpu.h`, so a mismatched mod fails at load instead of corrupting. [crt_mod](../tools/crt_mod/src/mod.cpp) is the reference consumer — a complete post-process filter in ~300 lines.

**Threading contract.** Every service function must be called on the game thread. There are two kinds of callbacks:

- `GfxStageFn` (stage hooks) run on the **game thread** during frame recording; the pass-scoped calls below are valid inside.
- `GfxDrawFn` (draw callbacks) run on the **render worker thread** while the frame is encoded, with the live `WGPURenderPassEncoder`. They may use the handles in their `GfxDrawContext` and raw `wgpu*` calls — and nothing else. No service may be called from a draw callback; hand results back through memory you own (e.g. atomics).

All WGPU handles the service hands out are borrowed: draw-context handles are valid only during the callback, resolved-target views for the current frame. GPU objects you create yourself (pipelines, samplers, buffers) are your responsibility — release them in `mod_shutdown` (the host guarantees no draw callback of yours can still be running by then).

**Stages.** `register_stage_hook(stage, ...)` fires your callback at a named point of every rendered frame (frame interpolation included — read interpolated state fresh each call; `GfxStageContext.interpolated_frame` tells you which kind of frame it is):

- `GFX_STAGE_WORLD_LATE` — inside the 3D scene, after terrain/shadow lists, before object and translucent lists. The place to record opaque world geometry with proper depth. May fire once per camera window.
- `GFX_STAGE_BEFORE_HUD` — the 3D scene and wipe are complete; no 2D/HUD lists yet. Post-process the world while leaving the HUD crisp.
- `GFX_STAGE_AFTER_HUD` — everything including the HUD is drawn. Whole-frame post-processing.

The host UI (RmlUi windows, ImGui console) composites after the frame is done, so it always renders on top of anything you draw at any stage.

**Custom draws.** Register a draw type once (`register_draw_type` — the `GfxDrawFn` plus a debug label), then record inline draws with `push_draw(handle, payload, size)` from a stage callback (or any GX-record-time hook). The payload (≤ `GFX_INLINE_DRAW_PAYLOAD_SIZE`, 128 bytes) is copied **as raw bytes** and handed back to your draw callback on the worker — it must be trivially copyable (`static_assert(std::is_trivially_copyable_v<...>)` is cheap insurance), and pointers inside it must remain valid through the frame's encode (frame-pooled objects like resolved views qualify; stack locals do not). Put bulk data in the shared streaming buffers via `push_verts`/`push_indices`/`push_uniform`/`push_storage`, which return `GfxRange` offsets into the buffers exposed in `GfxDrawContext` (offsets valid for the current frame only). WGSL uniform layout rules apply to anything you push as uniforms — pad your C struct to 16-byte multiples and keep it in sync with the shader. Draws execute at exactly the recorded position in the frame; the host restores GX pipeline/bind-group/viewport/scissor state after your callback, so set whatever state you need.

**GPU resource strategy.** `get_device_info` is valid from `mod_initialize` onward and **stable for the session** (MSAA/format changes require a restart), so build pipelines for scene-pass draws at init — no first-use shader-compile hitch. Pipelines must match the containing pass exactly: color format, MSAA `sample_count`, *and* the depth format declared (with depth test/write disabled) even if you never touch depth; only offscreen passes from `create_pass` differ (always single-sample), so key pipeline variants on `GfxDrawContext` if you draw into those. Creating a bind group per draw on the worker is the simple default and fine for a few draws per frame (the pass keeps its own reference — release yours right after `SetBindGroup`). For many draws, cache instead: the four streaming buffers are the **same objects for the entire session**, so bind groups referencing them stay valid; the only per-frame variance is the `GfxRange` offset (use a dynamic-offset uniform binding on an explicit bind group layout) and resolved-target views (frame-pooled — the same few handles repeat across frames until the resolution changes, so a small view-keyed cache works). Release everything you created in `mod_shutdown`; the host guarantees no draw callback of yours can still be running by then.

**Pass primitives.** `resolve_pass(desc, &targets)` snapshots the current pass targets into pooled textures — `desc.color` gives the scene color (single-sample, post-MSAA-resolve), `desc.depth` a raw-depth `R32Float` snapshot (check `uses_reversed_z` from `get_device_info`; may be NULL if the device lacks depth-sampling support) — then rendering continues on a fresh pass that loads the existing contents. `create_pass(width, height)` opens a cleared single-sample offscreen pass; subsequent draws target it until `resolve_pass` returns its snapshot and restores the scene pass. Balance every `create_pass` with a `resolve_pass` before your stage callback returns (leaving one open fails your mod), and don't nest them. Both return `MOD_UNAVAILABLE` outside an active pass (the true no-pass window is `mod_initialize` — `mod_update` runs inside the frame) and while the game is inside its own offscreen scope.

The post-process recipe (see crt_mod): register a draw type and an `AFTER_HUD` stage hook once; per frame, from the stage callback, `resolve_pass` the scene, `push_uniform` your parameters, and `push_draw` with the snapshot view in the payload; in the draw callback, lazily create your pipeline, bind the snapshot + uniforms, and draw a fullscreen triangle back over the frame. Multi-resolution chains (bloom pyramids, half-res AO) alternate `create_pass`/`resolve_pass` between full-size passes. Multiple mods compose naturally: each resolve sees everything drawn before it, in stage-hook registration order.

**Compute tasks (minor 1).** `register_compute_type` + `push_compute` splice compute work into the middle of the frame: `push_compute(handle, payload, size)` splits the scene pass at the current position, and your `GfxComputeFn` runs on the render worker with the frame's `WGPUCommandEncoder` between the two halves — dispatches see everything drawn (and resolved) before the call, and draws recorded after it see the compute output. The callback may begin/end any number of compute passes and record copies on the encoder; leave no pass open when returning and never `Finish`/`Release` the encoder. A single compute pass can chain dependent dispatches (WebGPU synchronizes between dispatches, so a storage texture written by one is readable by the next). Payload rules match `push_draw`. The typical multi-pass effect shape: `resolve_pass` for input snapshots → `push_compute` running the whole chain into mod-owned storage textures → `push_draw` compositing the result back over the scene. `MOD_UNAVAILABLE` outside an active pass or while any offscreen pass is open (compute can't split an offscreen pass — order it before `create_pass` or after its `resolve_pass`).

### CameraService (`mods/svc/camera.h`)

A single call, `get_camera(&info)`, snapshots the active game camera — the companion to GfxService's depth snapshots for any screen-space technique (SSAO, contact shadows, deferred decals) that needs to unproject depth back to view or world space.

**Matrix conventions.** Every matrix in `CameraInfo` is a column-major `float[16]` using the matrix × column-vector convention — memcpy it straight into a WGSL `mat4x4f`. Names read right to left: `a_from_b` maps b-space into a-space (`view_from_world` is the view matrix, `world_from_proj` the one-step depth-buffer→world unproject). This is the **transpose** of the game's row-major `Mtx`/`Mtx44` layout; if you hook game code and want the raw game matrices, read `dComIfGd_getView()` directly instead.

**Projection semantics.** `proj_from_view` is in WebGPU clip convention **exactly as rendered**: reversed-Z (depth 1.0 at the near plane, 0.0 at far — matching `GfxResolvedTargets` depth snapshots) and including any widescreen adjustment the game applied this frame. That last part matters: don't rebuild a projection from `fovy`/`aspect` and expect it to match the depth buffer — use the matrices. To unproject a depth texel at UV `(u, v)` with sampled depth `d`:

```wgsl
let ndc = vec4f(u * 2.0 - 1.0, 1.0 - v * 2.0, d, 1.0); // framebuffer y is down
let world4 = uniforms.world_from_proj * ndc;
let world = world4.xyz / world4.w;
```

(If `viewport_near_z`/`viewport_far_z` are not the usual 0/1, remap `d` first as documented in the header.)

**When to call.** Game thread only. Call from a gfx stage callback (or `mod_update`, which runs inside the frame) and the values match the frame being recorded — frame interpolation rewrites the live camera in place before each rendered frame, so this is automatically correct on interpolated frames too. During `GFX_STAGE_WORLD_LATE` you get the current camera window's camera; at `BEFORE_HUD`/`AFTER_HUD` the last window's. Returns `MOD_UNAVAILABLE` while no camera exists (menus before the first in-game frame, and always during `mod_initialize`) — handle it by skipping your effect that frame.

---

## Hooking Game Functions

`mods/hook.hpp` provides typed helpers over the hook service:

```cpp
#include "mods/hook.hpp"
#include "mods/svc/hook.h"

IMPORT_SERVICE(HookService, svc_hook);
```

### Pre-hooks

Run before the original. Return `HOOK_SKIP_ORIGINAL` to cancel it (post-hooks still run).

```cpp
HookAction on_pos_move_pre(ModContext*, void* args, void* retval, void* userdata) {
    daAlink_c* link = dusk::mods::arg<daAlink_c*>(args, 0);  // arg 0 is `this`
    if (link->shape_angle.y > 10000) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

dusk::mods::hook_add_pre<&daAlink_c::posMove>(svc_hook, on_pos_move_pre);
```

### Post-hooks

Run after the original (or after a replace-hook, or after a cancelled original). `retval` points to the return value, if any.

```cpp
void on_pos_move_post(ModContext*, void* args, void* retval, void* userdata) { ... }

dusk::mods::hook_add_post<&daAlink_c::posMove>(svc_hook, on_pos_move_post);
```

### Replace-hooks

Substitute the original entirely. Call through to it via `HookEntry<...>::g_orig` if needed:

```cpp
using ExecuteEntry = dusk::mods::HookEntry<&daAlink_c::execute>;

void on_execute_replace(ModContext*, void* args, void* retval, void*) {
    int result = ExecuteEntry::g_orig(dusk::mods::arg<daAlink_c*>(args, 0));
    if (retval != nullptr) {
        *static_cast<int*>(retval) = result;
    }
}

dusk::mods::hook_set_replace<&daAlink_c::execute>(svc_hook, on_execute_replace);
```

By default a second replace-hook on the same function is a conflict; `HookOptions` (`replace_policy`, `priority`, `userdata`) controls this and callback ordering. Multiple mods can attach pre/post hooks to the same function independently.

### Reading and writing arguments

`args` is an array of pointers to the arguments. For member functions, index 0 is `this`; parameters follow in declaration order.

```cpp
T  value = dusk::mods::arg<T>(args, n);      // copy
T& ref   = dusk::mods::arg_ref<T>(args, n);  // read/write reference
```

```cpp
// void daEnemy_c::takeDamage(int amount, daActor_c* source) — halve incoming damage
HookAction on_take_damage_pre(ModContext*, void* args, void*, void*) {
    dusk::mods::arg_ref<int>(args, 1) /= 2;
    return HOOK_CONTINUE;
}
```

For reference parameters (e.g. `const cXyz& pos`), `arg_ref<cXyz>` yields a direct reference.

---

## Asset Overlays

Files placed under `overlay/` in the `.dusk` archive override game files at the corresponding path. For example, `overlay/res/Stage/...` shadows that file on the game disc image. This requires no code — an archive with just `mod.json` and `overlay/` is a complete mod.

Files placed under `textures/` register as texture replacements the same way. Filenames follow the replacement naming convention (`tex1_{w}x{h}_{texhash}[_{tluthash}]_{fmt}.dds|.png`, `$` as a hash wildcard, `_mipN` sidecars for mip levels) — the same one the game's texture-dumping option produces, so a dump-rename-repack loop needs no code either. Subdirectories are fine; only the filename determines what a file replaces.

Both follow the mod's lifecycle: disabling the mod removes its overrides (files revert to the disc contents on their next open; added files stop existing), and reloading serves the new bundle's content. Game data the engine already read stays as-is until it is loaded again — asset changes typically need a scene reload to become visible. When mods collide, later-loaded mods win, and mods' texture replacements beat the user's `texture_replacements` config directory; cross-mod conflicts log a warning.

To decide overlays and replacements at runtime instead, see [OverlayService](#overlayservice-modssvcoverlayh) and [TextureService](#textureservice-modssvctextureh).

---

## Runtime Lifecycle

Mods can be disabled, re-enabled, and reloaded from the Mods window without restarting the game. Write your mod assuming this happens:

- **Disable** calls `mod_shutdown`, removes your hooks, services, UI, overlays, and texture replacements (both static and runtime-registered), and unloads your library.
- **Enable** and **Reload** load a *fresh copy* of your library — all statics are back at their initial values, imports are re-resolved, and `mod_initialize` runs again. You never see a second `mod_initialize` on the same image, so idempotence hacks are unnecessary; just make `mod_shutdown` release anything the loader doesn't manage for you (threads, files, game-side state you mutated).
- **Reload** additionally re-reads the `.dusk` from disk, picking up a rebuilt library and changed assets. This is the fast iteration loop during development: rebuild, click Reload.

A reload may freely change the mod's service imports and exports — the loader rebuilds the dependency graph and restart ordering to match. The one thing that must not change is the mod `id` (that's a different mod; restart the game).

**Dependents restart too.** Disabling or reloading a mod that exports services shuts down the mods importing them first (in reverse dependency order) and brings them back afterwards. A mod whose *required* provider is disabled stays down — shown as "Suspended" in the Mods window — and resumes automatically when the provider returns. Mods with an *optional* import of a disabled provider restart with that import null, exactly like a startup where the provider is absent.

**One caution for hooks:** lifecycle changes are applied between frames, which is safe for hooks on functions that return every frame (effectively everything you'd normally hook). Avoid hooking a function that stays on the stack for the whole session (e.g. the outermost main loop); a mod that does cannot be safely unloaded.

---

## Error Handling

Service calls report failure through `ModResult` return values (`MOD_OK`, `MOD_UNAVAILABLE`, `MOD_INVALID_ARGUMENT`, ...). Lifecycle exports additionally receive a `ModError*`: fill it (e.g. with `dusk::mods::set_error(error, code, "message")`) and return the code, and the loader disables the mod and shows the message to the user.

```cpp
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = dusk::mods::hook_add_pre<&daAlink_c::posMove>(svc_hook, on_pre);
    if (result != MOD_OK) {
        return dusk::mods::set_error(error, result, "failed to hook posMove");
    }
    return MOD_OK;
}
```

Throwing exceptions out of lifecycle functions also disables the mod (they are caught by the loader), but prefer explicit results.

---

## Advanced: Exporting Services

Mods can provide services to other mods. Define the interface in a header both mods share:

```cpp
// my_mod_api.h
#include "mods/api.h"

#define MY_MOD_SERVICE_ID "com.example.my_mod.api"
#define MY_MOD_SERVICE_MAJOR 1u
#define MY_MOD_SERVICE_MINOR 0u

typedef struct MyModService {
    ServiceHeader header;
    ModResult (*do_thing)(ModContext* ctx, int value);
} MyModService;

#ifdef __cplusplus
#include "mods/service.hpp"
template <>
struct dusk::mods::ServiceTraits<MyModService> {
    static constexpr const char* id = MY_MOD_SERVICE_ID;
    static constexpr uint16_t major_version = MY_MOD_SERVICE_MAJOR;
};
#endif
```

**Provider:**

```cpp
ModResult do_thing(ModContext* ctx, int value) { ... }

constexpr MyModService g_service{
    .header = SERVICE_HEADER(MyModService, MY_MOD_SERVICE_MAJOR, MY_MOD_SERVICE_MINOR),
    .do_thing = do_thing,
};
EXPORT_SERVICE(g_service);
```

**Consumer:**

```cpp
IMPORT_SERVICE(MyModService, svc_my_mod);
// or IMPORT_OPTIONAL_SERVICE if the dependency is optional

svc_my_mod->do_thing(mod_ctx, 42);
```

The loader registers all exports before resolving any imports, so declaration order between mods doesn't matter. Note that the `ctx` a provider receives identifies the *calling* mod.

### Dependencies between mods

Service imports are also dependency declarations: the loader initializes mods in dependency order, so by the time your `mod_initialize` runs, every mod you import services from — required *or* optional — has already finished its own `mod_initialize`. This includes deferred services: a service the provider publishes during its initialization resolves into your import slot just like a static export.

Consequences of that contract:

- If a provider fails to load, every mod that *requires* one of its services is disabled too, with an error naming the provider. Optional imports of a failed provider simply resolve to `NULL`.
- Mods whose **required** imports form a cycle all fail to load. If the cycle runs through an **optional** import, the loader breaks it there: the optional import still resolves, but its provider may not be initialized yet when you run — treat it accordingly.
- `svc_host->get_service(...)` is outside this system. It sees whatever is published at call time and gives no initialization-order guarantee, which also makes it the escape hatch for intentionally cyclic designs.

Mods shut down in reverse initialization order, so services you import remain safe to call from `mod_shutdown`.

Rules for providers:

- Service ids are global — use reverse-DNS names you control.
- Every function pointer covered by your declared minor version must be populated.
- Within a major version, only append fields; never reorder, remove, or repurpose them. Breaking changes require a major bump (which is, in effect, a new service).
- Only one provider per `(id, major)` pair may be registered; duplicates are load errors.

For services whose construction can't happen at static-init time, declare the export with `EXPORT_DEFERRED_SERVICE(...)` and publish the pointer later via `svc_host->publish_service(...)`. Consumers can fetch services dynamically with `svc_host->get_service(...)`; prefer manifest imports whenever possible, since they give the loader dependency information and fail fast with good errors.
