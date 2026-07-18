# Multi-Scene Stream — OBS plugin

[![Build](https://github.com/bakhodirovxz/obs-multiscene-stream/actions/workflows/push.yaml/badge.svg)](https://github.com/bakhodirovxz/obs-multiscene-stream/actions/workflows/push.yaml)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](LICENSE)

Stream **different scenes to different RTMP(S) destinations simultaneously** from a single OBS instance — independently of the main program output.

Example: Scene A ("Table 1") goes to YouTube channel 1 while Scene B ("Table 2") goes to YouTube channel 2, and the main OBS output stays free for a third destination or recording.

- **Target OBS:** 30.0+ (built and tested against OBS 31/32)
- **Platforms:** Windows x64 (primary); Linux and macOS build from source
- **License:** GPL-2.0

## Features

- Up to **4 independent virtual outputs**, each with:
  - its own **bound scene**, rendered even when it is not the active program scene
  - its own RTMP(S) server and stream key (YouTube / Twitch presets built in)
  - its own video encoder — NVENC / QSV / AMF / x264 auto-detected, best available preselected
  - its own bitrate, scaled output resolution, audio mixer track (1–6) and AAC bitrate
- Dockable **"Multi-Scene Stream"** panel:
  - per-output card with status dot and text (Idle / Connecting / Live / Reconnecting / Error)
  - live elapsed time and measured bitrate while streaming
  - Start/Stop per output, Start All / Stop All, right-click menu for delete
- **Automatic reconnect** (2 s delay, up to 25 retries)
- **Safe by default:**
  - stream keys encrypted at rest with per-user Windows DPAPI — never stored or logged in plain text
  - config saved atomically (write + rename), debounced
  - only `rtmp://` / `rtmps://` URLs accepted; `rtmps` recommended where supported
  - no telemetry, no network connections besides the streams you configure
- **Zero idle cost:** views, encoders and outputs are created on Start and fully destroyed on Stop — idle outputs add no rendering or encoding load
- Localized UI: English and Russian
- First-time-user friendly: pick a scene, paste a key, press Start — sane defaults for everything else

## Install (Windows)

**Option A — installer:** download `obs-multiscene-stream-x.y.z-setup.exe` from [Releases](https://github.com/bakhodirovxz/obs-multiscene-stream/releases), close OBS, run it. No admin rights needed (installs to the machine-wide OBS plugin folder `C:\ProgramData\obs-studio\plugins\`).

**Option B — zip:** extract the release zip into your OBS installation folder (`obs-plugins/64bit/` and `data/obs-plugins/obs-multiscene-stream/`).

Then start OBS and open **Docks → Multi-Scene Stream**.

## Quick start

1. Click **+ Add Output**.
2. Pick the scene to stream and a server preset (YouTube RTMPS recommended), paste your stream key.
3. **Save**, then **Start**. The dot turns green when live; the card shows elapsed time and bitrate.

Video/audio details (encoder, resolution, bitrates, audio track) live under **Advanced** — the defaults give a working 1080p stream out of the box.

## Build from source

Requirements: CMake 3.28+, Visual Studio 2022 (or newer Build Tools), Windows 10 SDK 10.0.20348+. On Linux/macOS: the standard [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) toolchains.

```powershell
cmake --preset windows-x64     # downloads OBS sources, obs-deps and Qt6 automatically
cmake --build --preset windows-x64
```

The built module lands in `build_x64/RelWithDebInfo/`. On Linux use `--preset ubuntu-x86_64`, on macOS `--preset macos`.

To build the Windows installer: `makensis -DSRCDIR=<repo> -DOUTDIR=<out> installer/windows-installer.nsi` (NSIS 3.x).

> Using a newer Visual Studio locally? Add a `CMakeUserPresets.json` overriding the generator — see CMake presets documentation. The committed preset targets VS 2022 to match CI.

## Testing against a local RTMP server

```
docker run -p 1935:1935 -p 8080:8080 bluenviron/mediamtx
```

Set server `rtmp://localhost/live`, key `test1`, then watch the stream with:

```
ffplay rtmp://localhost/live/test1
```

## Security notes

- Stream keys are stored as per-user **DPAPI** blobs (`key_protected` in the plugin's `config.json`); copying the config to another user or machine requires re-entering the keys — by design. On Linux/macOS v1 falls back to base64 obfuscation (OS keychain integration is planned).
- The key field is masked in the UI with an explicit Show toggle; keys never appear in OBS logs.
- Server URLs are validated: only `rtmp://` and `rtmps://` schemes are accepted, control characters are rejected, keys are capped at 512 characters.
- On Linux/macOS the config file is written with `0600` permissions.
- No third-party dependencies beyond libobs, obs-frontend-api and the Qt shipped with OBS.

## Performance notes

Idle outputs add no load. Each **active** output adds one scene render pass plus one encoder session:

- **NVENC:** each extra 1080p output costs roughly one NVENC session plus a small GPU render cost. Consumer NVIDIA GPUs allow a limited number of concurrent sessions (5–8 depending on driver/generation).
- **x264:** budget roughly one physical core per additional 1080p `veryfast` encode.
- Suggested minimum for two extra 1080p outputs: a 6-core CPU with an NVIDIA GTX 16/RTX-series GPU, or drop to 720p / lower bitrate on weaker hardware.

Measure your own numbers with the OBS Stats dock (View → Stats) before going live for real.

## Known limitations (v1)

- FPS is inherited from the main OBS video settings (no per-output FPS).
- RTMP/RTMPS only (no SRT/WHIP yet).
- The bound scene cannot be changed while that output is live (renaming the scene is fine — the plugin follows it).
- No per-output recording.

## Credits

Built on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). Inspired by the gap left between [Aitum Multistream](https://github.com/Aitum/obs-aitum-multistream) (same canvas to many destinations) and [Vertical Canvas](https://github.com/Aitum/obs-vertical-canvas) (a second canvas): this plugin streams *any scene* to *any destination*, per output.

## License

GPL-2.0 — see [LICENSE](LICENSE).
