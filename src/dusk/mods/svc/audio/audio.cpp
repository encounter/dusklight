#include "mods/svc/audio.h"
#include <algorithm>
#include <borealis/log.hpp>
#include <cmath>
#include <fmt/format.h>
#include "../audio_res/audio_res.hpp"
#include "../audio_res/bst.hpp"
#include "../internal.hpp"
#include "../registry.hpp"
#include "JSystem/JAudio2/JAIStream.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
#include "JSystem/JAudio2/JASPCMStream.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "audio.hpp"
#include "dusk/mods/log_buffer.hpp"
#include "m_Do/m_Do_audio.h"

namespace dusk::mods::svc::audio {
namespace {
constexpr borealis::Log Log{"dusk::mods::audio"};
using Error = JASPCMStream::Error;
using Phase = JASPCMStream::Phase;

struct Stream {
    std::shared_ptr<JASPCMStream> pcm{};
    JAISoundHandle sound{};
    AudioSoundTableHandle table = 0;
    ModContext* context = nullptr;
    AudioStreamDesc desc = AUDIO_STREAM_DESC_INIT;
    uint32_t fadeIn = 0;
    bool playRequested = false;
    bool pauseRequested = false;
    bool stopped = false;
    bool ducking = false;
    bool errorReported = false;

    ~Stream() {
        if (table) {
            audio_res::bst::remove_sound_table(context, table);
        }
        if (sound) {
            sound->stop();
        }
        if (pcm) {
            pcm->cancel();
        }
    }
};

SlotMap<std::unique_ptr<Stream>> streams;
// Retain retired objects until JAI, registration metadata and public handles release them.
// Destruction, including the converter, consequently happens on the game thread.
std::array<std::shared_ptr<JASPCMStream>, 4> allocations{};
uint32_t duckCount = 0;

void reap_retired() {
    for (auto& pcm : allocations) {
        if (pcm && pcm.use_count() == 1 && pcm->getState().retired) {
            pcm.reset();
        }
    }
}

uint32_t fade_ticks(uint32_t milliseconds) {
    return static_cast<uint32_t>((uint64_t{milliseconds} * 30 + 999) / 1000);
}

ModResult result_for(Error error) {
    switch (error) {
    case Error::NONE:
        return MOD_OK;
    case Error::INVALID_ARGUMENT:
        return MOD_INVALID_ARGUMENT;
    case Error::INPUT_CLOSED:
    case Error::ALREADY_ATTACHED:
        return MOD_CONFLICT;
    case Error::OUT_OF_MEMORY:
    case Error::CHANNELS_UNAVAILABLE:
        return MOD_UNAVAILABLE;
    default:
        return MOD_ERROR;
    }
}

AudioStreamError public_error(Error error) {
    switch (error) {
    case Error::NONE:
        return AUDIO_STREAM_ERROR_NONE;
    case Error::INVALID_PITCH:
        return AUDIO_STREAM_ERROR_INVALID_PITCH;
    case Error::CHANNELS_UNAVAILABLE:
        return AUDIO_STREAM_ERROR_CHANNELS_UNAVAILABLE;
    case Error::CHANNEL_LOST:
        return AUDIO_STREAM_ERROR_CHANNEL_LOST;
    default:
        return AUDIO_STREAM_ERROR_RESAMPLER;
    }
}

void report_error(Stream& stream, const JASPCMStream::State& state) {
    if (state.error != Error::NONE && !stream.errorReported) {
        stream.errorReported = true;
        log::emit(log::Source::Mod, mod_id_from_context(stream.context), LOG_LEVEL_ERROR,
            fmt::format("PCM playback failed (error {}, resampler {})",
                static_cast<int>(state.error), state.resamplerError));
    }
}

Stream* get_stream(ModContext* context, AudioStreamHandle handle) {
    auto* mod = mod_from_context(context);
    auto* entry = mod ? streams.find_owned(handle, *mod) : nullptr;
    return entry ? entry->value.get() : nullptr;
}

ModResult controllable(const Stream& stream) {
    const auto state = stream.pcm->getState();
    if (state.error != Error::NONE) {
        return MOD_ERROR;
    }
    if (stream.stopped || state.phase == Phase::ENDED || state.phase == Phase::STOPPING ||
        !stream.sound)
    {
        return MOD_CONFLICT;
    }
    return MOD_OK;
}

void set_ducking(Stream& stream, bool enabled) {
    enabled = enabled && stream.desc.duck_bgm;
    if (stream.ducking == enabled) {
        return;
    }
    stream.ducking = enabled;
    if (enabled) {
        if (++duckCount == 1) {
            Z2GetSeqMgr()->mStreamBgmMaster.fadeOut(15);
        }
    } else if (--duckCount == 0 && !Z2GetSeqMgr()->mStreamBgmHandle) {
        Z2GetSeqMgr()->mStreamBgmMaster.fadeIn(15);
    }
}

ModResult open(ModContext* context, const AudioStreamDesc* desc, AudioStreamHandle* outHandle) {
    if (outHandle) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (!mod || !outHandle) {
        return MOD_INVALID_ARGUMENT;
    }
    const AudioStreamDesc defaults = AUDIO_STREAM_DESC_INIT;
    if (!desc) {
        desc = &defaults;
    }
    if (desc->struct_size < sizeof(AudioStreamDesc) ||
        (desc->format != AUDIO_FORMAT_S16 && desc->format != AUDIO_FORMAT_F32) ||
        !std::isfinite(desc->volume) || desc->volume < 0 || desc->volume > 2 ||
        !std::isfinite(desc->pitch) || desc->pitch < 1.0f / 32 || desc->pitch > 4)
    {
        return MOD_INVALID_ARGUMENT;
    }
    if (!mDoAud_zelAudio_c::isInitFlag()) {
        return MOD_UNAVAILABLE;
    }
    {
        JASCriticalSection lock;
        reap_retired();
    }
    const auto allocation = std::find(allocations.begin(), allocations.end(), nullptr);
    if (allocation == allocations.end()) {
        return MOD_UNAVAILABLE;
    }
    // Allocate and touch native PCM/converter memory outside the audio lock.
    auto created =
        JASPCMStream::create({desc->format == AUDIO_FORMAT_S16 ? JASPCMStream::SampleFormat::S16 :
                                                                 JASPCMStream::SampleFormat::F32,
            desc->sample_rate, desc->channels, desc->capacity_frames, desc->prepare_frames});
    if (!created) {
        return result_for(created.error().code);
    }
    // Keep rollback destruction under the same lock as attachment.
    JASCriticalSection lock;
    auto stream = std::make_unique<Stream>();
    stream->context = context;
    stream->desc = *desc;
    stream->pcm = *created;
    JASPCMStream::Params initial{};
    initial.pitch = desc->pitch;
    stream->pcm->setParams(initial);
    *allocation = stream->pcm;
    auto info = audio_res::bst::default_stream_info;
    info.volume = 1;
    info.stop_on_scene_change = desc->stop_on_scene_change;
    if (desc->channels == 1) {
        info.pan_parameters[0] = STREAM_PAN_CENTER;
    }
    uint16_t soundId = 0;
    auto result = audio_res::bst::add_pcm_stream(
        context, stream->pcm, info, desc->volume, desc->pitch, &stream->table, &soundId);
    if (result != MOD_OK) {
        return result;
    }
    audio_res::bst::sync_audio_replacements();
    Z2GetSoundMgr()->startSound(JAISoundID{2, 0, soundId}, &stream->sound, nullptr);
    if (!stream->sound) {
        return MOD_UNAVAILABLE;
    }
    stream->sound->lockWhenPrepared();
    *outHandle = streams.emplace(*mod, std::move(stream));
    return MOD_OK;
}

ModResult write(ModContext* context, AudioStreamHandle handle, const void* frames, uint32_t count,
    uint32_t* accepted) {
    if (accepted) {
        *accepted = 0;
    }
    auto* stream = get_stream(context, handle);
    if (!stream || !accepted || (!frames && count)) {
        return MOD_INVALID_ARGUMENT;
    }
    // The service registry is game-thread-owned; producer copies do not take the audio lock.
    const auto result = stream->pcm->write(frames, count);
    *accepted = result.acceptedFrames;
    return result_for(result.error);
}

ModResult get_writable_frames(ModContext* context, AudioStreamHandle handle, uint32_t* outFrames) {
    if (outFrames) {
        *outFrames = 0;
    }
    auto* stream = get_stream(context, handle);
    if (!stream || !outFrames) {
        return MOD_INVALID_ARGUMENT;
    }
    *outFrames = stream->pcm->getWritableFrames();
    return MOD_OK;
}

ModResult end_of_stream(ModContext* context, AudioStreamHandle handle) {
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    return result_for(stream->pcm->endOfStream());
}

ModResult play(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = controllable(*stream);
    if (result != MOD_OK) {
        return result;
    }
    if (stream->sound->status_.state.unk == 5 && !stream->pauseRequested) {
        return MOD_OK;
    }
    stream->pauseRequested = false;
    stream->fadeIn = fade_ticks(fade);
    if (stream->sound->status_.state.unk == 5) {
        stream->sound->pause(false);
        stream->sound->getFader()->fadeIn(stream->fadeIn);
    } else {
        stream->playRequested = true;
    }
    return MOD_OK;
}

ModResult pause(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = controllable(*stream);
    if (result != MOD_OK) {
        return result;
    }
    stream->playRequested = false;
    if (stream->sound->status_.state.unk != 5) {
        if (stream->sound->status_.state.unk == 4) {
            stream->sound->status_.state.unk = 3;
        } else {
            stream->sound->lockWhenPrepared();
        }
        return MOD_OK;
    }
    if (stream->pauseRequested) {
        return MOD_OK;
    }
    stream->pauseRequested = true;
    stream->sound->fadeOut(fade_ticks(fade));
    if (!fade) {
        stream->sound->pause(true);
    }
    return MOD_OK;
}

ModResult stop(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    if (stream->stopped || stream->pcm->getState().phase == Phase::ENDED) {
        return MOD_OK;
    }
    stream->stopped = true;
    stream->playRequested = false;
    stream->pauseRequested = false;
    stream->pcm->endOfStream();
    if (stream->sound) {
        if (stream->sound->status_.state.unk != 5 || stream->sound->isPaused()) {
            stream->pcm->cancel();
            stream->sound->stop();
        } else {
            stream->sound->stop(fade_ticks(fade));
        }
    } else {
        stream->pcm->cancel();
    }
    return MOD_OK;
}

ModResult set_volume(ModContext* context, AudioStreamHandle handle, float volume, uint32_t ramp) {
    if (!std::isfinite(volume) || volume < 0 || volume > 2) {
        return MOD_INVALID_ARGUMENT;
    }
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = controllable(*stream);
    if (result != MOD_OK) {
        return result;
    }
    stream->sound->getAuxiliary().moveVolume(volume, fade_ticks(ramp));
    return MOD_OK;
}

ModResult set_pitch(ModContext* context, AudioStreamHandle handle, float pitch, uint32_t ramp) {
    if (!std::isfinite(pitch) || pitch < 1.0f / 32 || pitch > 4) {
        return MOD_INVALID_ARGUMENT;
    }
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = controllable(*stream);
    if (result != MOD_OK) {
        return result;
    }
    stream->sound->getAuxiliary().movePitch(pitch, fade_ticks(ramp));
    return MOD_OK;
}

ModResult get_state(ModContext* context, AudioStreamHandle handle, AudioStreamState* state) {
    if (!state || state->struct_size < sizeof(AudioStreamState)) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto size = state->struct_size;
    *state = AUDIO_STREAM_STATE_INIT;
    state->struct_size = size;
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto value = stream->pcm->getState();
    state->phase = static_cast<AudioStreamPhase>(value.phase);
    if (stream->stopped && !value.retired) {
        state->phase = AUDIO_STREAM_STOPPING;
    }
    state->error = public_error(value.error);
    state->capacity_frames = value.capacityFrames;
    state->position_frames = value.positionFrames;
    state->buffered_frames = value.bufferedFrames;
    state->underrun_count = value.underrunCount;
    state->effective_pitch = value.effectivePitch;
    state->pitch_limited = value.pitchLimited;
    state->input_closed = value.inputClosed;
    state->starved = value.starved;
    return MOD_OK;
}

ModResult close(ModContext* context, AudioStreamHandle handle) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    report_error(*stream, stream->pcm->getState());
    set_ducking(*stream, false);
    streams.erase(handle);
    return MOD_OK;
}

void frame_end() {
    JASCriticalSection lock;
    streams.for_each([](auto, auto& entry) {
        auto& stream = *entry.value;
        const auto state = stream.pcm->getState();
        report_error(stream, state);
        if (stream.sound && !stream.stopped) {
            if (stream.playRequested && stream.sound->isPrepared()) {
                stream.sound->unlockIfLocked();
                stream.sound->fadeIn(stream.fadeIn);
                stream.playRequested = false;
            }
            if (stream.pauseRequested && stream.sound->getFader()->isOut()) {
                stream.sound->pause(true);
            }
        }
        set_ducking(stream, !state.retired && state.phase != Phase::ENDED &&
                                (state.phase == Phase::PLAYING || state.phase == Phase::STOPPING));
    });
    reap_retired();
    if (duckCount && Z2GetSeqMgr()->mStreamBgmMaster.getDest() != 0.0f) {
        Z2GetSeqMgr()->mStreamBgmMaster.fadeOut(15);
    }
}

void shutdown() {
    JASCriticalSection lock;
    for (auto& pcm : allocations) {
        if (pcm) {
            pcm->cancel();
        }
    }
    // The loader has detached every owner. Remove lookup and JAI references before pool teardown.
    audio_res::bst::sync_audio_replacements();
    if (mDoAud_zelAudio_c::isInitFlag()) {
        Z2GetSoundMgr()->getStreamMgr()->calc();
    }
    reap_retired();
}

void mod_detached(LoadedMod& mod) {
    JASCriticalSection lock;
    auto entries = streams.take_all(mod);
    for (auto& entry : entries) {
        report_error(*entry.value, entry.value->pcm->getState());
        set_ducking(*entry.value, false);
    }
    if (!entries.empty()) {
        Log.warn("[{}] reclaimed {} open audio stream(s)", mod.metadata.id, entries.size());
    }
}
}  // namespace

