#include "mods/svc/audio.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <algorithm>
#include <borealis/log.hpp>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fmt/format.h>
#include <limits>
#include <string>
#include "../audio_res/audio_res.hpp"
#include "../internal.hpp"
#include "../registry.hpp"
#include "JSystem/JAudio2/JAIStream.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "dusk/mods/log_buffer.hpp"
#include "m_Do/m_Do_audio.h"
#include "source.hpp"

namespace dusk::mods::svc::audio {
namespace {
constexpr borealis::Log Log{"dusk::mods::audio"};

struct Stream {
    std::shared_ptr<Source> source{};
    SDL_AudioStream* converter{};
    JAISoundHandle sound{};
    AudioSoundTableHandle table{};
    ModContext* context{};
    AudioStreamDesc desc = AUDIO_STREAM_DESC_INIT;
    int entry{};
    uint32_t partial{}, fadeIn{};
    uint64_t outputFrames{};
    bool eof{}, playRequested{}, pauseRequested{}, stopped{}, ducking{};
    std::array<int16_t, blockFrames * 2> scratch{};

    ~Stream() {
        if (table) {
            audio_res::bst::remove_sound_table(context, table);
        }
        if (sound) {
            sound->stop();
        }
        SDL_DestroyAudioStream(converter);
    }
};

SlotMap<std::unique_ptr<Stream>> streams;

struct Registration {
    int entry{};
    std::weak_ptr<Source> source{};
};

std::array<Registration, 4> sources{};
int nextEntry = entryBase;
uint32_t duckCount{};

void report_sdl_error(ModContext* context, const char* operation) {
    log::emit(log::Source::Mod, mod_id_from_context(context), LOG_LEVEL_ERROR,
        fmt::format("{}: {}", operation, SDL_GetError()));
}

uint32_t fade_ticks(uint32_t frames) {
    return static_cast<uint32_t>((uint64_t{frames} * 30 + outputRate - 1) / outputRate);
}

Stream* get_stream(ModContext* context, AudioStreamHandle handle) {
    auto* mod = mod_from_context(context);
    auto* entry = mod ? streams.find_owned(handle, *mod) : nullptr;
    return entry ? entry->value.get() : nullptr;
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

bool drain(Stream& stream) {
    if (stream.stopped || !stream.sound) {
        return true;
    }
    auto& source = *stream.source;
    const auto channels = source.channels;
    auto produced = source.produced.load(std::memory_order_relaxed);
    while (produced - source.consumed.load(std::memory_order_acquire) < source.blocks.size()) {
        const int available = SDL_GetAudioStreamAvailable(stream.converter);
        if (available < 0) {
            report_sdl_error(stream.context, "query converted audio");
            return false;
        }
        const auto count = std::min<uint32_t>(
            available / (channels * sizeof(int16_t)), blockFrames - stream.partial);
        if (count == 0) {
            break;
        }
        const int bytes = SDL_GetAudioStreamData(
            stream.converter, stream.scratch.data(), count * channels * sizeof(int16_t));
        if (bytes < 0) {
            report_sdl_error(stream.context, "convert audio");
            return false;
        }
        if (bytes == 0) {
            break;
        }
        const uint32_t frames = bytes / (channels * sizeof(int16_t));
        auto& block = source.blocks[produced % source.blocks.size()];
        for (uint32_t frame = 0; frame < frames; ++frame) {
            for (uint32_t channel = 0; channel < channels; ++channel) {
                const uint16_t sample = stream.scratch[frame * channels + channel];
                const auto offset = (channel * blockFrames + stream.partial + frame) * 2;
                block.pcm[offset] = sample >> 8;
                block.pcm[offset + 1] = sample & 0xff;
            }
        }
        stream.partial += frames;
        stream.outputFrames += frames;
        if (stream.partial == blockFrames) {
            stream.partial = 0;
            source.produced.store(++produced, std::memory_order_release);
        }
    }
    if (!stream.eof) {
        return true;
    }
    const int available = SDL_GetAudioStreamAvailable(stream.converter);
    if (available < 0) {
        report_sdl_error(stream.context, "query final audio");
        return false;
    }
    if (available == 0) {
        if (stream.partial != 0) {
            auto& block = source.blocks[produced % source.blocks.size()];
            for (uint32_t channel = 0; channel < channels; ++channel) {
                std::memset(block.pcm.data() + (channel * blockFrames + stream.partial) * 2, 0,
                    (blockFrames - stream.partial) * 2);
            }
            stream.partial = 0;
            source.produced.store(produced + 1, std::memory_order_release);
        }
        source.endFrame = static_cast<uint32_t>(stream.outputFrames);
        source.finished = true;
        if (stream.outputFrames == 0 && stream.sound) {
            stream.sound->stop();
            stream.stopped = true;
        }
    }
    return true;
}

ModResult writable_frames(Stream& stream, uint32_t& outFrames) {
    outFrames = 0;
    if (stream.eof || stream.stopped || !stream.sound) {
        return MOD_OK;
    }
    auto& source = *stream.source;
    const uint32_t staged = source.produced.load() - source.consumed.load();
    const uint64_t capacity = (source.blocks.size() - staged) * blockFrames - stream.partial;
    const int queued = SDL_GetAudioStreamQueued(stream.converter);
    if (queued < 0) {
        report_sdl_error(stream.context, "query queued audio");
        return MOD_ERROR;
    }
    const uint64_t budget = capacity * stream.desc.sample_rate / outputRate;
    const uint32_t sampleBytes = stream.desc.format == AUDIO_FORMAT_S16 ? 2 : 4;
    const uint64_t pending = queued / (stream.desc.channels * sampleBytes);
    // Bound the open-ended stream to JAS's signed sample range. Reopen for longer playback.
    const uint64_t remaining =
        stream.outputFrames < 0x7fff0000u ?
            (0x7fff0000u - stream.outputFrames) * stream.desc.sample_rate / outputRate :
            0;
    const uint64_t limit = std::min(budget, remaining);
    outFrames = static_cast<uint32_t>(limit > pending ? limit - pending : 0);
    return MOD_OK;
}

ModResult open(ModContext* context, const AudioStreamDesc* desc, AudioStreamHandle* outHandle) {
    if (outHandle) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (!mod || !outHandle) {
        return MOD_INVALID_ARGUMENT;
    }
    if (!desc || desc->struct_size < sizeof(AudioStreamDesc) || desc->source != AUDIO_SOURCE_PCM ||
        (desc->format != AUDIO_FORMAT_S16 && desc->format != AUDIO_FORMAT_F32) ||
        desc->sample_rate < 8000 || desc->sample_rate > 192000 || desc->channels < 1 ||
        desc->channels > 2 || !std::isfinite(desc->volume) || desc->volume < 0 || desc->volume > 2)
    {
        return MOD_INVALID_ARGUMENT;
    }
    JASCriticalSection lock;
    if (!mDoAud_zelAudio_c::isInitFlag() || !JASKernel::getAramHeap()) {
        return MOD_UNAVAILABLE;
    }
    auto registration = std::find_if(
        sources.begin(), sources.end(), [](auto& value) { return value.source.expired(); });
    if (registration == sources.end() || nextEntry == std::numeric_limits<int>::max()) {
        return MOD_UNAVAILABLE;
    }
    auto stream = std::make_unique<Stream>();
    stream->context = context;
    stream->desc = *desc;
    stream->source = std::make_shared<Source>();
    auto& source = *stream->source;
    source.channels = desc->channels;
    source.ringBlocks =
        desc->ring_frames ? std::clamp(desc->ring_frames / blockFrames, 3u, 10u) : 5;
    source.prepareBlocks =
        desc->prepare_frames ?
            std::clamp<uint64_t>((uint64_t{desc->prepare_frames} + blockFrames - 1) / blockFrames,
                1, source.ringBlocks) :
            source.ringBlocks;
    if (!source.heap.alloc(JASKernel::getAramHeap(),
            (source.ringBlocks + 1) * source.channels * JASAramStream::getBlockSize()))
    {
        return MOD_UNAVAILABLE;
    }
    SDL_AudioSpec input{desc->format == AUDIO_FORMAT_S16 ? SDL_AUDIO_S16 : SDL_AUDIO_F32,
        desc->channels, int(desc->sample_rate)};
    SDL_AudioSpec output{SDL_AUDIO_S16, desc->channels, outputRate};
    stream->converter = SDL_CreateAudioStream(&input, &output);
    if (!stream->converter) {
        report_sdl_error(context, "open audio stream");
        return MOD_ERROR;
    }
    stream->entry = nextEntry++;
    *registration = {stream->entry, stream->source};
    const auto handle = streams.emplace(*mod, std::move(stream));

    struct PendingOpen {
        AudioStreamHandle handle;

        ~PendingOpen() {
            if (handle) {
                streams.erase(handle);
            }
        }
    } pending{handle};

    auto& value = *streams.find(handle)->value;
    const auto path = std::string{"dusk://audio/"} + std::to_string(handle);
    auto info = audio_res::bst::default_stream_info;
    info.volume = desc->volume;
    info.stop_on_scene_change = desc->stop_on_scene_change;
    if (desc->channels == 1) {
        info.pan_parameters[0] = STREAM_PAN_CENTER;
    }
    uint16_t soundId{};
    auto result = audio_res::bst::add_sound_table_stream(
        context, path.c_str(), &info, &value.table, &soundId);
    if (result == MOD_OK) {
        audio_res::bst::sync_audio_replacements();
        Z2GetSoundMgr()->startSound(JAISoundID{2, 0, soundId}, &value.sound, nullptr);
        if (value.sound) {
            value.sound->lockWhenPrepared();
        } else {
            result = MOD_UNAVAILABLE;
        }
    }
    if (result != MOD_OK) {
        return result;
    }
    *outHandle = handle;
    pending.handle = 0;
    return MOD_OK;
}

ModResult write(ModContext* context, AudioStreamHandle handle, const void* frames, uint32_t count,
    uint32_t* accepted) {
    if (accepted) {
        *accepted = 0;
    }
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !accepted || (!frames && count) || stream->eof || stream->stopped ||
        !stream->sound)
    {
        return MOD_INVALID_ARGUMENT;
    }
    if (!drain(*stream)) {
        return MOD_ERROR;
    }
    uint32_t capacity{};
    if (writable_frames(*stream, capacity) != MOD_OK) {
        return MOD_ERROR;
    }
    const auto take = std::min(count, capacity);
    if (take &&
        !SDL_PutAudioStreamData(stream->converter, frames,
            take * stream->desc.channels * (stream->desc.format == AUDIO_FORMAT_S16 ? 2 : 4)))
    {
        report_sdl_error(context, "write audio");
        return MOD_ERROR;
    }
    *accepted = take;
    return drain(*stream) ? MOD_OK : MOD_ERROR;
}

ModResult free_frames(ModContext* context, AudioStreamHandle handle, uint32_t* outFrames) {
    if (outFrames) {
        *outFrames = 0;
    }
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !outFrames) {
        return MOD_INVALID_ARGUMENT;
    }
    if (!drain(*stream)) {
        return MOD_ERROR;
    }
    return writable_frames(*stream, *outFrames);
}

ModResult end_of_stream(ModContext* context, AudioStreamHandle handle) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    if (stream->eof) {
        return MOD_OK;
    }
    if (stream->stopped || !stream->sound) {
        return MOD_INVALID_ARGUMENT;
    }
    if (!SDL_FlushAudioStream(stream->converter)) {
        report_sdl_error(context, "flush audio");
        return MOD_ERROR;
    }
    stream->eof = true;
    return drain(*stream) ? MOD_OK : MOD_ERROR;
}

