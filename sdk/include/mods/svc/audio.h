#pragma once

#include <mods/api.h>

#ifdef __cplusplus
#include <mods/service.hpp>
#endif

#define AUDIO_SERVICE_ID DUSKLIGHT_SERVICE_ID_PREFIX "audio"
#define AUDIO_SERVICE_MAJOR 1u
#define AUDIO_SERVICE_MINOR 0u

/* Owned by the opening mod. 0 is never a valid handle. */
typedef uint64_t AudioStreamHandle;

typedef enum AudioSourceKind {
    AUDIO_SOURCE_PCM = 0,
} AudioSourceKind;

typedef enum AudioSampleFormat {
    AUDIO_FORMAT_S16 = 0,
    AUDIO_FORMAT_F32 = 1,
} AudioSampleFormat;

typedef enum AudioStreamPhase {
    AUDIO_STREAM_OPENING = 0,
    AUDIO_STREAM_PREPARED = 1,
    AUDIO_STREAM_PLAYING = 2,
    AUDIO_STREAM_PAUSED = 3,
    AUDIO_STREAM_ENDED = 4,
} AudioStreamPhase;

/* Initialize with AUDIO_STREAM_DESC_INIT. */
typedef struct AudioStreamDesc {
    uint32_t struct_size;
    AudioSourceKind source;
    AudioSampleFormat format;
    uint32_t sample_rate; /* Source frames per second, 8000..192000. */
    uint8_t channels;     /* 1 or 2 interleaved channels. */
    /* Usable depth in 32 kHz frames, rounded down to 5040-frame blocks, clamped to 3..10 blocks.
     * Zero selects 5 blocks (0.7875 seconds). A separate guard block is allocated per channel. */
    uint32_t ring_frames;
    /* Preparation threshold in 32 kHz frames, rounded up to blocks. Zero fills the ring. */
    uint32_t prepare_frames;
    float volume; /* 0..2 */
    bool stop_on_scene_change;
    bool duck_bgm;
} AudioStreamDesc;

#define AUDIO_STREAM_DESC_INIT                                                                     \
    {sizeof(AudioStreamDesc), AUDIO_SOURCE_PCM, AUDIO_FORMAT_S16, 32000u, 2u, 0u, 0u, 1.0f, true,  \
        true}

/* Initialize with AUDIO_STREAM_STATE_INIT before calling get_state. */
typedef struct AudioStreamState {
    uint32_t struct_size;
    AudioStreamPhase phase;
    uint32_t buffered_frames; /* 32 kHz frames, including staging and resampler input. */
    uint32_t underrun_count;
    uint64_t position_frames; /* 32 kHz frames consumed; pitch changes playback duration. */
} AudioStreamState;

#define AUDIO_STREAM_STATE_INIT {sizeof(AudioStreamState), AUDIO_STREAM_OPENING, 0u, 0u, 0u}

/*
 * Game thread only. At most four open streams across all mods. Handles belong to their opener
 * and are invalidated by close or detach. PCM is native-endian, interleaved, with 1 or 2 channels.
 * Mods decode on their own and implement loops by seeking their decoder and continuing to write.
 * Fade/ramp arguments use 32 kHz audio frames, rounded up to JAudio ticks (30 ticks/second).
 * Open prepares and locks; play may be requested before preparation completes.
 * End-of-stream is irreversible. Stop is terminal; close releases the handle.
 * Defaults: S16, 32000 Hz, stereo, full preparation, volume 1, scene stop and BGM ducking enabled.
 */
typedef struct AudioService {
    ServiceHeader header;

    /* NULL desc selects the defaults. out_handle is required and cleared on failure.
     * Returns MOD_UNAVAILABLE before audio initialization or while stream capacity is exhausted. */
    ModResult (*open)(ModContext* ctx, const AudioStreamDesc* desc, AudioStreamHandle* out_handle);
    /* Copies up to frame_count source frames; frames may be NULL only when frame_count is zero.
     * out_accepted is required and reports copied frames even if later conversion fails.
     * Zero acceptance is normal backpressure. Stopped, ended, and flushed streams reject writes. */
    ModResult (*write)(ModContext* ctx, AudioStreamHandle stream, const void* frames,
        uint32_t frame_count, uint32_t* out_accepted);
    /* Immediately writable source frames. Returns zero after end_of_stream, stop, or playback end.
     * out_frames is required. Poll/write again on subsequent mod updates. */
    ModResult (*free_frames)(ModContext* ctx, AudioStreamHandle stream, uint32_t* out_frames);
    /* Flushes the final input. Repeated calls succeed; no more frames may be written. */
    ModResult (*end_of_stream)(ModContext* ctx, AudioStreamHandle stream);
    ModResult (*play)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_in_frames);
    /* Terminal, including during preparation. Repeated calls succeed. */
    ModResult (*stop)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_out_frames);
    ModResult (*pause)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_frames);
    ModResult (*resume)(ModContext* ctx, AudioStreamHandle stream, uint32_t fade_frames);
    /* Absolute finite gain in 0..2. */
    ModResult (*set_volume)(
        ModContext* ctx, AudioStreamHandle stream, float volume, uint32_t ramp_frames);
    /* Finite playback-rate multiplier greater than zero and at most four. */
    ModResult (*set_pitch)(ModContext* ctx, AudioStreamHandle stream, float ratio);
    /* Preserves out_state->struct_size. An undersized output is rejected without writing to it. */
    ModResult (*get_state)(ModContext* ctx, AudioStreamHandle stream, AudioStreamState* out_state);
    ModResult (*close)(ModContext* ctx, AudioStreamHandle stream);
} AudioService;

MOD_DECLARE_SERVICE(
    AudioService, svc_audio, AUDIO_SERVICE_ID, AUDIO_SERVICE_MAJOR, AUDIO_SERVICE_MINOR);
