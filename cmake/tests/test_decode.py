"""Decode real generated AAC through the plugin decoder, without a camera."""
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory() as directory:
    audio = Path(directory) / "audio.aac"
    subprocess.run([sys.argv[2], "-v", "error", "-f", "lavfi", "-i",
                    "sine=frequency=1000:sample_rate=48000", "-t", "1", "-ac", "2",
                    "-c:a", "aac", "-f", "adts", str(audio)], check=True)
    subprocess.run([sys.argv[1], str(audio)], check=True)
