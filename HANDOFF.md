# Camera controls checkpoint — 2026-10-23

## Implemented and observed

Branch feat/camera-controls based on e26e104, agent checkout /Users/agent-codex/obs-ssp-macos-fix. Added camera-advertised imaging controls (37 observed), explicit H.2658/10bit selection, read/apply/pause/resume and one-shot autofocus; validated read-only/range/choice handling and encoded query writes. Read-only malformed metadata fails closed. Async callbacks hold a shared source-lifetime guard; real request cancellation and concurrent lifetime tests pass. Camera controller no longer logs response bodies/URLs. No automatic camera settings writes on source startup. Optional stock OBS LUT uses private per-source/filter ownership to protect unrelated filters, including renamed filters and name collisions.

ProRes Proxy/LT/Standard/HQ decoder tests use real generated10bit422frames, converted to OBSI210. Network ProRes is unverified; ZRaw decoder/framing still requires an actual compatible contract/sample. User-facing feature names are ProRes support and ZRaw support, with accurate availability limits.

Prior full universal build and8CTestcases passed, including40case H264/HEVC8/10bit matrix. Actual camera read returned37controls; controller destruction test returned0callbacks. Signed controls bundle installed and OBS UI displayed controls/bitdepth. Recovery /Users/hedon/Library/Application Support/obs-studio/plugin-backups/20261023-160721-camera-controls . Current operator camera settings were preserved; recent live preview was secondary AVC/AAC.

## Unfinished acceptance and next steps

This is a recoverable checkpoint, not a completed controls release. Validate managed OBS-side LUT through the real OBS engine or real committed Qt input: AX setValue changed displayed text without persisting the file path; no managed LUT filter was created. Local SSP source may have LUT checkbox enabled with blank path (no image effect). Restore that test state after validation, protecting other filters. Identity test LUT is /Users/agent-codex/obs-ssp-diagnostics/identity-test.cube. Then update docs, finish controls delivery.

Native framing fix is a separate committed branch fix/ssp-wire-framing417d31f; its six-second live captures verified independent mainHEVC10bit/PCM and secondaryAVC/AAC. Actual plugin decoder harness decoded177/177main softwareframes and160/160secondary hardwareframes;3native focusedchecks passed. Native correction is not installed in OBS. Integrate through normal branch/PR flow rather than mixing concerns.

OBS-to-OBS SSP transport remains authorized but unimplemented; task1219837003201276. Controls task1219837591349200; firmware backlog1219837206662019; native protocol task1219838151737717. Camera-agent thread01a16a43-8508-77f1-8341-1dc1c4a4e5e7 is the integration contact; coordinate before stream subscriptions and never publish private media captures.

## Security review and blockers

Independent camera-controls review identified source callback lifetime, malformed readonly metadata and public LUT ownership concerns. Implemented lifetime guards/cancellation checks, fail-closed readonly and private ownership/typechecks. Reviewer did not independently rerun fixes; checks described above are implementation evidence. ProRes network and ZRaw remain blocked on actual media and compatible framing/decoder; other camera models/Windows/Linux untested.

An unrelated RTSP credential reached the transcript through an earlier insufficiently filtered log read. Rotation remains BLOCKED on operator camera administration. Operator must rotate account, update consumers, verify old rejection/new success. Never print saved credentials; whitelist SSP logs and redact embedded URL credentials before output. Metadata-only incident /Users/agent-codex/obs-ssp-diagnostics/credential-exposure-20261023.md; task1219837730457886.

## Resources

Preserve installed bundle, scene backups, diagnostic private media and all unknown/unrelated files. No worktree allocated for controls; separate native protocol worktree is temporary until its push is verified. Build/cache/stage resources remain under agent-codex. Prior playback recovery20261023-152646-ssp-playback retained. No firmware files changed.
