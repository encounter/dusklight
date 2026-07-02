#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>
#include "miniz.h"

#include <aurora/texture.hpp>

#include "dusk/mod_loader.hpp"

namespace dusk::mods::loader {

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

}  // namespace dusk::mods::loader
