#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <cstdint>
#include <string_view>

namespace {

constexpr size_t kMaxOverlayFileSize = UINT32_MAX;

bool is_valid_disc_path(const char* discPath) {
    if (discPath == nullptr) {
        return false;
    }
    const std::string_view path{discPath};
    return path.starts_with('/') && dusk::mods::loader::is_safe_resource_path(path.substr(1));
}

ModResult overlay_add_file(
    ModContext* context, const char* discPath, const char* bundlePath, OverlayHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (mod == nullptr || !is_valid_disc_path(discPath) || bundlePath == nullptr ||
        !dusk::mods::loader::is_safe_resource_path(bundlePath))
    {
        return MOD_INVALID_ARGUMENT;
    }

    size_t size = 0;
    try {
        size = mod->bundle->getFileSize(bundlePath);
    } catch (const std::exception& e) {
        DuskLog.error(
            "[{}] overlay add_file '{}' failed: {}", mod->metadata.id, bundlePath, e.what());
        return MOD_UNAVAILABLE;
    }
    if (size > kMaxOverlayFileSize) {
        DuskLog.error("[{}] overlay add_file '{}' failed: file too large ({} bytes)",
            mod->metadata.id, bundlePath, size);
        return MOD_INVALID_ARGUMENT;
    }

    const auto handle = dusk::mods::loader::overlay_add_file(*mod, discPath, bundlePath, size);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult overlay_add_buffer(ModContext* context, const char* discPath, const void* data,
    size_t size, OverlayHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (mod == nullptr || !is_valid_disc_path(discPath) || (data == nullptr && size != 0) ||
        size > kMaxOverlayFileSize)
    {
        return MOD_INVALID_ARGUMENT;
    }

    const auto* bytes = static_cast<const u8*>(data);
    const auto handle = dusk::mods::loader::overlay_add_buffer(
        *mod, discPath, std::vector<u8>{bytes, bytes + size});
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult overlay_remove(ModContext* context, OverlayHandle handle) {
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    if (!dusk::mods::loader::overlay_remove(*mod, handle)) {
        DuskLog.error("[{}] overlay remove failed: unknown handle {}", mod->metadata.id, handle);
        return MOD_INVALID_ARGUMENT;
    }
    return MOD_OK;
}

constexpr OverlayService s_overlayService{
    .header = SERVICE_HEADER(OverlayService, OVERLAY_SERVICE_MAJOR, OVERLAY_SERVICE_MINOR),
    .add_file = overlay_add_file,
    .add_buffer = overlay_add_buffer,
    .remove = overlay_remove,
};

}  // namespace

namespace dusk::mods::svc {

const OverlayService& overlay_service() {
    return s_overlayService;
}

}  // namespace dusk::mods::svc
