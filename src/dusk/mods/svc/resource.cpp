#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {

bool is_safe_resource_path(std::string_view path) {
    if (path.empty() || path.starts_with('/') || path.starts_with('\\') ||
        path.find(':') != std::string_view::npos)
    {
        return false;
    }

    while (!path.empty()) {
        const auto slash = path.find_first_of("/\\");
        const auto segment = path.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }

    return true;
}

ModResult resource_load(ModContext* context, const char* relativePath, ResourceBuffer* outBuffer) {
    auto* mod = dusk::mods::loader::mod_from_context(context);
    if (outBuffer == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    *outBuffer = ResourceBuffer{sizeof(ResourceBuffer), nullptr, 0u};
    if (mod == nullptr || relativePath == nullptr || !is_safe_resource_path(relativePath)) {
        return MOD_INVALID_ARGUMENT;
    }

    std::vector<u8> data;
    const std::string entry = std::string{"res/"} + relativePath;
    try {
        data = mod->bundle->readFile(entry);
    } catch (const std::runtime_error& e) {
        DuskLog.error("[{}] resource load '{}' failed: {}", mod->metadata.id, entry, e.what());
        return MOD_UNAVAILABLE;
    }

    void* retPtr = nullptr;
    if (!data.empty()) {
        retPtr = std::malloc(data.size());
        if (retPtr == nullptr) {
            return MOD_ERROR;
        }
        std::memcpy(retPtr, data.data(), data.size());
    }

    outBuffer->data = retPtr;
    outBuffer->size = data.size();
    return MOD_OK;
}

void resource_free(ModContext*, ResourceBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    std::free(buffer->data);
    *buffer = ResourceBuffer{sizeof(ResourceBuffer), nullptr, 0u};
}

const ResourceService s_resourceService{
    .header = SERVICE_HEADER(ResourceService, RESOURCE_SERVICE_MAJOR, RESOURCE_SERVICE_MINOR),
    .load = resource_load,
    .free = resource_free,
};

}  // namespace

namespace dusk::mods::svc {

const ResourceService& resource_service() {
    return s_resourceService;
}

}  // namespace dusk::mods::svc
