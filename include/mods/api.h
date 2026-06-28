#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#if defined(_WIN32)
#define MOD_EXPORT __declspec(dllexport)
#else
#define MOD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define MOD_EXTERN_C extern "C"
#else
#define MOD_EXTERN_C
#endif

#define MOD_ABI_VERSION 4u
#define MOD_ERROR_MESSAGE_SIZE 512u

typedef struct ModContext ModContext;

typedef enum ModResult {
    MOD_OK = 0,
    MOD_ERROR = 1,
    MOD_UNAVAILABLE = 2,
    MOD_UNSUPPORTED = 3,
    MOD_CONFLICT = 4,
    MOD_INVALID_ARGUMENT = 5,
    MOD_RUNTIME_DISABLE = 6,
} ModResult;

typedef struct ModError {
    uint32_t struct_size;
    ModResult code;
    char message[MOD_ERROR_MESSAGE_SIZE];
} ModError;

#define MOD_ERROR_INIT {sizeof(ModError), MOD_OK, {0}}

extern ModContext* mod_context;

typedef struct ServiceHeader {
    uint32_t struct_size;
    uint16_t major_version;
    uint16_t minor_version;
} ServiceHeader;

#define SERVICE_HEADER(service_type, major, minor) {sizeof(service_type), (major), (minor)}

#define SERVICE_HAS(service, service_type, field)                                                  \
    ((service) != NULL &&                                                                          \
        (service)->header.struct_size >=                                                           \
            (uint32_t)(offsetof(service_type, field) + sizeof(((service_type*)0)->field)))

typedef enum ServiceImportFlags {
    SERVICE_IMPORT_REQUIRED = 0u,
    SERVICE_IMPORT_OPTIONAL = 1u << 0u,
} ServiceImportFlags;

typedef enum ServiceExportFlags {
    SERVICE_EXPORT_STATIC = 0u,
    SERVICE_EXPORT_DEFERRED = 1u << 0u,
} ServiceExportFlags;

typedef struct ServiceImport {
    uint32_t struct_size;
    const char* service_id;
    uint16_t major_version;
    uint16_t min_minor_version;
    uint32_t flags;
    void* slot;
} ServiceImport;

typedef struct ServiceExport {
    uint32_t struct_size;
    const char* service_id;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t flags;
    const void* service;
} ServiceExport;

typedef struct ModManifest {
    uint32_t struct_size;
    uint32_t abi_version;
    const ServiceImport* imports;
    size_t import_count;
    const ServiceExport* exports;
    size_t export_count;
} ModManifest;

typedef const ModManifest* (*ModGetManifestFn)(void);

typedef ModResult (*ModInitializeFn)(ModError* out_error);
typedef ModResult (*ModUpdateFn)(ModError* out_error);
typedef ModResult (*ModShutdownFn)(ModError* out_error);

MOD_EXPORT const ModManifest* mod_get_manifest(void);

MOD_EXPORT ModResult mod_initialize(ModError* out_error);
MOD_EXPORT ModResult mod_update(ModError* out_error);
MOD_EXPORT ModResult mod_shutdown(ModError* out_error);

#ifdef __cplusplus
}
#endif
