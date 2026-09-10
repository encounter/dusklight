# AudioService

AudioService 1.0 accepts decoded PCM on the game thread and plays it through JAudio's stream
pipeline. Import `mods/svc/audio.h`; game linkage is not required for playback. Use AudioResService
for resident sound effects and HookService for intercepting BGM selection or boss phase changes.

## Playback

Copy `default_stream_desc`, set the source format and rate, and call `open`. Sources are native-endian
interleaved S16 or F32, 8000 through 192000 Hz, with one or two channels. SDL converts each source to
32 kHz PCM16 before it enters the stream ring. No mod callbacks run on the audio or load thread.

`open` prepares a locked stream. Push frames in `mod_update` until the desired preparation threshold
is reached. `play` may be requested earlier; playback starts after preparation completes. A source
shorter than the threshold becomes prepared when `end_of_stream` flushes its final data.

```cpp
IMPORT_SERVICE(AudioService, svc_audio);

AudioStreamHandle music{};

ModResult open_music() {
    auto desc = *svc_audio->default_stream_desc;
    desc.format = AUDIO_FORMAT_F32;
    desc.sample_rate = 48000;
    desc.channels = 2;
    return svc_audio->open(mod_ctx, &desc, &music);
}

// Keep unaccepted decoder output for the next update.
ModResult push_music(const float* pcm, uint32_t frames, uint32_t* accepted) {
    return svc_audio->write(mod_ctx, music, pcm, frames, accepted);
}
```

The mod owns decoding and file format support. Read compressed files through FileService at
assignment time, then decode incrementally from memory. Implement loops by seeking the decoder
and continuing to push. Call `end_of_stream` only after the last input frame has been accepted;
subsequent writes are invalid. The host flushes resampler history and pads only the unused portion
of the final transfer block. That padding is excluded from the playback endpoint.

## Buffering and timing

`ring_frames` describes usable depth at 32 kHz. The default is five 5040-frame blocks (0.7875 seconds).
Explicit depths round down to whole blocks and clamp to three through ten blocks; each channel
also receives one guard block. `prepare_frames` rounds up to blocks within that usable depth and
defaults to the full ring. Smaller thresholds start sooner but provide less protection against
shader compilation or other game-thread stalls.

The load thread transfers complete blocks from a two-block staging queue into emulated ARAM.
`free_frames` reports the source frames that can be accepted immediately, accounting for staging,
partial blocks and queued resampler input. It does not count ring capacity that the asynchronous
load thread has not yet made available through staging. Callers must honor `write`'s accepted count,
including zero, and retry during later updates. The resampler input queue is bounded by this budget.

An underrun pauses before the DSP reaches unwritten ring data. Refills resume automatically once
sufficient data is available. `underrun_count` counts starvation transitions, excluding preparation;
`position_frames` counts consumed 32 kHz frames. Pause transitions can have up to two DSP subframes
of in-flight position bookkeeping. `buffered_frames` includes the ring, staging and queued input.

Fade and ramp arguments are 32 kHz audio frames. They round up to 30 Hz JAudio game ticks:
`ceil(frames * 30 / 32000)`. They inherit game pause and tick timing. Pitch ratios must be greater
than zero and at most four. Pitch changes take effect through the stream parameters; source
position and buffering remain expressed in 32 kHz frames. Streams are bounded to JAS's signed
sample range (approximately 18.6 hours at unit pitch); close and reopen for longer playback.

## Controls and lifetime

`pause` fades out before freezing playback; `resume` fades back in from the current intensity.
`set_volume` sets an absolute gain from zero through two, optionally ramped. `stop` is terminal and
uses the vanilla sound fade; stopping an already paused stream ends it immediately. `close` stops
playback and invalidates the generational handle. An ended stream retains its handle until closed.

At most four AudioService streams may be open across all mods. Closing releases the public handle
immediately, while JAudio retains its source and ARAM until pending tasks and DSP channels retire.
A replacement open may therefore return `MOD_UNAVAILABLE` for a few updates after closing.

Streams use section-2 sound-table entries and inherit JAudio master volume, game pause and
`stop_on_scene_change`. BGM ducking defaults on and is shared across playing custom streams;
prepared and paused streams do not hold the duck. Closing or detaching removes table entries and
stops owned streams. Mods must not reuse a handle owned by another mod.

Prepare multiple phase tracks before a fight, then use `play`, `pause` and `resume` to switch or
cross-fade them. Hook `Z2SeqMgr::bgmStart`, `bgmStreamPrepare`, `bgmStop` and related functions when
replacing the game's BGM policy. `changeBgmStatus` operates on sequenced BGM handles and does not
control the separate custom stream handles.

## Implementation notes

The PC stream source replaces header and block production inside `JASAramStream`. Synthetic file
entries resolve through both `Z2SoundInfo` and `JAUStreamFileTable`. Empty reads retain their pending
work for retry rather than dropping vanilla's first-load chain. Disc streams retain their existing
DVD producer and shared read buffer.

PCM streams retain fixed usable ring geometry. Their endpoint can arrive after playback has
started, so the final block's ring index is calculated when the endpoint becomes known. Short
streams receive their endpoint before channel start; block-aligned endpoints include the whole
final block. A late endpoint within 800 frames after a wrap is copied into the reserved guard
block so the final samples remain contiguous. Four additional stream objects and their child parameters are reserved on PC.

GameService major 3 is required for the AudioRes and PCM-source game struct changes. Service-only
mods import AudioService independently of that game ABI epoch.

## Runtime regression tests

Build the opt-in test mod:

```sh
cmake --preset macos-default-debug -DDUSK_AUDIO_TESTS=ON
cmake --build --preset macos-default-debug
```

Load `build/macos-default-debug/mods/audio_test.dusk` and `audio_detach_test.dusk` in a separate
test user directory and launch
`--stage F_SP108`. Disable unrelated demo mods for the run. The test emits `ALL TESTS PASSED` on
success and fails its mod activation on an assertion. Coverage includes four-stream limits,
argument validation, preparation locks, full-buffer backpressure, underrun pause/recovery,
prepared phase switching, stale handles, cancellation during opening, short and block-aligned
endings, running endpoints across the ring boundary, and mono F32 resampling at 44.1/48 kHz.

After the playback tests, the test deliberately fails itself with four pending streams. The
companion lifecycle observer verifies that all four slots can be reused after detach. This
intentional failure is expected in the test log. Disable/re-enable or reload both test mods to
repeat the checks. Audible track replacement and boss-specific hook selection
remain mod-level smoke checks; the automated test uses silence.