ModResult play(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || stream->stopped || !stream->sound) {
        return MOD_INVALID_ARGUMENT;
    }
    if (stream->sound->status_.state.unk >= 4) {
        return MOD_OK;
    }
    stream->playRequested = true;
    stream->fadeIn = fade_ticks(fade);
    return MOD_OK;
}

ModResult stop(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    if (stream->stopped) {
        return MOD_OK;
    }
    stream->playRequested = false;
    stream->pauseRequested = false;
    if (stream->sound) {
        stream->sound->stop(stream->sound->isPaused() ? 0 : fade_ticks(fade));
    }
    stream->stopped = true;
    if (!stream->sound) {
        set_ducking(*stream, false);
    }
    return MOD_OK;
}

ModResult pause(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !stream->sound || stream->stopped) {
        return MOD_INVALID_ARGUMENT;
    }
    stream->pauseRequested = true;
    stream->sound->fadeOut(fade_ticks(fade));
    if (!fade) {
        stream->sound->pause(true);
    }
    return MOD_OK;
}

ModResult resume(ModContext* context, AudioStreamHandle handle, uint32_t fade) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !stream->sound || stream->stopped) {
        return MOD_INVALID_ARGUMENT;
    }
    stream->pauseRequested = false;
    stream->sound->pause(false);
    stream->sound->getFader()->fadeIn(fade_ticks(fade));
    return MOD_OK;
}

