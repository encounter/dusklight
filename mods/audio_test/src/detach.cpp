#include <mods/service.hpp>
#include <mods/svc/audio.h>
#include <mods/svc/host.h>
#include <mods/svc/log.h>
#include <array>
#include <cstring>

DEFINE_MOD();
IMPORT_SERVICE(AudioService, svc_audio);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(LogService, svc_log);
namespace {
uint32_t waitTicks{};
void detached(ModContext*, ModContext*, const char* id, ModLifecycleEvent event, void*) {
    if (event == MOD_LIFECYCLE_DETACHED && std::strcmp(id, "dev.twilitrealm.audio_test") == 0) waitTicks = 1;
}
}
extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    uint64_t watcher{};
    return svc_host->watch_mod_lifecycle(mod_ctx, detached, nullptr, &watcher);
}
MOD_EXPORT ModResult mod_update(ModError*) {
    if (!waitTicks || ++waitTicks < 30) return MOD_OK;
    waitTicks = 0;
    std::array<AudioStreamHandle, 4> handles{};
    auto desc = *svc_audio->default_stream_desc;
    desc.volume = 0;
    desc.duck_bgm = false;
    for (auto& handle : handles) {
        if (svc_audio->open(mod_ctx, &desc, &handle) != MOD_OK) {
            svc_log->error(mod_ctx, "audio_test: stream capacity leaked after owner detach");
            return MOD_ERROR;
        }
    }
    for (auto handle : handles) svc_audio->close(mod_ctx, handle);
    svc_log->info(mod_ctx, "audio_test: owner detach and stream capacity reuse passed");
    return MOD_OK;
}
MOD_EXPORT ModResult mod_shutdown(ModError*) { return MOD_OK; }
}
