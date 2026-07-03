#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>
#include "miniz.h"

#include <aurora/texture.hpp>

#include "dusk/mod_loader.hpp"
#include "mods/svc/config.h"
#include "mods/svc/gfx.h"
#include "mods/svc/ui.h"

namespace dusk::ui {
class Pane;
}  // namespace dusk::ui

namespace dusk::mods {

#if DUSK_CODE_MODS
constexpr bool EnableCodeMods = true;
#else
constexpr bool EnableCodeMods = false;
#endif

// Implementations must be safe for concurrent calls: overlay file reads run on DVD threads
// while the game thread reads resources from the same bundle.
class ModBundle {
public:
    virtual ~ModBundle() = default;

    virtual std::vector<u8> readFile(const std::string& fileName) = 0;
    virtual std::vector<std::string> getFileNames() = 0;
    virtual size_t getFileSize(const std::string& fileName) = 0;
};

class ModBundleZip final : public ModBundle {
public:
    explicit ModBundleZip(std::vector<u8>&& data);
    ~ModBundleZip() override;
    std::vector<u8> readFile(const std::string& fileName) override;
    std::vector<std::string> getFileNames() override;
    size_t getFileSize(const std::string& fileName) override;

private:
    std::vector<uint8_t> zip_data;
    mz_zip_archive res_zip{};
    bool res_zip_open = false;
    std::mutex m_mutex;
};

class ModBundleDisk final : public ModBundle {
public:
    explicit ModBundleDisk(std::filesystem::path root);
    ~ModBundleDisk() override = default;
    std::vector<u8> readFile(const std::string& fileName) override;
    std::vector<std::string> getFileNames() override;
    size_t getFileSize(const std::string& fileName) override;

private:
    [[nodiscard]] std::filesystem::path toRealPath(const std::string& fileName) const;
    std::filesystem::path root_path;
};

LoadedMod* mod_from_context(ModContext* context);
const LoadedMod* mod_from_context(const ModContext* context);
const char* mod_id_from_context(ModContext* context);
void fail_mod(LoadedMod& mod, ModResult code, std::string_view message);
bool is_safe_resource_path(std::string_view path);
std::string escape_mod_id_for_config(std::string_view id);

uint64_t overlay_add_file(
    LoadedMod& mod, std::string discPath, std::string bundlePath, size_t size);
uint64_t overlay_add_buffer(LoadedMod& mod, std::string discPath, std::vector<u8> data);
bool overlay_remove(LoadedMod& mod, uint64_t handle);
void overlay_remove_mod(LoadedMod& mod);
bool consume_overlays_dirty();

struct TextureRawData {
    std::vector<u8> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 1;
    uint32_t gxFormat = 0;
};
uint64_t texture_register_raw(
    LoadedMod& mod, const aurora::texture::ReplacementKey& key, TextureRawData data);
uint64_t texture_register_file(LoadedMod& mod, std::string bundlePath);
bool texture_unregister(LoadedMod& mod, uint64_t handle);
void textures_remove_mod(LoadedMod& mod);

struct ModConfigVarSpec {
    std::string fragment;  // validated by the service layer
    uint32_t type = 0;     // ConfigVarType
    bool defaultBool = false;
    int64_t defaultInt = 0;
    double defaultFloat = 0.0;
    std::string defaultString;
};
ModResult config_register_var(LoadedMod& mod, const ModConfigVarSpec& spec, uint64_t& outHandle);
ModResult config_unregister_var(LoadedMod& mod, uint64_t handle);
config::ConfigVarBase* config_find_var(LoadedMod& mod, uint64_t handle, uint32_t expectedType);
ModResult config_subscribe(LoadedMod& mod, uint64_t varHandle, ConfigChangedFn callback,
    void* userData, uint64_t& outHandle);
ModResult config_unsubscribe(LoadedMod& mod, uint64_t handle);
void config_remove_mod(LoadedMod& mod);
void config_mark_dirty();
void config_flush_if_dirty(bool force);

// UI service plumbing (loader/ui.cpp). Game thread only, like config.
ModResult ui_register_mods_panel(LoadedMod& mod, const UiModsPanelDesc& desc);
// Called by the host ModsWindow to build/update the mod's registered panels into its tab.
void ui_build_mods_panels(LoadedMod& mod, dusk::ui::Pane& pane);
void ui_update_mods_panels(LoadedMod& mod);
ModResult ui_pane_add_section(LoadedMod& mod, uint64_t pane, const char* title);
ModResult ui_pane_add_text(LoadedMod& mod, uint64_t pane, const char* text, uint64_t* outElem);
ModResult ui_pane_add_rml(LoadedMod& mod, uint64_t pane, const char* rml, uint64_t* outElem);
ModResult ui_pane_add_badge_row(
    LoadedMod& mod, uint64_t pane, const char* label, bool ok, uint64_t* outElem);
ModResult ui_pane_add_progress(LoadedMod& mod, uint64_t pane, float value, uint64_t* outElem);
ModResult ui_pane_add_control(
    LoadedMod& mod, uint64_t pane, const UiControlDesc& desc, uint64_t* outElem);
ModResult ui_elem_set_text(LoadedMod& mod, uint64_t elem, const char* text);
ModResult ui_elem_set_rml(LoadedMod& mod, uint64_t elem, const char* rml);
ModResult ui_elem_set_badge(LoadedMod& mod, uint64_t elem, bool ok);
ModResult ui_elem_set_progress(LoadedMod& mod, uint64_t elem, float value);
ModResult ui_window_push(LoadedMod& mod, const UiWindowDesc& desc, uint64_t& outHandle);
ModResult ui_window_close(LoadedMod& mod, uint64_t handle);
ModResult ui_dialog_push(LoadedMod& mod, const UiDialogDesc& desc, uint64_t& outHandle);
ModResult ui_dialog_close(LoadedMod& mod, uint64_t handle);
bool ui_any_document_visible();
void ui_focus_top_document();
ModResult ui_close_top_document(LoadedMod& mod);
ModResult ui_register_styles(LoadedMod& mod, uint32_t scope, const char* rcss, uint64_t& outHandle);
ModResult ui_register_styles_file(
    LoadedMod& mod, uint32_t scope, const char* path, uint64_t& outHandle);
ModResult ui_unregister_styles(LoadedMod& mod, uint64_t handle);
void ui_remove_mod(LoadedMod& mod);

// Gfx service plumbing (loader/gfx.cpp). Unlike the other loader records, the slot table is
// shared with Aurora's render worker thread (draw trampolines) and internally mutex-guarded;
// the functions below are still game-thread-only.
enum class GfxStreamBuffer : u8 { Verts, Indices, Uniform, Storage };
ModResult gfx_register_draw_type(
    LoadedMod& mod, const char* label, GfxDrawFn draw, void* userData, uint64_t& outHandle);
ModResult gfx_unregister_draw_type(LoadedMod& mod, uint64_t handle);
ModResult gfx_push_draw(LoadedMod& mod, uint64_t handle, const void* payload, size_t payloadSize);
ModResult gfx_push_stream(
    GfxStreamBuffer buffer, const void* data, size_t size, size_t alignment, GfxRange& outRange);
ModResult gfx_register_stage_hook(
    LoadedMod& mod, GfxStage stage, GfxStageFn callback, void* userData, uint64_t& outHandle);
ModResult gfx_unregister_stage_hook(LoadedMod& mod, uint64_t handle);
ModResult gfx_resolve_pass(LoadedMod& mod, const GfxResolveDesc& desc, GfxResolvedTargets& out);
ModResult gfx_create_pass(LoadedMod& mod, uint32_t width, uint32_t height);
ModResult gfx_register_compute_type(
    LoadedMod& mod, const char* label, GfxComputeFn callback, void* userData, uint64_t& outHandle);
ModResult gfx_unregister_compute_type(LoadedMod& mod, uint64_t handle);
ModResult gfx_push_compute(
    LoadedMod& mod, uint64_t handle, const void* payload, size_t payloadSize);
// gfx_run_stage (the frame markers' entry point) is declared in dusk/mods/gfx_stages.hpp.
// Applies failures queued by render-worker trampolines; top of ModLoader::tick.
void gfx_drain_worker_failures();
void gfx_remove_mod(LoadedMod& mod);

}  // namespace dusk::mods
