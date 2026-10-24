# OBS SSP peer protocol version 1

This is an explicit authenticated extension for application peers. It is not the legacy camera authentication or selector protocol. Implementation: `src/ssp-peer.cpp` at cc50f29 and descendants. Do not advertise support until an actual implementation interoperates. The application marker below is a protocol discriminator in v1, not a claim about the producer executable.

Every TCP frame starts with a big-endian unsigned 32-bit length, followed by one type byte and that many minus one body bytes. Length includes the type, excludes the length prefix, and is at most 16 MiB. No media encryption is provided: use a trusted LAN or VPN. The shared key is 32 securely generated random bytes, entered as 64 hexadecimal digits. It is separate from OBS WebSocket authentication.

1. Server sends INIT type `0x64`: ASCII `OSSP`, BE32 version 1, 32 cryptographically random nonce bytes (40 body bytes).
2. Client sends AUTH `0xc8`: HMAC-SHA256(key, `obs-ssp/client/v1` followed by one NUL byte followed by nonce). Exactly 32 body bytes.
3. Server verifies in constant time and returns ACK `0x65`: HMAC-SHA256(key, `obs-ssp/server/v1` followed by one NUL byte followed by nonce). Client verifies before accepting a manifest.
4. Derive direction keys with HMAC-SHA256(key, ASCII `obs-ssp/media/server/v1` followed by nonce), and similarly `obs-ssp/media/client/v1`. These two labels have **no** terminating NUL.
5. Every subsequent frame body is BE64 sequence, application payload, 32-byte HMAC-SHA256(direction key, type byte followed by BE64 sequence followed by application payload). Each direction starts at zero, increments strictly, rejects replay/tamper, and must close before sequence exhaustion.

The first protected server message is manifest `0x6e`, UTF-8 JSON:

```json
{"application":"obs-ssp","schema_version":1,"tracks":[
 {"id":"video/0","index":0,"kind":"video","codec":"h264","config":"BASE64","width":1920,"height":1080,"fps_num":30000,"fps_den":1001},
 {"id":"audio/0","index":0,"kind":"audio","codec":"aac","config":"BASE64","sample_rate":48000,"channels":2}
]}
```

Codec configuration is the encoder's actual extradata, base64 encoded, maximum 64 KiB per track. Manifest maximum 2 MiB. IDs must exactly match kind/index and be unique. Maximum 10 video indices 0–9 and 6 audio indices 0–5. Supported video identifiers are `h264`, `hevc`, `prores`; audio `aac`, `pcm_s16le`. Regular ProRes decoding exists; network ProRes interoperability is not yet qualified. ZRaw and uncompressed RAW have no identifier or packing contract in v1. Never label opaque RAW bytes as an implemented video codec.

Video `0x6f` and audio `0x70` application payload: BE32 track index, one byte keyframe flag (0 or 1), BE64 signed PTS in microseconds, BE64 signed DTS in microseconds, opaque encoded packet bytes. No per-packet codec re-encoding or ADTS insertion is performed. AAC uses actual configuration plus raw AAC packets when supplied by the encoder. All tracks share the sender timestamp epoch. Negative preroll is bounded to 10 seconds; maximum timestamp is 10 years. Receiver schedules a shared local clock with 120 ms initial offset. New clients wait for a keyframe independently for each video track; audio is delivered immediately.

Client heartbeat `0xcb` has an empty application payload and the same protection/sequence rules. Client sends every 3 seconds. Authentication deadline 5 seconds; idle deadline 15 seconds. Server allows at most four pending/authenticated sessions. Global publish queue is bounded to 512 packets or 32 MiB; per-client queued socket writes to 16 MiB. Overflow fails the output or disconnects the lagging client rather than growing indefinitely.

Dimensions are positive, at most 8192 per axis and 33,554,432 pixels. Rational frame rate numerator/denominator are positive and at most 1,000,000. Audio is 8–192 kHz, 1–8 channels. These are parser bounds, not promises that hardware supports every combination. Availability is determined from actual sender encoders and authenticated manifests. Camera controls and firmware capability discovery remain a separate contract.

Verification: production loopback client/server, independent tamper/replay/oversize/quota/backpressure tests, and real desktop OBS two-video/two-audio plus late-join acceptance. Cross-application and cross-host interoperability remain untested. Recovery: stop the output, close receivers, restore the previous plugin bundle; no camera mutation is required.
