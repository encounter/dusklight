#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <aurora/texture.hpp>

#include <optional>
#include <string_view>

static_assert(TEXTURE_HASH_WILDCARD == aurora::texture::kWildcardTextureHash);
static_assert(TEXTURE_TLUT_WILDCARD == aurora::texture::kWildcardTlutHash);

namespace {

std::optional<aurora::texture::ReplacementKey> translate_key(const TextureKey* key) {
    if (key == nullptr || key->struct_size < sizeof(TextureKey)) {
        return std::nullopt;
    }
    switch (key->kind) {
    case TEXTURE_KEY_POINTER:
        if (key->pointer == nullptr) {
            return std::nullopt;
        }
        return aurora::texture::ReplacementKey{aurora::texture::TexturePointerKey{key->pointer}};
    case TEXTURE_KEY_SOURCE:
        if (key->width == 0 || key->height == 0) {
            return std::nullopt;
        }
        return aurora::texture::ReplacementKey{aurora::texture::TextureSourceKey{
            .textureHash = key->texture_hash,
            .tlutHash = key->tlut_hash,
            .width = key->width,
            .height = key->height,
            .format = key->gx_format,
            .hasTlut = key->has_tlut != 0,
        }};
    default:
        return std::nullopt;
    }
}

ModResult texture_register_data(ModContext* context, const TextureKey* key,
    const TextureData* data, TextureReplacementHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = dusk::mods::loader::mod_from_context(context);
    const auto translatedKey = translate_key(key);
    if (mod == nullptr || !translatedKey.has_value() || data == nullptr ||
        data->struct_size < sizeof(TextureData) || data->data == nullptr || data->size == 0 ||
        data->width == 0 || data->height == 0 || data->mip_count == 0)
    {
        return MOD_INVALID_ARGUMENT;
    }

    const auto* bytes = static_cast<const u8*>(data->data);
    const auto handle = dusk::mods::loader::texture_register_raw(*mod, *translatedKey,
        {
            .data = std::vector<u8>{bytes, bytes + data->size},
            .width = data->width,
            .height = data->height,
            .mipCount = data->mip_count,
            .gxFormat = data->gx_format,
        });
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult texture_register_file(
    ModContext* context, const char* bundlePath, TextureReplacementHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (mod == nullptr || bundlePath == nullptr ||
        !dusk::mods::loader::is_safe_resource_path(bundlePath))
    {
        return MOD_INVALID_ARGUMENT;
    }

    const std::string_view path{bundlePath};
    const auto slash = path.rfind('/');
    const auto filename = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (!aurora::texture::parse_replacement_filename(filename).has_value()) {
        DuskLog.error("[{}] texture register_file '{}' failed: "
                      "filename does not follow the replacement naming convention",
            mod->metadata.id, bundlePath);
        return MOD_INVALID_ARGUMENT;
    }

    try {
        mod->bundle->getFileSize(bundlePath);
    } catch (const std::exception& e) {
        DuskLog.error("[{}] texture register_file '{}' failed: {}", mod->metadata.id, bundlePath,
            e.what());
        return MOD_UNAVAILABLE;
    }

    const auto handle = dusk::mods::loader::texture_register_file(*mod, bundlePath);
    if (handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return MOD_OK;
}

ModResult texture_unregister(ModContext* context, TextureReplacementHandle handle) {
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    if (!dusk::mods::loader::texture_unregister(*mod, handle)) {
        DuskLog.error("[{}] texture unregister failed: unknown handle {}", mod->metadata.id, handle);
        return MOD_INVALID_ARGUMENT;
    }
    return MOD_OK;
}

constexpr TextureService s_textureService{
    .header = SERVICE_HEADER(TextureService, TEXTURE_SERVICE_MAJOR, TEXTURE_SERVICE_MINOR),
    .register_data = texture_register_data,
    .register_file = texture_register_file,
    .unregister = texture_unregister,
};

}  // namespace

namespace dusk::mods::svc {

const TextureService& texture_service() {
    return s_textureService;
}

}  // namespace dusk::mods::svc
