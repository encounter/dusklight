#pragma once

// Test services shared between mod_test_dep (provider) and mod_test (importer) to
// exercise inter-mod dependency ordering.

#include "mods/api.h"

#define TEST_DEP_SERVICE_ID "dev.twilitrealm.test_mod_dep.static"
#define TEST_DEP_SERVICE_MAJOR 1u
#define TEST_DEP_SERVICE_MINOR 0u

typedef struct TestDepService {
    ServiceHeader header;

    /* Returns 1 once mod_test_dep's mod_initialize has completed. */
    int32_t (*initialized)(ModContext* ctx);
} TestDepService;

#define TEST_DEP_DEFERRED_SERVICE_ID "dev.twilitrealm.test_mod_dep.deferred"
#define TEST_DEP_DEFERRED_SERVICE_MAJOR 1u
#define TEST_DEP_DEFERRED_SERVICE_MINOR 0u
#define TEST_DEP_DEFERRED_MAGIC 1234

typedef struct TestDepDeferredService {
    ServiceHeader header;

    /* Returns TEST_DEP_DEFERRED_MAGIC. */
    int32_t (*magic)(ModContext* ctx);
} TestDepDeferredService;

/*
 * Exported by mod_test. mod_test_dep imports it *optionally*, which together with
 * mod_test's required import of TestDepService forms a dependency cycle that the
 * loader must break by dropping the optional edge.
 */
#define TEST_MAIN_SERVICE_ID "dev.twilitrealm.test_mod.main"
#define TEST_MAIN_SERVICE_MAJOR 1u
#define TEST_MAIN_SERVICE_MINOR 0u

typedef struct TestMainService {
    ServiceHeader header;

    /* Returns 1 once mod_test's mod_initialize has completed. */
    int32_t (*initialized)(ModContext* ctx);
} TestMainService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<TestDepService> {
    static constexpr const char* id = TEST_DEP_SERVICE_ID;
    static constexpr uint16_t major_version = TEST_DEP_SERVICE_MAJOR;
};

template <>
struct dusk::mods::ServiceTraits<TestDepDeferredService> {
    static constexpr const char* id = TEST_DEP_DEFERRED_SERVICE_ID;
    static constexpr uint16_t major_version = TEST_DEP_DEFERRED_SERVICE_MAJOR;
};

template <>
struct dusk::mods::ServiceTraits<TestMainService> {
    static constexpr const char* id = TEST_MAIN_SERVICE_ID;
    static constexpr uint16_t major_version = TEST_MAIN_SERVICE_MAJOR;
};
#endif
