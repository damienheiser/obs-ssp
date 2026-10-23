# Native SSP wire contract — 2026-10-23

## Done and evidence

Branch fix/ssp-wire-framing corrects the production native stream-start enable/selector fields and optional20-byte video/audio extensions. Tests first reproduced the serializer defect. Production static helpers are exercised directly with socketpair serialization and actual private camera packet captures, including extension truncations and legacy bodies. Protocol documentation now distinguishes microsecond wirePTS from metadata cadence and PCM block size from bit depth. No camera formats, firmware or authentication settings changed. See docs/ssp-wire-contract.md.

Operator-authorized native live tests, coordinated exclusively with the camera agent, captured6seconds each: main177video/279audio4096x2160HEVC265+PCM23; secondary160video/281audio1920x1012AVC96+AAC37. Both wirePTS deltas were measured in microseconds. External FFmpeg decoded mainHEVC, secondaryAVC/AAC and mainPCM without error. Actual plugin decoder harness decoded all177/177mainHEVC10bit frames in software and160/160secondaryAVC8bit frames through VideoToolbox. Focused CMake checks passed3/3 (existing protocol helpers, production selector, actual captured-media parser); clang-format19 and gersemi0.21 checks passed. Other physical models and platforms are untested. Installed OBS continues using the prior libssp-based bundle; this native change has not been installed into OBS.

## Next

Commit/push this verified native fix, then complete camera-controls work separately in /Users/agent-codex/obs-ssp-macos-fix (feat/camera-controls), preserving its dirty work. OBS-to-OBS transport remains separate authorized work, not implemented. ProRes support and ZRaw support require actual network samples; ProRes422 is only a proposed/recovered-client identifier and ZRaw framing/decoder are unresolved. Never publish private media captures.

Camera-agent thread01a16a43-8508-77f1-8341-1dc1c4a4e5e7 confirmed contract; main/secondary captured privately in /Users/agent-codex/obs-ssp-diagnostics/live-stream{1,2}.{video,audio}. Native test window released after captures; no test camera connections left open.

## Recovery and protected resources

Revert this focused commit or restore prior native connector. Previous installed playback plugin recovery is /Users/hedon/Library/Application Support/obs-studio/plugin-backups/20261023-152646-ssp-playback; controls recovery20261023-160721-camera-controls. Private captures and unrelated camera-controls changes are protected. Temporary native worktree will be retired after delivery and destination verification; retain diagnostic artifacts privately. Camera settings were unchanged.

## Independent blocker

An unrelated RTSP credential reached the transcript earlier through an insufficiently filtered OBS log. Rotation remains BLOCKED on operator camera administration; do not read or print saved credentials. Operator must rotate account, update consumers and verify old rejection/new success. Incident task1219837730457886; metadata-only incident record /Users/agent-codex/obs-ssp-diagnostics/credential-exposure-20261023.md.

Task: https://app.asana.com/0/0/1219838151737717 .
