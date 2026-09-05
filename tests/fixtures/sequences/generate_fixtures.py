"""Deterministic evaluation-only P5 fixtures. Run this file to reproduce bytes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def pgm(directory, filename, maximum, width, height, samples):
    assert len(samples) == width * height
    assert all(0 <= sample <= maximum for sample in samples)
    target = ROOT / directory / filename
    target.parent.mkdir(parents=True, exist_ok=True)
    header = f"P5\n# Synthetic evaluation fixture; no patient data\n{width} {height}\n{maximum}\n"
    size = 1 if maximum <= 255 else 2
    target.write_bytes(header.encode("ascii") + b"".join(
        sample.to_bytes(size, "big") for sample in samples))


pgm("ramp16", "frame0001.pgm", 4095, 3, 2,
    [0x0123, 0x0ABC, 0x0FFF, 0x0010, 0x0020, 0x0030])
pgm("ramp16", "frame0002.pgm", 4095, 3, 2,
    [0x0456, 0x0789, 0x0000, 0x0040, 0x0050, 0x0060])
for maximum in [255, 1023, 4095, 65535, 1000]:
    pgm(f"max{maximum}", "frame.pgm", maximum, 3, 1, [0, 35, maximum])
