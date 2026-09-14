#pragma once

#include <mods/api.h>

#ifdef __cplusplus
#include <mods/service.hpp>
#endif

#define AUDIO_SERVICE_ID DUSKLIGHT_SERVICE_ID_PREFIX "audio"
#define AUDIO_SERVICE_MAJOR 1u
#define AUDIO_SERVICE_MINOR 0u

typedef uint64_t AudioStreamHandle;

typedef enum AudioSampleFormat {
    AUDIO_FORMAT_S16 = 0,
    AUDIO_FORMAT_F32 = 1,
} AudioSampleFormat;

typedef enum AudioStreamPhase {
    AUDIO_STREAM_PREPARING = 0,
    AUDIO_STREAM_PREPARED,
    AUDIO_STREAM_PLAYING,
    AUDIO_STREAM_PAUSED,
    AUDIO_STREAM_STOPPING,
    AUDIO_STREAM_ENDED,
} AudioStreamPhase;

typedef enum AudioStreamError {
    AUDIO_STREAM_ERROR_NONE = 0,
    AUDIO_STREAM_ERROR_RESAMPLER,
    AUDIO_STREAM_ERROR_INVALID_PITCH,
    AUDIO_STREAM_ERROR_CHANNELS_UNAVAILABLE,
    AUDIO_STREAM_ERROR_CHANNEL_LOST,
} AudioStreamError;

typedef struct AudioStreamDesc {
    uint32_t struct_size;
    AudioSampleFormat format;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t capacity_frames;
    uint32_t prepare_frames;
    float volume;
    float pitch;
    bool stop_on_scene_change;
    bool duck_bgm;
} AudioStreamDesc;

#define AUDIO_STREAM_DESC_INIT                                                                     \
    {sizeof(AudioStreamDesc), AUDIO_FORMAT_F32, 48000u, 2u, 0u, 0u, 1.0f, 1.0f, true, true}

typedef struct AudioStreamState {
    uint32_t struct_size;
    AudioStreamPhase phase;
    AudioStreamError error;
    uint32_t capacity_frames;
    uint64_t position_frames;
    uint64_t buffered_frames;
    uint64_t underrun_count;
    float effective_pitch;
    bool pitch_limited;
    bool input_closed;
    bool starved;
} AudioStreamState;

#define AUDIO_STREAM_STATE_INIT                                                                    \
    {sizeof(AudioStreamState), AUDIO_STREAM_PREPARING, AUDIO_STREAM_ERROR_NONE, 0u, 0u, 0u, 0u,    \
        1.0f, false, false, false}

/* Game thread only; handles belong to their opener. Four active plus retiring allocations.
 * PCM is native-endian and interleaved. All frame counts use the immutable source rate.
 * Fade/ramp durations are milliseconds, rounded up to 30 Hz JAI control ticks.
 * NULL open descriptor uses AUDIO_STREAM_DESC_INIT; buffering defaults to 500/100 ms.
 * Volume is finite in [0, 2], pitch in [1/32, 4]. Stop is terminal; play also resumes.
 * write copies a possibly partial prefix; retry unaccepted frames. EOF seals input without play.
 * get_state preserves struct_size and unknown tail bytes. Initialize sized structs with _INIT.
 */
typedef struct AudioService {
    ServiceHeader header;

    ModResult (*open)(ModContext* ctx, const AudioStreamDesc* desc, AudioStreamHandle* out_stream);
    ModResult (*write)(ModContext* ctx, AudioStreamHandle stream, const void* frames,
        uint32_t frame_count, uint32_t* out_accepted);
    ModResult (*get_writable_frames)(
        ModContext* ctx, AudioStreamHandle stream, uint32_t* out_frames);
    ModResult (*end_of_stream)(ModContext* ctx, AudioStreamHandle stream);

    ModResult (*play)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_in_ms);
    ModResult (*pause)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_out_ms);
    ModResult (*stop)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_out_ms);
    ModResult (*set_volume)(
        ModContext* ctx, AudioStreamHandle stream, float volume, uint32_t ramp_ms);
    ModResult (*set_pitch)(
        ModContext* ctx, AudioStreamHandle stream, float pitch, uint32_t ramp_ms);

    ModResult (*get_state)(ModContext* ctx, AudioStreamHandle stream, AudioStreamState* out_state);
    ModResult (*close)(ModContext* ctx, AudioStreamHandle stream);
} AudioService;

MOD_DECLARE_SERVICE(
    AudioService, svc_audio, AUDIO_SERVICE_ID, AUDIO_SERVICE_MAJOR, AUDIO_SERVICE_MINOR);
