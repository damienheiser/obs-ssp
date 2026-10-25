# Native ProRes receiver

Status: implemented, built and installed locally; production parser/decode verified against nine actual camera frames. In-app OBS rendering and a live ProRes session remain unverified.

## Requirement and mechanism

The operator clarified that this task owns the receiver for the camera agent's native ProRes sender. Existing camera reception is retained. The receiver accepts declared codec1006 and complete standard length-framed `icpf` ProRes frames through the existing SSP video packet and IPC callbacks. No encoder/firmware patch or synthetic camera service is introduced. Actual Trial12 packets use wiretype0x6f, frame_type5, base17bytes plus the observed20byte extension; the production parser returns the exact compressed payload and microsecond PTS. No new packet type is inferred from an internal camera producer enum.

The original generic ProRes decoder used automatic frame threading. The first actual C4K frame was buffered rather than returned, so a short native burst could produce no displayed frame until EOF/flush. ProRes now uses at most four slice threads with a33,554,432pixel bound. As an intra-only codec it needs no inter-frame reordering. Each recorded access unit returns an immediate I21010bit4:2:2 frame with exact dimensions/PTS, without EOF flushing. Existing AVC/HEVC decoder defaults remain unchanged. The source already disables hardware decoding for ProRes even when its hardware-acceleration property is enabled.

Declared codec1006 is recognized in metadata. For actual payload detection, complete `icpf` length framing must match the provided byte count; malformed/truncated declarations are rejected. Generic ProRes payload sniffing remains additive for existing senders. The existing4MiB native SSP packet cap is unchanged; Trial12 payloads are1,240,112–1,257,076bytes. The codec detector additionally caps accepted ProRes buffers at32MiB for other existing ingress paths. No full4K-HQ/4444/12bit/RAW or arbitrary sender framing claim follows from these bounds.

## Authorization and availability

Camera mutation remains with the camera agent. This receiver change makes no camera-format/controller writes and does not promote the experimental sender to normal capability availability. Camera-reported support and transport qualification remain separate. The sender's Trial12 stopped after9video/3observedaudio packets; its producer lifecycle is not stable. Audio payloads were not retained in the provided capture and are not independently qualified by this test. Existing PCM/AAC receive behavior is unchanged.

## Verification

Tests were written first using actual private Trial12 `.wire`/`.payload` files. The production `ssp.c` handler is compiled into the regression executable so parsing is not reimplemented. Before the fix, frame0 returned no output and the immediate-display assertion failed. After the fix,9/9 actual native C4K frames parse byte-exactly and decode immediately as4096x2160I210, preserving each microsecond PTS without EOF flush. The test also checks codec1006 metadata, malformed framing, decoder threading and pixel bounds. PTS deltas include33.366/33.367ms,66.733ms and100.100ms; they are preserved, not presented as a sustained29.97fps result. Existing generated real ProRes profile encode/decode tests still pass. Sixteen selected offline CTests pass7.10s; synthetic-camera IPC test remains excluded.

The macOS universal x86_64+arm64 normal build passes. Source formatting and CMake formatting pass. An explicit defaultOFF `OBS_SSP_PRORES_DIAGNOSTICS` variant also compiles: it reads the actual capture, invokes the production OBS metadata/video callbacks with hardware flag enabled, and checks9decodedI210frames plus OBS async texture dimensions in a private view. It creates no camera/network service. This in-app check has NOT RUN: desktop-control native pipe failed and a reset/inventory retry failed identically. OBS was closed during installation. No in-app/live success is claimed. Windows/Linux and sustained sender/receiver soak remain untested.

## Installation and recovery

Normal diagnosticsOFF bundle installed at `/Users/hedon/Library/Application Support/obs-studio/plugins/obs-ssp.plugin`; ad-hoc signature verified. Installed binary SHA256: `322b4ed2e1e45ea358d74edf336cbddb66a2b3b49cb9898754cec8643fcc42bd`. Installation manifest `/Users/Shared/obs-ssp-prores-install.json` is owner-only. Previous bundle preserved at `/Users/hedon/Library/Application Support/obs-studio/plugin-backups/20261024-prores-receiver-183011/previous.plugin`. OBS was not running and remains unlaunched by this task due to desktop-tool failure. Restore the previous bundle with OBS closed if necessary; no camera rollback is required by receiver code.

Private evidence stays outside git: `/Users/agent-codex/zcam-freedom-stable/reference-private/native-prores-boot12/ssp/`. Build/test logs: `/Users/agent-codex/obs-ssp-diagnostics/prores-*`. Diagnostic and normal staging bundles are task-owned and retained pending the actual OBS acceptance check. No new worktree was created; current checkout reused on `fix/prores-receiver`. Next: run the compiled private capture diagnostic once desktop control works, then coordinate a guarded live sender window and observe actual OBS video/audio/reconnect/stop without promoting unsupported capabilities.
