# SSP playback and camera settings repair

Mechanism: match the FFmpeg SDK to OBS 32, reject incompatible decoder major versions before allocating decoder objects, timestamp audio by its clock and decoded frame count, and expose camera-advertised resolution/FPS/codec choices. Opening a source and changing playback properties perform no camera writes. An explicit Apply camera settings button is the only camera-format mutation path; Read camera settings refreshes current values. Preserve legacy saved labels through normalization. Codec selection must honor H.264/H.265 and not imply a particular stream.

Authorization: operator requested implementation repair, exact camera terminology, broader Z CAM compatibility, and local validation. Enforcement: runtime ABI guard, camera-supported options, checked HTTP responses and explicit apply. Bounds: no reboot/reset, no credentials, no changes to unrelated OBS sources. SSP and RTSP diagnostics use bounded connections; no stream success is claimed without real frame delivery.

Tests: real FFmpeg AAC decode and incompatible-runtime rejection; pure camera-option/legacy-value and audio-timestamp tests; protocol/connector tests; macOS universal build; live camera configuration readback; sustained OBS playback and RTSP packet/decode checks. Other platforms remain untested unless run.

Recovery: retain prior installed plugin and OBS scene configuration before replacement; revert branch commits for source rollback. Camera recovery snapshot is recorded before any explicit apply. OBS-to-OBS sender work is a separate concern after this repair.

Observed results: six real CTest cases pass, including actual incompatible-runtime rejection, PCM/AAC and 40 H.264/H.265 decode cases with 10-bit HEVC, fractional FPS, C4K and VideoToolbox. Live OBS decoded 4096x2160 H.264/PCM and operator's subsequent format/rate changes beyond600frames with no resampler errors. Camera TCP services recovered after operator power cycle; no firmware changes. Updated Qt requires macOS13; build targets OBS32.2.2. Other hosts/camera models are untested. See HANDOFF for recovery location and live-log evidence.
