# Camera imaging controls and extended decoding

Operator requested exact camera controls, H.265 bit depth, WB/ISO/shutter/focus, camera profiles/LUTs, ProRes and ZRAW. Network format availability must be confirmed through actual samples and the camera-agent integration contract.

Mechanism: an allowlist of imaging controls is queried using real camera type/options/range/read-only metadata. Unsupported controls are omitted; current read-only controls are displayed disabled. Explicit pause/read/apply/resume actions separate live playback from camera writes. Dependent settings are applied in camera-safe order with fresh validation, encoded query parameters, checked replies and readback. No network, account, Wi-Fi or credential controls are included.

Selected-stream bit width is read back from the camera and shown read-only; alternate bit widths await advertised capability choices. Stream selection lists only camera-reported implemented selectors. Camera profile choices use the camera's `lut` values. OBS-side LUT application uses OBS's stock color-LUT filter, if requested, without removing unrelated filters. ProRes is recognized by a validated frame signature and decoded with FFmpeg, including supported 10-bit 4:2:2 conversion. ZRAW requires a real available decoder and actual network framing; unsupported decoding must be reported clearly and never silently treated as HEVC.

Tests cover validation/readonly/query encoding, actual camera capability reads, ProRes decode and existing40-case decoder matrix. Firmware/network availability and other physical camera models are not assumed. Recovery is restoring prior installed plugin and captured camera state; no firmware files are written.

Independent security review identified callback lifetime, fail-open malformed read-only metadata, and a public LUT ownership marker. The implementation now serializes source callbacks with a lifetime guard invalidated before teardown, cancels in-flight Qt replies, treats missing/malformed readonly flags as readonly, and associates LUTs using private per-source/filter IDs plus filter type. It finds owned filters after renaming and refuses all unrelated name collisions. Tests cover malformed metadata and callback invalidation/concurrent teardown. Real-camera controller-cancellation test is opt-in and makes no camera writes.

ProRes Proxy/LT/Standard/HQ 10-bit 4:2:2 decoder tests pass with real generated frames. Network ProRes has not been observed; frame recognition is bounded by a validated ProRes header. ZRAW remains blocked on a compatible decoder and verified network framing/sample. The found https://github.com/storyboardcreativity/zraw-decoder-lib project explicitly targets pre-0.94 firmware and has no identified license in its repository API response; it has not been copied or integrated.

Local OBS/LUT acceptance and universal native installation are recorded in docs/obs-local-validation.md.