bool is_ducking() {
    return duckCount != 0;
}
}  // namespace dusk::mods::svc::audio

namespace dusk::mods::svc {
namespace {
constexpr AudioService service{
    .header = SERVICE_HEADER(AudioService, AUDIO_SERVICE_MAJOR, AUDIO_SERVICE_MINOR),
    .open = SERVICE_FUNCTION(audio::open),
    .write = SERVICE_FUNCTION(audio::write),
    .get_writable_frames = SERVICE_FUNCTION(audio::get_writable_frames),
    .end_of_stream = SERVICE_FUNCTION(audio::end_of_stream),
    .play = SERVICE_FUNCTION(audio::play),
    .pause = SERVICE_FUNCTION(audio::pause),
    .stop = SERVICE_FUNCTION(audio::stop),
    .set_volume = SERVICE_FUNCTION(audio::set_volume),
    .set_pitch = SERVICE_FUNCTION(audio::set_pitch),
    .get_state = SERVICE_FUNCTION(audio::get_state),
    .close = SERVICE_FUNCTION(audio::close),
};
}

constinit const ServiceModule g_audioModule{
    .id = AUDIO_SERVICE_ID,
    .majorVersion = AUDIO_SERVICE_MAJOR,
    .minorVersion = AUDIO_SERVICE_MINOR,
    .service = &service,
    .modDetached = audio::mod_detached,
    .frameEnd = audio::frame_end,
    .shutdown = audio::shutdown,
};
}  // namespace dusk::mods::svc
