// Companion provider mod for mod_test's dependency-ordering checks. mod_test requires
// both services below, so this mod must be initialized first even though its bundle
// sorts after mod_test.dusk in the mods directory.

#include "mods/service.hpp"
#include "mods/svc/host.h"
#include "mods/svc/log.h"
#include "test_services.h"

DEFINE_MOD();
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(LogService, svc_log);
// Forms a cycle with mod_test's required import of TestDepService. The loader breaks it
// by dropping this optional edge: the slot still resolves (the export is static), but
// mod_test is not initialized yet when this mod runs.
IMPORT_OPTIONAL_SERVICE(TestMainService, svc_test_main);

namespace {

int32_t g_initialized = 0;

int32_t test_dep_initialized(ModContext*) {
    return g_initialized;
}

int32_t test_dep_magic(ModContext*) {
    return TEST_DEP_DEFERRED_MAGIC;
}

constexpr TestDepService s_testDepService{
    .header = SERVICE_HEADER(TestDepService, TEST_DEP_SERVICE_MAJOR, TEST_DEP_SERVICE_MINOR),
    .initialized = test_dep_initialized,
};

constexpr TestDepDeferredService s_testDepDeferredService{
    .header = SERVICE_HEADER(
        TestDepDeferredService, TEST_DEP_DEFERRED_SERVICE_MAJOR, TEST_DEP_DEFERRED_SERVICE_MINOR),
    .magic = test_dep_magic,
};

}  // namespace

EXPORT_SERVICE(s_testDepService);
EXPORT_DEFERRED_SERVICE(test_dep_deferred, TEST_DEP_DEFERRED_SERVICE_ID,
    TEST_DEP_DEFERRED_SERVICE_MAJOR, TEST_DEP_DEFERRED_SERVICE_MINOR);

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, "mod_test_dep initializing");

    if (svc_test_main == nullptr) {
        svc_log->warn(mod_ctx, "optional cycle import did not resolve");
    } else if (svc_test_main->initialized(mod_ctx) == 0) {
        svc_log->info(mod_ctx, "optional cycle import OK: resolved, provider uninitialized");
    } else {
        svc_log->error(mod_ctx, "optional cycle import BROKEN: mod_test initialized first");
    }

    const ModResult result = svc_host->publish_service(mod_ctx, TEST_DEP_DEFERRED_SERVICE_ID,
        TEST_DEP_DEFERRED_SERVICE_MAJOR, &s_testDepDeferredService);
    if (result != MOD_OK) {
        return dusk::mods::set_error(error, result, "failed to publish deferred service");
    }

    g_initialized = 1;
    svc_log->info(mod_ctx, "mod_test_dep ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    svc_log->info(mod_ctx, "mod_test_dep unloaded");
    return MOD_OK;
}
}
