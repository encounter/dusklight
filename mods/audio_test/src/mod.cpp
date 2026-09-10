#include <mods/service.hpp>
#include <mods/svc/audio.h>
#include <mods/svc/log.h>
#include <mods/svc/host.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

DEFINE_MOD();
IMPORT_SERVICE(AudioService, svc_audio);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HostService, svc_host);
namespace {
std::array<AudioStreamHandle, 4> handles{};
std::array<float, 10080 * 2> silence{};
uint32_t step{}, ticks{}, waitTicks{}, testCase{}, written{};
uint64_t pausedPosition{};
constexpr std::array<uint32_t, 8> lengths{1, 5039, 5040, 10081, 45360, 45401, 48000, 44100};
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}
void ok(ModResult result) {
    if (result != MOD_OK) {
        char message[128]{};
        std::snprintf(message, sizeof(message), "AudioService result %d at step %u tick %u case %u", int(result), step, ticks, testCase);
        throw std::runtime_error{message};
    }
}
AudioStreamState state(AudioStreamHandle handle) {
    AudioStreamState value{};
    ok(svc_audio->get_state(mod_ctx, handle, &value));
    return value;
}
uint32_t feed(AudioStreamHandle handle, uint32_t count = 10080) {
    uint32_t accepted{};
    ok(svc_audio->write(mod_ctx, handle, silence.data(), count, &accepted));
    require(accepted <= count, "write exceeded requested count");
    return accepted;
}
AudioStreamHandle open_stream(uint32_t rate = 32000, bool monoFloat = false) {
    auto desc = *svc_audio->default_stream_desc;
    desc.sample_rate = rate;
    if (rate == 44100) desc.prepare_frames = 1;
    if (monoFloat) { desc.channels = 1; desc.format = AUDIO_FORMAT_F32; }
    desc.volume = 0;
    desc.duck_bgm = false;
    desc.stop_on_scene_change = false;
    desc.ring_frames = 15120;
    AudioStreamHandle handle{};
    ok(svc_audio->open(mod_ctx, &desc, &handle));
    require(handle != 0, "open returned zero handle");
    return handle;
}
void mark(const char* text) { svc_log->info(mod_ctx, text); }
}
extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) { return MOD_OK; }
MOD_EXPORT ModResult mod_update(ModError* error) {
    try {
        if (step == 99) return MOD_OK;
        ++ticks;
        if (ticks >= 1800) ok(MOD_ERROR);
        if (step >= 5 && ticks % 30 == 0) {
            char diagnostic[192]{};
            const auto value = handles[0] ? state(handles[0]) : AudioStreamState{};
            std::snprintf(diagnostic, sizeof(diagnostic), "audio_test: step %u case %u written %u phase %u position %llu buffered %u",
                step, testCase, written, unsigned(value.phase), static_cast<unsigned long long>(value.position_frames), value.buffered_frames);
            mark(diagnostic);
        }
        switch (step) {
        case 0: {
            auto desc = *svc_audio->default_stream_desc;
            desc.volume = 0;
            desc.duck_bgm = false;
            desc.stop_on_scene_change = false;
            const auto result = svc_audio->open(mod_ctx, &desc, &handles[0]);
            if (result == MOD_UNAVAILABLE) return MOD_OK;
            ok(result);
            for (size_t i = 1; i < handles.size(); ++i) handles[i] = open_stream(i == 1 ? 44100 : 32000);
            AudioStreamHandle extra{};
            require(svc_audio->open(mod_ctx, nullptr, &extra) == MOD_UNAVAILABLE && extra == 0, "four-stream limit failed");
            uint32_t accepted{1};
            require(svc_audio->write(mod_ctx, handles[0], nullptr, 1, &accepted) == MOD_INVALID_ARGUMENT && accepted == 0,
                "invalid write accepted");
            mark("audio_test: four-stream limit and argument validation passed");
            ++step;
            break;
        }
        case 1: {
            bool prepared = true;
            for (auto handle : handles) {
                feed(handle);
                const auto value = state(handle);
                require(value.position_frames == 0, "locked stream advanced");
                prepared &= value.phase == AUDIO_STREAM_PREPARED;
            }
            if (prepared && ++waitTicks > 15) {
                uint32_t free{};
                ok(svc_audio->free_frames(mod_ctx, handles[0], &free));
                require(free == 0, "full stream did not apply backpressure");
                ok(svc_audio->set_pitch(mod_ctx, handles[0], 1.0f));
                ok(svc_audio->set_volume(mod_ctx, handles[0], 0.0f, 3200));
                ok(svc_audio->play(mod_ctx, handles[0], 0));
                mark("audio_test: prefill, resampling, lock and backpressure passed");
                ++step;
            }
            break;
        }
        case 2:
            if (state(handles[0]).underrun_count) {
                require(state(handles[0]).phase == AUDIO_STREAM_PAUSED, "underrun did not pause");
                pausedPosition = state(handles[0]).position_frames;
                waitTicks = 0;
                ++step;
            }
            break;
        case 3:
            if (++waitTicks < 12) {
                const auto position = state(handles[0]).position_frames;
                if (waitTicks <= 2) {
                    require(position <= pausedPosition + 160, "underrun pause exceeded two subframes");
                    pausedPosition = position;
                } else require(position == pausedPosition, "starved stream replayed data");
            } else {
                feed(handles[0]);
                if (state(handles[0]).position_frames > pausedPosition + 1000) {
                    ok(svc_audio->pause(mod_ctx, handles[0], 3200));
                    waitTicks = 0;
                    ++step;
                }
            }
            break;
        case 4:
            feed(handles[0]);
            if (++waitTicks > 15 && state(handles[0]).phase == AUDIO_STREAM_PAUSED) {
                ok(svc_audio->play(mod_ctx, handles[1], 0));
                ok(svc_audio->resume(mod_ctx, handles[0], 3200));
                mark("audio_test: underrun recovery and prepared phase switching passed");
                for (auto& handle : handles) {
                    const auto stale = handle;
                    ok(svc_audio->close(mod_ctx, handle));
                    AudioStreamState value{};
                    require(svc_audio->get_state(mod_ctx, stale, &value) == MOD_INVALID_ARGUMENT, "stale handle survived close");
                    handle = 0;
                }
                waitTicks = 0;
                ++step;
            }
            break;
        case 5:
            if (++waitTicks > 15) {
                if (!handles[0]) {
                    handles[0] = open_stream(testCase == 6 ? 48000 : testCase == 7 ? 44100 : 32000, testCase >= 6);
                    if (lengths[testCase] > 10081) ok(svc_audio->play(mod_ctx, handles[0], 0));
                }
                written += feed(handles[0], lengths[testCase] - written);
                if (written != lengths[testCase]) break;
                written = 0;
                ok(svc_audio->end_of_stream(mod_ctx, handles[0]));
                ok(svc_audio->play(mod_ctx, handles[0], 0));
                ++step;
            }
            break;
        case 6:
            if (state(handles[0]).phase == AUDIO_STREAM_ENDED) {
                require(state(handles[0]).position_frames == (testCase >= 6 ? 32000 : lengths[testCase]), "end position differs from input length");
                ok(svc_audio->close(mod_ctx, handles[0]));
                handles[0] = 0;
                if (++testCase == lengths.size()) {
                    mark("audio_test: short, block-aligned, running and resampled endings passed");
                    step = 7;
                } else step = 5;
                waitTicks = 0;
            }
            break;
        case 7:
            if (++waitTicks > 15) {
                handles[0] = open_stream();
                ok(svc_audio->close(mod_ctx, handles[0]));
                handles[0] = 0;
                mark("audio_test: cancellation during opening passed; ALL TESTS PASSED");
                step = 8;
                waitTicks = 0;
            }
            break;
        case 8:
            if (++waitTicks > 15) {
                for (auto& handle : handles) {
                    handle = open_stream();
                    feed(handle);
                }
                step = 99;
                svc_host->fail(mod_ctx, MOD_ERROR, "audio_test: intentional detach with four pending streams");
            }
            break;
        }
        return MOD_OK;
    } catch (const std::exception& exception) {
        svc_log->error(mod_ctx, exception.what());
        std::snprintf(error->message, sizeof(error->message), "%s", exception.what());
        return MOD_ERROR;
    }
}
MOD_EXPORT ModResult mod_shutdown(ModError*) {
    // Intentionally leave surviving handles for the service's detach cleanup.
    handles = {};
    return MOD_OK;
}
}