ModResult set_volume(ModContext* context, AudioStreamHandle handle, float volume, uint32_t ramp) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !stream->sound || stream->stopped || !std::isfinite(volume) || volume < 0 ||
        volume > 2)
    {
        return MOD_INVALID_ARGUMENT;
    }
    // Transfer the table's initial gain to the movable parameter before the first ramp.
    auto& property = stream->sound->getProperty();
    auto& auxiliary = stream->sound->getAuxiliary();
    if (property.mVolume != 1) {
        auxiliary.params_.mVolume *= property.mVolume;
        property.mVolume = 1;
    }
    auxiliary.moveVolume(volume, fade_ticks(ramp));
    return MOD_OK;
}

ModResult set_pitch(ModContext* context, AudioStreamHandle handle, float ratio) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream || !stream->sound || stream->stopped || !std::isfinite(ratio) || ratio <= 0 ||
        ratio > 4)
    {
        return MOD_INVALID_ARGUMENT;
    }
    stream->sound->getAuxiliary().movePitch(ratio, 0);
    return MOD_OK;
}

ModResult get_state(ModContext* context, AudioStreamHandle handle, AudioStreamState* state) {
    if (!state || state->struct_size < sizeof(AudioStreamState)) {
        return MOD_INVALID_ARGUMENT;
    }
    const uint32_t structSize = state->struct_size;
    *state = AudioStreamState{.struct_size = structSize};
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    const int queued = SDL_GetAudioStreamQueued(stream->converter);
    if (queued < 0) {
        report_sdl_error(context, "get audio state");
        return MOD_ERROR;
    }
    auto& source = *stream->source;
    const auto position = source.position.load();
    state->position_frames =
        source.retired && source.finished && !source.stopped && !stream->stopped ?
            source.endFrame.load() :
            position;
    state->underrun_count = source.underruns;
    state->buffered_frames = stream->outputFrames > state->position_frames ?
                                 stream->outputFrames - state->position_frames :
                                 0;
    if (queued > 0) {
        state->buffered_frames += uint64_t{static_cast<uint32_t>(queued)} * outputRate /
                                  (stream->desc.sample_rate * stream->desc.channels *
                                      (stream->desc.format == AUDIO_FORMAT_S16 ? 2 : 4));
    }
    if (!stream->sound || source.retired) {
        state->phase = AUDIO_STREAM_ENDED;
    } else if (stream->sound->isPaused() || source.starved) {
        state->phase = AUDIO_STREAM_PAUSED;
    } else if (stream->sound->status_.state.unk == 5) {
        state->phase = AUDIO_STREAM_PLAYING;
    } else if (stream->sound->isPrepared()) {
        state->phase = AUDIO_STREAM_PREPARED;
    } else {
        state->phase = AUDIO_STREAM_OPENING;
    }
    return MOD_OK;
}

