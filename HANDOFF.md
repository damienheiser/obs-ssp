# macOS fresh-checkout configuration fix

The first local configure left PLUGIN_BUILD_NUMBER unset, dropping a property value in the macOS helper. Default it to 1 while preserving cached increments, CI run IDs, and explicit overrides. Add an actionable missing-mDNS-submodule diagnostic and document initialization alongside the macOS preset commands. No protocol or runtime behavior changes.

Validation: reproduced the fresh-checkout failure before implementation; all seven checks in `python3 cmake/tests/test_buildnumber.py` pass. `cmake --preset macos` and `cmake --build --preset macos` pass on macOS with AppleClang 21 / macOS 27 SDK, producing arm64 and x86_64 binaries. Existing FFmpeg pkg-config and CMake policy warnings remain. OBS camera playback, other platforms, and native SSP mode were not tested.

Delivery: branch `fix/macos-local-configure` for `damienheiser/obs-ssp`. Author and committer requested by operator: Damien Heiser <14894171+damienheiser@users.noreply.github.com>. Operator will file the upstream PR. Next: open that PR from the feature branch; manually validate camera playback in OBS. No build blocker remains.

Asana: https://app.asana.com/1/1198722117693833/project/1198722117693841/task/1219836346553340

Recovery: revert the focused fix commit. Original operator checkout and caches are protected; agent validation checkout and build log are retained for inspection. No temporary worktrees were created.

## Local installation — 2026-10-23

Installed the validated universal libssp-based build into `/Users/hedon/Library/Application Support/obs-studio/plugins/obs-ssp.plugin` using CMake staging, including `Contents/MacOS/ssp-connector` and `Contents/Frameworks/libssp.dylib`. Ad-hoc signed the assembled bundle and verified its signature. No existing obs-ssp installation was present; the Canon plugin was preserved. Operator stopped OBS; restarted OBS 32.2.2 through computer use. Startup log `2026-10-23 14-53-00.txt` reports obs-ssp 0.13.0 loaded and mDNS query thread started. Observed SSP Source in Add Source. Camera connection/audio/video remain untested; next action is to add SSP Source and connect the camera. Rollback: quit OBS and move this plugin bundle outside the plugins directory. Agent staging and validation artifacts retained; no worktrees created or removed.

## Streaming stall diagnosis — 2026-10-23

Camera 172.22.3.171 responds to HTTP /info and read-only /ctrl/get queries: model elephant, H.265, Stream0, 4K (Low Noise), 29.97 fps. Four pings: zero loss, 0.951–3.728 ms. No camera settings changed.

OBS log 2026-10-23 14-53-00.txt shows successful SSP handshake and 3840x2160 metadata, repeated `avresample_open failed: error code -12`, and max audio buffering at 960 ms. Changing bitrate from 200 to 20 Mi-bit/s did not remove errors. Most recent connection at 14:56:39 has connection notification but no subsequent metadata in inspected log. Two-second connector process sample shows libssp waiting in kevent, not blocked writing IPC. This alone does not establish camera stream health.

Confirmed incompatible FFmpeg majors: plugin compiled using obs-deps headers/libs avcodec 61 / avutil 59, while lsof of running OBS confirms it loads /Applications/OBS.app/Contents/Frameworks libraries, avcodec 62 / avutil 60 (otool). Homebrew pkg-config version 63 printed during configure was not the selected header path: cache points to bundled obs-deps include. Plugin load success was insufficient runtime validation. This mismatch is a concrete defect and likely contributor to decode/audio errors, but correcting it and observing continuous playback is required to establish causality.

Next: rebuild against FFmpeg headers and libraries matching installed OBS 32.2.2, add an explicit runtime major-version check, reinstall with recovery copy, and observe sustained SSP audio/video. Investigate remaining transport stall separately if it persists. No runtime fix made in this diagnostic pass. /tmp/obs-ssp-connector-sample.txt retained. No worktrees created/removed.
