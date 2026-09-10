#pragma once
#include <mods/api.h>
#ifdef __cplusplus
#include <mods/service.hpp>
#endif

#define AUDIO_SERVICE_ID "dev.twilitrealm.dusklight.audio"
#define AUDIO_SERVICE_MAJOR 1u
#define AUDIO_SERVICE_MINOR 0u

typedef uint64_t AudioStreamHandle;
typedef enum AudioSourceKind { AUDIO_SOURCE_PCM = 0 } AudioSourceKind;
typedef enum AudioSampleFormat { AUDIO_FORMAT_S16 = 0, AUDIO_FORMAT_F32 = 1 } AudioSampleFormat;
typedef enum AudioStreamPhase {
    AUDIO_STREAM_OPENING, AUDIO_STREAM_PREPARED, AUDIO_STREAM_PLAYING,
    AUDIO_STREAM_PAUSED, AUDIO_STREAM_ENDED
} AudioStreamPhase;

typedef struct AudioStreamDesc {
    uint32_t struct_size;
    AudioSourceKind source;
    AudioSampleFormat format;
    uint32_t sample_rate;
    uint8_t channels;
    /* Usable depth in 32 kHz frames, rounded down to 5040-frame blocks, clamped to 3..10 blocks.
     * Zero selects 5 blocks (0.7875 seconds). A separate guard block is allocated per channel. */
    uint32_t ring_frames;
    /* Preparation threshold in 32 kHz frames, rounded up to blocks. Zero fills the ring. */
    uint32_t prepare_frames;
    float volume; /* 0..2 */
    bool stop_on_scene_change;
    bool duck_bgm;
} AudioStreamDesc;

typedef struct AudioStreamState {
    AudioStreamPhase phase;
    uint32_t buffered_frames; /* 32 kHz frames, including staging and resampler input. */
    uint32_t underrun_count;
    uint64_t position_frames; /* 32 kHz frames consumed; pitch changes playback duration. */
} AudioStreamState;

/* Game thread only. At most four open streams across all mods. Handles belong to their opener
 * and are invalidated by close or detach. PCM is native-endian, interleaved, with 1 or 2 channels.
 * Mods decode on their own and implement loops by seeking their decoder and continuing to write.
 * Fade/ramp arguments use 32 kHz audio frames, rounded up to JAudio ticks (30 ticks/second).
 * Open prepares and locks; play may be requested before preparation completes.
 * End-of-stream is irreversible. Stop is terminal; close releases the handle.
 * Defaults: S16, 32000 Hz, stereo, full preparation, volume 1, scene stop and BGM ducking enabled. */
typedef struct AudioService {
    ServiceHeader header;
    const AudioStreamDesc* default_stream_desc;
    ModResult (*open)(ModContext*, const AudioStreamDesc*, AudioStreamHandle*);
    /* Copies up to out_accepted source frames. Zero acceptance is normal backpressure. */
    ModResult (*write)(ModContext*, AudioStreamHandle, const void*, uint32_t, uint32_t* out_accepted);
    /* Immediately writable source frames. Poll/write again on subsequent mod updates. */
    ModResult (*free_frames)(ModContext*, AudioStreamHandle, uint32_t*);
    ModResult (*end_of_stream)(ModContext*, AudioStreamHandle);
    ModResult (*play)(ModContext*, AudioStreamHandle, uint32_t fade_in_frames);
    ModResult (*stop)(ModContext*, AudioStreamHandle, uint32_t fade_out_frames);
    ModResult (*pause)(ModContext*, AudioStreamHandle, uint32_t fade_frames);
    ModResult (*resume)(ModContext*, AudioStreamHandle, uint32_t fade_frames);
    ModResult (*set_volume)(ModContext*, AudioStreamHandle, float, uint32_t ramp_frames);
    ModResult (*set_pitch)(ModContext*, AudioStreamHandle, float);
    ModResult (*get_state)(ModContext*, AudioStreamHandle, AudioStreamState*);
    ModResult (*close)(ModContext*, AudioStreamHandle);
} AudioService;
MOD_DECLARE_SERVICE(AudioService, svc_audio, AUDIO_SERVICE_ID, AUDIO_SERVICE_MAJOR, AUDIO_SERVICE_MINOR);