ModResult close(ModContext* context, AudioStreamHandle handle) {
    JASCriticalSection lock;
    auto* stream = get_stream(context, handle);
    if (!stream) {
        return MOD_INVALID_ARGUMENT;
    }
    set_ducking(*stream, false);
    streams.erase(handle);
    return MOD_OK;
}

void frame_end() {
    JASCriticalSection lock;
    streams.for_each([](auto, auto& entry) {
        auto& stream = *entry.value;
        if (!stream.stopped && stream.sound && !drain(stream)) {
            stream.sound->stop();
            stream.stopped = true;
        }
        // Draining an empty final buffer or stopping after an error releases the sound handle.
        if (!stream.sound) {
            set_ducking(stream, false);
            return;
        }
        if (!stream.stopped && stream.playRequested && stream.sound->isPrepared()) {
            stream.sound->unlockIfLocked();
            stream.sound->fadeIn(stream.fadeIn);
            stream.playRequested = false;
        }
        if (!stream.stopped && stream.pauseRequested && stream.sound->getFader()->isOut()) {
            stream.sound->pause(true);
        }
        set_ducking(stream, stream.sound->status_.state.unk >= 4 && !stream.sound->isPaused());
    });
    if (duckCount && Z2GetSeqMgr()->mStreamBgmMaster.getDest() != 0.0f) {
        Z2GetSeqMgr()->mStreamBgmMaster.fadeOut(15);
    }
}

void mod_detached(LoadedMod& mod) {
    JASCriticalSection lock;
    auto entries = streams.take_all(mod);
    for (auto& entry : entries) {
        set_ducking(*entry.value, false);
    }
    if (!entries.empty()) {
        Log.warn("[{}] reclaimed {} open audio stream(s)", mod.metadata.id, entries.size());
    }
    // Destroy handles while the owner's context and AudioRes registry are still available.
}
}  // namespace

std::shared_ptr<Source> find_source(int entry) {
    JASCriticalSection lock;
    for (auto& registration : sources) {
        if (registration.entry == entry) {
            return registration.source.lock();
        }
    }
    return {};
}

int resolve_path(const char* path) {
    if (!path) {
        return -1;
    }
    constexpr std::string_view prefix{"dusk://audio/"};
    const std::string_view text{path};
    if (!text.starts_with(prefix)) {
        return -1;
    }
    AudioStreamHandle handle{};
    const auto result =
        std::from_chars(text.data() + prefix.size(), text.data() + text.size(), handle);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return -1;
    }
    JASCriticalSection lock;
    auto* entry = streams.find(handle);
    return entry ? entry->value->entry : -1;
}

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
    .free_frames = SERVICE_FUNCTION(audio::free_frames),
    .end_of_stream = SERVICE_FUNCTION(audio::end_of_stream),
    .play = SERVICE_FUNCTION(audio::play),
    .stop = SERVICE_FUNCTION(audio::stop),
    .pause = SERVICE_FUNCTION(audio::pause),
    .resume = SERVICE_FUNCTION(audio::resume),
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
};
}  // namespace dusk::mods::svc
