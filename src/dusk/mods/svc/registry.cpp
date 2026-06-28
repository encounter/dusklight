#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <string_view>
#include <unordered_map>

namespace {

std::unordered_map<std::string, dusk::mods::svc::ServiceRecord> s_services;

std::string service_key(std::string_view id, const uint16_t majorVersion) {
    std::string key{id};
    key.push_back('\x1f');
    key += std::to_string(majorVersion);
    return key;
}

const char* mod_id(const dusk::LoadedMod* mod) {
    return mod != nullptr ? mod->metadata.id.c_str() : "dusklight";
}

bool validate_service_header(const ServiceHeader* header, const char* serviceId,
    const uint16_t majorVersion, const uint16_t minorVersion, dusk::LoadedMod* provider) {
    if (header == nullptr) {
        DuskLog.error("[{}] service '{}' has null header", mod_id(provider), serviceId);
        return false;
    }
    if (header->struct_size < sizeof(ServiceHeader)) {
        DuskLog.error("[{}] service '{}' has invalid header size {}", mod_id(provider), serviceId,
            header->struct_size);
        return false;
    }
    if (header->major_version != majorVersion || header->minor_version != minorVersion) {
        DuskLog.error("[{}] service '{}' header version {}.{} does not match export {}.{}",
            mod_id(provider), serviceId, header->major_version, header->minor_version, majorVersion,
            minorVersion);
        return false;
    }
    return true;
}

}  // namespace

