#!/usr/bin/env python3
"""Prepares the footstep and landing foley of the character sample, apps/samples/character (issue #506).

  python3 tools/prepare_character_foley.py <downloads>

<downloads> is a folder holding the CC0 downloads listed in apps/samples/character/LICENSES.md, extracted
anywhere below it; a source is found by its file name. Each one is mixed to mono, high-passed at 40 Hz, trimmed
to its step, faded, brought to one loudness per kind (footsteps, landings) and written as 16-bit mono FLAC at its
own sample rate, with a sidecar whose identity is derived from the file's path, the way
tools/generate_character_level.py derives the course's. Nothing is pitch-shifted or time-stretched: the
character varies pitch and volume per step at run time. The outputs are committed, so only someone replacing a
source runs this. Needs numpy, scipy, soundfile and pyloudnorm (`pip install numpy scipy soundfile pyloudnorm`).

Names: `footstep_<surface>_<foot>_<nn>.flac` and `land_<surface>.flac` under assets/audio/footsteps, with
<surface> one of `concrete`, `metal`, `wood` and <foot> `left` or `right`.

Loudness is ITU-R BS.1770 (K-weighted, gated) measured over 100 ms blocks rather than 400 ms ones, since a step
is shorter than one 400 ms block. A clip whose first transient would pass the peak ceiling at its kind's
loudness has that transient limited (3 ms look-ahead, no release tail); the gain reduction is printed.
Re-running on the same downloads writes the same files.
"""
import json
import sys
import zlib
from pathlib import Path

import numpy as np
import pyloudnorm
import soundfile
from scipy import ndimage, signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_character_level import PROJECT, asset_uuid  # noqa: E402

OUTPUT = "assets/audio/footsteps"

# Loudness in LUFS over 100 ms blocks. A landing is both feet and the body's weight, so it is louder.
LOUDNESS = {"step": -22.0, "land": -18.0}
PEAK_CEILING = -1.0  # dBFS
LAYER_OFFSET = 0.012  # seconds between the feet of a landing made of two steps

# (output name, source file name). Sources without a side are assigned one alternately.
SOURCES = [
    ("footstep_concrete_left_01", "Fantozzi-StoneL1.flac"),
    ("footstep_concrete_left_02", "Fantozzi-StoneL2.flac"),
    ("footstep_concrete_left_03", "Fantozzi-StoneL3.flac"),
    ("footstep_concrete_right_01", "Fantozzi-StoneR1.flac"),
    ("footstep_concrete_right_02", "Fantozzi-StoneR2.flac"),
    ("footstep_concrete_right_03", "Fantozzi-StoneR3.flac"),
    ("footstep_metal_left_01", "alt-toasterstep1.wav"),
    ("footstep_metal_right_01", "alt-toasterstep2.wav"),
    ("footstep_metal_left_02", "alt-toasterstep3.wav"),
    ("footstep_metal_right_02", "alt-toasterstep4.wav"),
    ("footstep_wood_left_01", "sfx100v2_footstep_wood_01.ogg"),
    ("footstep_wood_right_01", "sfx100v2_footstep_wood_02.ogg"),
    ("footstep_wood_left_02", "sfx100v2_footstep_wood_03.ogg"),
    ("footstep_wood_right_02", "sfx100v2_footstep_wood_04.ogg"),
    # No recorded landing on concrete that was CC0 and downloadable: both feet of the concrete steps, 12 ms apart.
    ("land_concrete", ("Fantozzi-StoneL1.flac", "Fantozzi-StoneR2.flac")),
    ("land_metal", "toasterstep2.wav"),
    ("land_wood", "sfx100v2_wood_hit_01.ogg"),
]


def find(downloads: Path, name: str) -> Path:
    found = sorted(downloads.rglob(name))
    if not found:
        sys.exit(f"{name} not found under {downloads}")
    return found[0]


def to_mono(data: np.ndarray) -> tuple[np.ndarray, str]:
    """The channels' mean when they agree; the louder channel when they are out of phase."""
    if data.shape[1] == 1:
        return data[:, 0], "mono"
    left, right = data[:, 0], data[:, 1]
    correlation = float(np.corrcoef(left, right)[0, 1])
    if correlation > 0.3:
        return (left + right) / 2, f"mean of channels (correlation {correlation:.2f})"
    louder = 0 if np.sum(left ** 2) >= np.sum(right ** 2) else 1
    return data[:, louder], f"{'left' if louder == 0 else 'right'} channel (correlation {correlation:.2f})"


def envelope(x: np.ndarray, rate: int) -> np.ndarray:
    """RMS over 2 ms windows, one value per sample."""
    width = max(1, int(rate * 0.002))
    return np.sqrt(np.convolve(x ** 2, np.ones(width) / width, mode="same"))


