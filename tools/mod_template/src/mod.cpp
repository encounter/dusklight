#include "mods/service.hpp"
#include "mods/svc/log.h"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError*) {
    SERVICE_CALL(svc_log, info, "template_mod initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    SERVICE_CALL(svc_log, info, "template_mod unloaded");
    return MOD_OK;
}
}
