# AudioService

AudioService 1.0 accepts decoded PCM through the normal JAI stream mixer. Include
`mods/svc/audio.h`; game linkage is unnecessary. AudioResService manages resident wave and
file-based sound replacements. This unreleased interface replaces the previous streaming API;
rebuild consumers against the new header.

Calls run on the game thread. Handles belong to their opening mod and become invalid on close
or unload. At most four stream allocations, including retiring streams, can exist across mods.
A stream that has ended still owns its allocation until closed.

## Input and buffering

Initialize `AudioStreamDesc` with `AUDIO_STREAM_DESC_INIT`. Passing `NULL` to `open` selects the
same defaults: interleaved native-endian F32, 48000 Hz, stereo, volume/pitch 1, scene stop and BGM
ducking enabled. S16, mono, and source rates from 8000 through 192000 Hz are also supported.
The source format, rate, and channel count cannot change after opening.

Every `_frames` field counts source frames. One frame contains one sample per channel.
`capacity_frames == 0` selects 500 ms of source content. Explicit capacities must represent
50 through 2000 ms. `prepare_frames == 0` selects 100 ms, limited to capacity; an explicit value
must be between one and capacity. Durations describe source content at pitch 1.

`write` copies up to the available ring space and returns the accepted prefix. Partial and zero
acceptance are normal; retain and retry the remainder. A null sample pointer requires zero
frames. `get_writable_frames` reports immediate ring space. `buffered_frames` estimates all
unpresented source content, including converter read-ahead, so it can exceed ring capacity.
Finite F32 headroom is preserved. Non-finite samples become silence.

```cpp
AudioStreamHandle music = 0;
AudioStreamDesc desc = AUDIO_STREAM_DESC_INIT;
desc.sample_rate = decoder.sample_rate();
desc.channels = decoder.channels();
ModResult result = svc_audio->open(mod_ctx, &desc, &music);
if (result == MOD_OK) {
    result = svc_audio->play(mod_ctx, music, 200);
}
// On subsequent updates, retain any unaccepted frames for the next write.
uint32_t accepted = 0;
result = svc_audio->write(mod_ctx, music, pcm, frameCount, &accepted);
```

Decode files outside the audio callback. Source loops seek the decoder and continue writing;
they do not seal or reset the stream. Seeking replaces playback with a new stream. The
`mods/music_player` example demonstrates decoder buffering, loops, and controls.

## Playback and controls

`play` requests playback while preparing and resumes paused playback. Preparation completes at
the source-frame threshold, or at EOF for a shorter nonempty source. Repeating `play` during
playback does nothing; during a pause fade it cancels the pause and fades back in.

`pause` fades to silence before suspending source consumption. Repeated pause does not restart
the fade. Before playback, pause cancels the pending play request. Game-wide pause remains a
separate JAI policy.

`end_of_stream` seals input without requesting playback. It is idempotent and requires no
producer flush or frame-end pump. An empty sealed stream ends without acquiring playback voices.
A temporarily empty ring preserves converter history and leaves the phase `PLAYING`, with
`starved` set. `underrun_count` increments once on entry into starvation. Pause, preparation and
normal EOF are not underruns.

`stop` is terminal and seals input. The phase remains `STOPPING` during an active fade/release,
then becomes `ENDED` after channel retirement. A paused or unstarted stream can retire
immediately. Stop and EOF remain successful no-ops after termination. `close` cancels playback
and releases the public handle immediately; internal references can retire later.

Volume must be finite in `[0, 2]`; pitch must be finite in `[1/32, 4]`. Setters replace their
service-controlled targets; game/JAI modifiers still compose around them. Fade and ramp
arguments use milliseconds, rounded up to 30 Hz JAI control ticks. Zero changes immediately.
Pitch changes both playback speed and duration.

After JAS composition, PCM pitch is capped at 8, and the source step has a minimum of `1/256`.
`effective_pitch` reports the applied multiplier and `pitch_limited` indicates either limit.
Finite non-positive composed pitch holds silently without consuming input. Non-finite composed
pitch terminates playback with `AUDIO_STREAM_ERROR_INVALID_PITCH`.

## State and errors

Initialize state with `AUDIO_STREAM_STATE_INIT` before `get_state`. The host preserves
`struct_size` and unknown trailing bytes. Undersized structures are rejected. Counts and handles
are cleared before a failed call when their output pointer is supplied.

`position_frames` is the floor of source presentation progress represented by generated mixer
output, rather than converter read-ahead or the physical device cursor. Pause and starvation
silence do not advance it. Natural drain reports the exact final source position; cancellation
preserves the last presented position. Terminal streams report no playable buffered content.

`ENDED` with no error covers natural EOF and explicit stop. Async errors persist in state and
are logged once to the owning mod's logger. State, close, and idempotent stop/EOF remain available
after failure. Control operations on a failed stream return `MOD_ERROR`.

Invalid arguments and stale/foreign handles return `MOD_INVALID_ARGUMENT`; closed-input and
illegal lifecycle operations return `MOD_CONFLICT`; resource exhaustion returns
`MOD_UNAVAILABLE`. A partial write to open input returns `MOD_OK`.

Scene-stop and BGM ducking use the existing Z2 sound policy. Ducking ends on pause, retirement,
close, or mod unload. State remains available after JAI releases its sound handle, until close.
EOF describes converter drain; effect tails and device latency can continue afterward.
