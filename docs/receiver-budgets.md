# Native receiver admission, queue and cancellation budgets

Status: implemented and offline-verified; normal universal build installed. In-app OBS/live-camera validation remains pending. Operator authorized building/installing the receiver; camera agent asked for consistent host budgets before larger-frame handoff. This concern changes only the host receiver, not camera limits, modes or ownership.

## Mechanism and enforcement

Shared `ssp_connector/include/ssp/limits.h` defines32MiB native wire-body admission (the4byte length prefix is separate),32MiB media access-unit maximum,32MiB+1024byte aggregate IPC-body ceiling,64MiB resident encoded payload and32 resident packets. The native parser checks the declared wire size before allocation. These are host limits, not measured camera-server limits. ProRes framing checks share the same media bound.

The IPC reader validates known message type and type-specific outer lengths before body allocation. Metadata size is exact; video/audio include a nonempty payload no larger than32MiB; empty control messages are exact; exceptions are at most64KiB. Embedded video/audio lengths must exactly match the remaining body, and exception text must be bounded/NUL-terminated. Malformed/truncated/oversized input closes the reader rather than exposing an unchecked pointer to a decoder callback. Message ABI fields themselves remain unchanged.

VFrameQueue counts payload bytes and packet count until the decoder callback COMPLETES, so the currently decoding access unit is included. It checks admission before copying, owns the payload copy and holds at most64MiB/32 packets. Below limits, compressed receive order and reorderedPTS are preserved. On overload/invalid admission it frees pending packets, discards non-keyframes, and accepts a recovery keyframe only within the same bounds. A discontinuity callback runs on the decoder worker before recovery decode, flushing codec references and rearming the source intra-frame gate. ProRes is identified as intra by its valid complete frame as well as native keyframe flags. This replaces unbounded buffering only under overload; it does not randomly drop inter-frame references and continue decoding a broken chain.

Start/stop are serialized. Stop prevents admission, wakes and joins the worker, releases pending copies, and drains stale semaphore tokens; repeated stop and a subsequent start are tested. The source joins connector/queue workers before releasing decoders. A process-pipe guard terminates only its owned connector child before joining stderr, including protocol-rejection paths where the child may be blocked writing stdout. Normal Stop likewise terminates before waiting for the stdout reader. No camera command or source-setting write is sent by these paths.

## Bounds and limits

64MiB means resident encoded payload, including in-flight, not total process/GPU memory or allocator/container overhead. Parent IPC buffers, connector process, FFmpeg packet copies/scratch/decoded frames and OBS async caches are additional. ProRes and peer decoder limits remain32MP/4slice threads; legacy camera AVC/HEVC decode policy is unchanged. Aggregate multi-source memory and real-time throughput remain deployment measurements. Larger admission does not implement a new codec or prove the camera can transmit that size.

Explicit12-bit ap4h decoder initialization/I412 works with the actual host fixture, but native/peer descriptor delivery remains pending. Codec1006 alone is not a12-bit declaration. RAW/Bayer/alpha and native12-bit capability are not promoted by this budget work. Camera agent's per-mode cadence/image-quality acceptance remains independent.

## Verification

Tests first: queue bound/reset/stat APIs failed compilation before implementation; IPC/pipe guard tests failed at missing validators/guard; production native socket admission rejected the real~6.1MB access unit under the old4MiB cap. After implementation:

- Forced stalled real worker verifies64MiB byte and32-packet bounds INCLUDINGin-flight, copied-data ownership, dropping references, keyframe recovery/reset ordering, oversized/empty rejection, concurrent stop/drain/restart and zero resident payload after stop.
- Existing reorderedPTS receive-order test passes.
- Actual private host ProRes access unit wrapped in explicit UNIT SSP framing crosses a real Unix socket and production `ssp_recv_packet`/`handle_video` byte-exactly; a prefix above32MiB rejects before allocation. This is admission/parsing evidence, not a native camera12-bit stream or throughput benchmark.
- Packed IPC unit boundaries exercise wrong outer/inner lengths, metadata, audio/video, empty control, unknown/null and bounded exception cases.
- Real adversarial child sends an invalid IPC header and blocks writing stdout; the production guard terminates it and joins stderr promptly. This is an actual process/cancellation test, not a fake camera service.
- Full22selected offline CTests pass8.49s; synthetic-camera IPC test remains excluded. Universalarm64+x86_64 build and formatting pass. One independent focused security/concurrency review found no concrete issue; reviewer did not run tests/read footage.

No new camera connection/controller/mode/source action or OBS launch occurred. Desktop native pipe remains unavailable; actual in-app/live behavior and Windows/Linux runtime are untested.

## Delivery and recovery

Normal diagnosticsOFF bundle installed at `/Users/hedon/Library/Application Support/obs-studio/plugins/obs-ssp.plugin` with OBSclosed; ad-hoc signature verified and staged/installed hashes match. BinarySHA256 `10670ad8c7587a73d8f83e1075ec2a60271846a18f4addc301dfae4d68faf6bb`. Previous bundle `/Users/hedon/Library/Application Support/obs-studio/plugin-backups/20261024-bounded-receiver-224047/previous.plugin`; owner-only manifest `/Users/Shared/obs-ssp-bounded-receiver-install.json`. Restore with OBSclosed if necessary. No camera rollback is part of host receiver recovery.

Current worktree reused on fix/prores-receiver; no new worktree or historical cleanup. Task-owned staging/private fixtures/evidence retained outside git. Source/test logs under `/Users/agent-codex/obs-ssp-diagnostics/{queue,packet,ipc,pipe,budget}-*`. Private footage/payloads are not committed/published. Next: explicit format-descriptor transport, GUI capture/live acceptance after camera-window release and desktop-tool recovery; do not reopen passed component checks without changed inputs.