namespace dusk::mods::svc {

bool valid_service_id(const char* serviceId) {
    return serviceId != nullptr && serviceId[0] != '\0';
}

ModResult register_service(const char* serviceId, const uint16_t majorVersion,
    const uint16_t minorVersion, const void* service, LoadedMod* provider, const bool deferred) {
    if (!valid_service_id(serviceId)) {
        DuskLog.error("[{}] attempted to register a service with no id", mod_id(provider));
        return MOD_INVALID_ARGUMENT;
    }

    if (!deferred && !validate_service_header(static_cast<const ServiceHeader*>(service), serviceId,
                         majorVersion, minorVersion, provider))
    {
        return MOD_INVALID_ARGUMENT;
    }

    const auto key = service_key(serviceId, majorVersion);
    if (s_services.contains(key)) {
        DuskLog.error("[{}] duplicate service '{}@{}'", mod_id(provider), serviceId, majorVersion);
        return MOD_CONFLICT;
    }

    s_services.emplace(key, ServiceRecord{
                                serviceId,
                                majorVersion,
                                minorVersion,
                                service,
                                provider,
                                deferred,
                            });
    return MOD_OK;
}

ModResult publish_deferred_service(
    LoadedMod& provider, const char* serviceId, const uint16_t majorVersion, const void* service) {
    if (!valid_service_id(serviceId) || service == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    const auto it = s_services.find(service_key(serviceId, majorVersion));
    if (it == s_services.end() || !it->second.deferred || it->second.provider != &provider) {
        DuskLog.error("[{}] tried to publish undeclared service '{}@{}'", provider.metadata.id,
            serviceId, majorVersion);
        return MOD_UNSUPPORTED;
    }
    auto& record = it->second;
    if (record.service != nullptr) {
        return MOD_CONFLICT;
    }

    const auto* header = static_cast<const ServiceHeader*>(service);
    if (!validate_service_header(header, serviceId, majorVersion, record.minorVersion, &provider)) {
        return MOD_INVALID_ARGUMENT;
    }

    record.service = service;
    record.minorVersion = header->minor_version;
    return MOD_OK;
}

void remove_services_for_provider(const LoadedMod& provider) {
    std::erase_if(
        s_services, [&](const auto& entry) { return entry.second.provider == &provider; });
}

const ServiceRecord* find_service(
    const char* serviceId, const uint16_t majorVersion, const uint16_t minMinorVersion) {
    if (!valid_service_id(serviceId)) {
        return nullptr;
    }

    const auto it = s_services.find(service_key(serviceId, majorVersion));
    if (it == s_services.end()) {
        return nullptr;
    }
    const auto& record = it->second;
    if (record.service == nullptr || record.minorVersion < minMinorVersion) {
        return nullptr;
    }
    return &record;
}

void clear_services() {
    s_services.clear();
}

}  // namespace dusk::mods::svc

namespace dusk {

void ModLoader::initializeServices() {
    mods::svc::clear_services();
    mods::svc::register_service(HOST_SERVICE_ID, HOST_SERVICE_MAJOR, HOST_SERVICE_MINOR,
        &mods::svc::host_service(), nullptr, false);
    mods::svc::register_service(LOG_SERVICE_ID, LOG_SERVICE_MAJOR, LOG_SERVICE_MINOR,
        &mods::svc::log_service(), nullptr, false);
    mods::svc::register_service(RESOURCE_SERVICE_ID, RESOURCE_SERVICE_MAJOR, RESOURCE_SERVICE_MINOR,
        &mods::svc::resource_service(), nullptr, false);
    mods::svc::register_service(UI_SERVICE_ID, UI_SERVICE_MAJOR, UI_SERVICE_MINOR,
        &mods::svc::ui_service(), nullptr, false);
    mods::svc::register_service(HOOK_SERVICE_ID, HOOK_SERVICE_MAJOR, HOOK_SERVICE_MINOR,
        &mods::svc::hook_service(), nullptr, false);
}

bool ModLoader::registerStaticServiceExports(LoadedMod& mod) {
    if (!mod.native || mod.native->manifest == nullptr) {
        return true;
    }

    const auto& manifest = *mod.native->manifest;
    for (size_t i = 0; i < manifest.export_count; ++i) {
        const auto& serviceExport = manifest.exports[i];
        if (serviceExport.struct_size != sizeof(ServiceExport) ||
            !mods::svc::valid_service_id(serviceExport.service_id))
        {
            mods::loader::fail_mod(mod, MOD_INVALID_ARGUMENT, "invalid service export descriptor");
            mods::svc::remove_services_for_provider(mod);
            return false;
        }

        const bool deferred = (serviceExport.flags & SERVICE_EXPORT_DEFERRED) != 0;
        if (!deferred && serviceExport.service == nullptr) {
            mods::loader::fail_mod(
                mod, MOD_INVALID_ARGUMENT, "static service export has null service pointer");
            mods::svc::remove_services_for_provider(mod);
            return false;
        }

        const auto result =
            mods::svc::register_service(serviceExport.service_id, serviceExport.major_version,
                serviceExport.minor_version, serviceExport.service, &mod, deferred);
        if (result != MOD_OK) {
            mods::loader::fail_mod(mod, result, "service export registration failed");
            mods::svc::remove_services_for_provider(mod);
            return false;
        }
    }

    return true;
}

bool ModLoader::resolveServiceImports(LoadedMod& mod) {
    if (!mod.native || mod.native->manifest == nullptr) {
        return true;
    }

    const auto& manifest = *mod.native->manifest;
    for (size_t i = 0; i < manifest.import_count; ++i) {
        const auto& serviceImport = manifest.imports[i];
        if (serviceImport.struct_size != sizeof(ServiceImport) ||
            !mods::svc::valid_service_id(serviceImport.service_id) || serviceImport.slot == nullptr)
        {
            mods::loader::fail_mod(mod, MOD_INVALID_ARGUMENT, "invalid service import descriptor");
            return false;
        }

        const auto* service = mods::svc::find_service(
            serviceImport.service_id, serviceImport.major_version, serviceImport.min_minor_version);
        if (service == nullptr) {
            *static_cast<const void**>(serviceImport.slot) = nullptr;
            if ((serviceImport.flags & SERVICE_IMPORT_OPTIONAL) != 0) {
                continue;
            }

            mods::loader::fail_mod(mod, MOD_UNAVAILABLE,
                std::string{"required service unavailable: "} + serviceImport.service_id);
            return false;
        }

        *static_cast<const void**>(serviceImport.slot) = service->service;
    }

    return true;
}

void ModLoader::clearServices() {
    mods::svc::clear_services();
}

void ModLoader::failMod(LoadedMod& mod, const ModResult code, std::string_view message) {
    mods::loader::fail_mod(mod, code, message);
}

}  // namespace dusk
