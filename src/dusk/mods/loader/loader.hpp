#pragma once

#include <filesystem>
#include <string_view>
#include "miniz.h"

#include "dusk/mod_loader.hpp"

namespace dusk::mods::loader {

#if DUSK_CODE_MODS
constexpr bool EnableCodeMods = true;
#else
constexpr bool EnableCodeMods = false;
#endif

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

}  // namespace dusk::mods::loader
