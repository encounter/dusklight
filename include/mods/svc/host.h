#pragma once

#include "mods/api.h"

#define HOST_SERVICE_ID "dev.twilitrealm.dusklight.host"
#define HOST_SERVICE_MAJOR 1u
#define HOST_SERVICE_MINOR 0u

typedef struct HostService {
    ServiceHeader header;

    ModResult (*get_service)(ModContext* ctx, const char* service_id, uint16_t major_version,
        uint16_t min_minor_version, const void** out_service);
    ModResult (*publish_service)(
        ModContext* ctx, const char* service_id, uint16_t major_version, const void* service);
    void (*fail)(ModContext* ctx, ModResult code, const char* message);

    const char* (*mod_id)(ModContext* ctx);
    const char* (*mod_name)(ModContext* ctx);
    const char* (*mod_version)(ModContext* ctx);
    const char* (*mod_dir)(ModContext* ctx);
} HostService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<HostService> {
    static constexpr const char* id = HOST_SERVICE_ID;
    static constexpr uint16_t major_version = HOST_SERVICE_MAJOR;
};
#endif
