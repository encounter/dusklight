#pragma once

#include "mods/api.h"

#define RESOURCE_SERVICE_ID "dev.twilitrealm.dusklight.resource"
#define RESOURCE_SERVICE_MAJOR 2u
#define RESOURCE_SERVICE_MINOR 0u

typedef struct ResourceBuffer {
    uint32_t struct_size;
    void* data;
    size_t size;
} ResourceBuffer;

#define RESOURCE_BUFFER_INIT {sizeof(ResourceBuffer), NULL, 0u}

typedef struct ResourceService {
    ServiceHeader header;

    ModResult (*load)(ModContext* ctx, const char* relative_path, ResourceBuffer* out_buffer);
    void (*free)(ModContext* ctx, ResourceBuffer* buffer);
} ResourceService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<ResourceService> {
    static constexpr const char* id = RESOURCE_SERVICE_ID;
    static constexpr uint16_t major_version = RESOURCE_SERVICE_MAJOR;
};
#endif
