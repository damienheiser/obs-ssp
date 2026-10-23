"""Encode and decode real media through obs-ssp, including fractional FPS and 10-bit HEVC."""
from pathlib import Path
import subprocess
import sys
import tempfile
ffmpeg, video_decoder, pcm_decoder = sys.argv[1:]
rates = ("24000/1001", "24", "25", "30000/1001", "50", "60000/1001")
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    pcm = root / "audio.pcm"
    subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i", "sine=sample_rate=48000",
                    "-t", "0.1", "-ac", "2", "-c:a", "pcm_s16le", "-f", "s16le", str(pcm)], check=True)
    subprocess.run([pcm_decoder, str(pcm)], check=True)
    cases = [(codec, depth, rate, 320, 180, 12) for codec, depth in
             (("h264", 8), ("hevc", 8), ("hevc", 10)) for rate in rates]
    cases += [("h264", 8, "30000/1001", 4096, 2160, 4),
              ("hevc", 10, "30000/1001", 4096, 2160, 4)]
    for codec, depth, rate, width, height, count in cases:
        path = root / f"{codec}-{depth}-{rate.replace('/', '-')}-{width}.bin"
        options = (["-x264-params", "threads=2:bframes=2:keyint=10"] if codec == "h264" else
                   ["-x265-params", "pools=none:frame-threads=2:bframes=2:keyint=10:log-level=error"])
        subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                        f"testsrc2=size={width}x{height}:rate={rate}", "-frames:v", str(count),
                        "-pix_fmt", "yuv420p10le" if depth == 10 else "yuv420p",
                        "-c:v", "libx264" if codec == "h264" else "libx265", "-preset", "ultrafast",
                        *options, "-f", codec, str(path)], check=True)
        for hardware in (0, 1):
            result = subprocess.run([video_decoder, codec, str(path), str(width), str(height),
                                     str(count), str(int(depth == 10)), str(hardware), rate])
            if result.returncode == 77:
                print(f"SKIP hardware: {codec}/{depth}/{rate}/{width}", flush=True)
            elif result.returncode:
                print(f"FAIL {codec}/{depth}/{rate}/{width} hardware={hardware} return={result.returncode}", flush=True)
                raise SystemExit(result.returncode)
