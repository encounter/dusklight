#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/flow.h"
#include "mods/svc/game.h"
#include "mods/svc/hook.h"
#include "mods/svc/item.h"
#include "mods/svc/log.h"
#include "mods/svc/save.h"
#include "mods/svc/stage.h"
#include "mods/svc/text.h"
#include "mods/svc/ui.h"

#include "hooks.hpp"
#include "seed_session.hpp"
#include "ui/ui.hpp"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
// Links game/host code directly (generator paths, game runtime, hooks).
IMPORT_SERVICE(GameService, svc_game);
IMPORT_SERVICE_VERSION(HookService, svc_hook, 1);
IMPORT_SERVICE(ItemService, svc_item);
IMPORT_SERVICE(FlowService, svc_flow);
IMPORT_SERVICE(TextService, svc_text);
IMPORT_SERVICE_VERSION(StageService, svc_stage, 1);
IMPORT_SERVICE(SaveService, svc_save);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE_VERSION(UiService, svc_ui, 3);

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* out_error) {
    ModResult res = randomizer::hooks::install(svc_hook);
    if (res != MOD_OK) {
        return dusk::mods::set_error(out_error, res, "failed to install game hooks");
    }

    res = randomizer::session::initialize({
        svc_item,
        svc_flow,
        svc_text,
        svc_stage,
        svc_save,
        svc_config,
    });
    if (res != MOD_OK) {
        return dusk::mods::set_error(out_error, res, "failed to initialize seed session");
    }

    res = randomizer::ui::initialize(svc_ui, randomizer::session::pending_seed_var());
    if (res != MOD_OK) {
        return dusk::mods::set_error(out_error, res, "failed to register randomizer UI");
    }

    svc_log->info(mod_ctx, "randomizer initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    randomizer::session::update();
    randomizer::ui::update();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    randomizer::session::deactivate_seed();
    svc_log->info(mod_ctx, "randomizer unloaded");
    return MOD_OK;
}
}
