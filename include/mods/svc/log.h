#pragma once

#include "mods/api.h"

#define LOG_SERVICE_ID "dev.twilitrealm.dusklight.log"
#define LOG_SERVICE_MAJOR 1u
#define LOG_SERVICE_MINOR 0u

typedef enum LogLevel {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
} LogLevel;

typedef struct LogService {
    ServiceHeader header;

    void (*write)(ModContext* ctx, LogLevel level, const char* message);
    void (*trace)(ModContext* ctx, const char* message);
    void (*debug)(ModContext* ctx, const char* message);
    void (*info)(ModContext* ctx, const char* message);
    void (*warn)(ModContext* ctx, const char* message);
    void (*error)(ModContext* ctx, const char* message);
} LogService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<LogService> {
    static constexpr const char* id = LOG_SERVICE_ID;
    static constexpr uint16_t major_version = LOG_SERVICE_MAJOR;
};
#endif