def trim(x: np.ndarray, rate: int) -> np.ndarray:
    """From 3 ms before the step passes -30 dB of its peak to 10 ms after it falls under -50 dB (or the noise floor)."""
    env = envelope(x, rate)
    top = env.max()
    start = int(np.argmax(env > top * 10 ** (-30 / 20)))
    start = max(0, start - int(rate * 0.003))
    floor = float(np.percentile(env, 10))
    threshold = max(top * 10 ** (-50 / 20), floor * 2.0)
    end = len(x) - int(np.argmax(env[::-1] > threshold))
    end = min(len(x), end + int(rate * 0.010))
    y = x[start:end].copy()
    fade_in = int(rate * 0.002)
    fade_out = min(int(rate * 0.030), len(y) // 3)
    y[:fade_in] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, fade_in))
    y[len(y) - fade_out:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, fade_out))
    return y


def loudness(x: np.ndarray, rate: int) -> float:
    meter = pyloudnorm.Meter(rate, block_size=0.100)
    # pyloudnorm needs at least one block; a step shorter than that is padded with silence for the measure.
    padded = np.pad(x, (0, max(0, int(rate * 0.100) - len(x))))
    return float(meter.integrated_loudness(padded))


def normalise(x: np.ndarray, rate: int, target: float) -> np.ndarray:
    # Repeated: the absolute gate at -70 LUFS lets more of a quiet recording's tail in once it is louder.
    for _ in range(3):
        x = x * 10 ** ((target - loudness(x, rate)) / 20)
    return x


def limit(x: np.ndarray, rate: int, ceiling: float) -> tuple[np.ndarray, float]:
    """A look-ahead peak limiter: the gain each sample needs, its minimum over 3 ms, smoothed over 1.5 ms."""
    need = np.minimum(1.0, ceiling / np.maximum(np.abs(x), 1e-12))
    if need.min() >= 1.0:
        return x, 0.0
    width = max(1, int(rate * 0.0015))
    gain = ndimage.minimum_filter1d(need, size=2 * width + 1, mode="nearest")
    window = np.hanning(width + 2)[1:-1]
    gain = np.convolve(np.pad(gain, width, mode="edge"), window / window.sum(), mode="same")[width:-width]
    gain = np.minimum(gain, need)
    return x * gain, -db(gain.min())


def db(value: float) -> float:
    return 20 * np.log10(max(value, 1e-12))


def prepare(downloads: Path):
    out = PROJECT / OUTPUT
    out.mkdir(parents=True, exist_ok=True)
    ceiling = 10 ** (PEAK_CEILING / 20)
    rows = []
    for name, sources in SOURCES:
        layers = []
        for source in (sources,) if isinstance(sources, str) else sources:
            data, rate = soundfile.read(find(downloads, source), dtype="float64", always_2d=True)
            x, mixdown = to_mono(data)
            x = signal.sosfiltfilt(signal.butter(2, 40, "highpass", fs=rate, output="sos"), x - x.mean())
            layers.append(trim(x, rate))
        offset = int(rate * LAYER_OFFSET)
        x = np.zeros(max(len(layer) + i * offset for i, layer in enumerate(layers)))
        for i, layer in enumerate(layers):
            x[i * offset:i * offset + len(layer)] += layer
        source = " + ".join((sources,) if isinstance(sources, str) else sources)
        target = LOUDNESS["land" if name.startswith("land_") else "step"]
        reduction = 0.0
        for _ in range(4):  # limiting lowers the loudness a little, so normalise and limit again
            x = normalise(x, rate, target)
            x, step = limit(x, rate, ceiling * 10 ** (-0.05 / 20))
            reduction = max(reduction, step)
        # Triangular dither to 16 bits, seeded by the name so re-runs write the same bytes.
        rng = np.random.default_rng(zlib.crc32(name.encode()))
        lsb = 1 / 32768
        x = np.clip(x + (rng.random(len(x)) - rng.random(len(x))) * lsb, -ceiling, ceiling)
        relative = f"{OUTPUT}/{name}.flac"
        soundfile.write(PROJECT / relative, x, rate, subtype="PCM_16", format="FLAC")
        written, _ = soundfile.read(PROJECT / relative, dtype="float64")
        rows.append((name, source, mixdown, rate, len(written) / rate, loudness(written, rate),
                     db(np.abs(written).max()), db(np.sqrt(np.mean(written ** 2))), reduction,
                     (PROJECT / relative).stat().st_size))
        sidecar = PROJECT / (relative + ".meta")
        if not sidecar.exists():
            document = {"settings": {}, "type": "Sound", "uuid": asset_uuid(relative), "version": 2}
            sidecar.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    print("| File | Source | Rate | Length | Loudness | Peak | RMS | Limited | Size |")
    print("|---|---|---|---|---|---|---|---|---|")
    for name, source, mixdown, rate, length, lufs, peak, rms, reduction, size in rows:
        print(f"| `{name}.flac` | `{source}` | {rate} Hz | {length * 1000:.0f} ms | {lufs:.1f} LUFS | "
              f"{peak:.1f} dBFS | {rms:.1f} dBFS | {reduction:.1f} dB | {size / 1024:.1f} KiB |")
    for name, source, mixdown, *_ in rows:
        print(f"{name}: {mixdown}")
    print(f"total {sum(r[-1] for r in rows) / 1024:.1f} KiB")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    prepare(Path(sys.argv[1]))


if __name__ == "__main__":
    main()
