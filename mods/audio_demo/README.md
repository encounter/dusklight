# Audio demo

A music player using FileService, AudioService, and UiService. Enable **[Demo] Music Player** in Mods, start the game, then open **F1 → Music**. The controls are also available in the mod's panel.

Choose an MP3, Ogg Vorbis, FLAC, or WAV file and press **Play**. The Material icon buttons are restart, play/resume, pause, stop, and next. Other controls include percentage seeking, volume, speed/pitch, fade time, looping, and game music ducking. Track information shows format, sample rate, channels, compressed size, position, and duration.

The **Queue & history** tab has two scrollable lists. Use the add-file, add-folder, and trash icons to manage the lists. Adding a folder queues its supported audio files in name order; subfolders are skipped. Select a queued track to play it immediately, or let playback advance automatically. Next skips to the front of the queue even when looping. Stop preserves the queue. Clear queue leaves the current track playing.

History keeps the last 32 distinct tracks played in this session, newest first. Select a history item to queue it again. The queue holds up to 256 entries; files are checked and decoded only when played. Missing, inaccessible, or damaged files are skipped with an error in the queue status and mod log. Queue and history are cleared when the mod unloads.

The demo accepts mono or stereo audio from 8 to 192 kHz, with compressed files up to 256 MiB. Ogg Opus is not supported. FileService retains the compressed file in memory; decoding produces small interleaved float chunks on demand. AudioService handles resampling and playback. Seeking recreates the stream and preserves whether playback was paused. Stop closes immediately; fade time applies to play, pause, resume, and volume changes. Speed changes also change pitch. Playback follows game master volume and pause state.

Build from the repository root:

```sh
cmake --preset macos-default-debug
cmake --build --preset macos-default-debug
```

The bundle is `build/macos-default-debug/mods/audio_demo.dusk`.

## Bundled decoders

Unmodified sources in `vendor/`:

- dr_mp3, dr_flac, dr_wav: https://github.com/mackron/dr_libs at `dfe8377631000664666519fdb83da193fd8037f4`.
- stb_vorbis: https://github.com/nothings/stb at `2c980bb59875b0d32144a71867fbdebb2f77cd20`.

Both projects offer MIT or public-domain licensing. License texts and revisions are included in the bundle's `res/third-party-licenses.txt`.

The player uses UiService 2.3 rows and icon buttons. Custom RCSS adjusts typography and control sizing;
the host supplies row navigation, Material Symbols, and local action tooltips.
