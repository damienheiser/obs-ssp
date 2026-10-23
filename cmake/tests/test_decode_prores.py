from pathlib import Path
import subprocess
import sys
import tempfile
ffmpeg, decoder = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    for profile in range(4):
        path = Path(directory) / f"prores-{profile}.bin"
        subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=320x180:rate=30000/1001", "-frames:v", "4",
                        "-c:v", "prores_ks", "-threads", "2", "-profile:v", str(profile),
                        "-pix_fmt", "yuv422p10le", "-f", "image2pipe", str(path)], check=True)
        subprocess.run([decoder, str(path)], check=True)
