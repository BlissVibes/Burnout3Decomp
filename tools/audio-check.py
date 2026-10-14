#!/usr/bin/env python3
"""Measures a 16-bit PCM WAV recording of the emulator's audio: what can be said without ears.

usage: audio-check.py <file.wav> [--from SECONDS] [--to SECONDS] [--window SECONDS]
                      [--spectrogram OUT.png] [--batch N] [--json]

Prints, for the chosen span: duration, peak and RMS level per channel in dBFS, DC offset, the share
of samples at full scale (clipping), silent stretches (both channels at or below +-1 for 50 ms or
more), left/right correlation, where the spectrum's energy sits, a per-window level table, and
clicks: isolated steps, one sample-to-sample jump far above the jumps on both sides of it.
With --batch N it also compares the jumps at every Nth sample boundary with the jumps elsewhere,
which shows a mixer or ring that glitches once per batch.

The numbers say whether there is sound, how loud, whether it clips, drops out or crackles. They do
not say whether it is the right sound. Needs numpy. The recording is game audio: keep it and the
spectrogram outside the repository.
"""
import json
import struct
import sys
import zlib

import numpy as np

SILENCE_LEVEL = 1          # |sample| at or below this counts as silence
SILENCE_SECONDS = 0.05     # shortest stretch reported as a silent gap
CLICK_FACTOR = 8.0         # a jump this many times the median of the jumps around it...
CLICK_FLOOR = 3000         # ...and at least this large is a click
CLICK_NEIGHBOURS = 6       # jumps looked at on each side


def read_wav(path):
    """(rate, int16 array of shape (frames, channels)) for a PCM 16-bit WAV."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('not a RIFF/WAVE file')
    at, fmt, pcm = 12, None, None
    while at + 8 <= len(data):
        tag, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        body = data[at + 8:at + 8 + size]
        if tag == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', body)
        elif tag == b'data':
            pcm = body
        at += 8 + size + (size & 1)
    if fmt is None or pcm is None:
        raise ValueError('no fmt or data chunk')
    kind, channels, rate, _, _, bits = fmt
    if kind != 1 or bits != 16:
        raise ValueError(f'only 16-bit PCM is handled (format {kind}, {bits} bits)')
    frames = len(pcm) // (2 * channels)
    return rate, np.frombuffer(pcm, dtype='<i2', count=frames * channels).reshape(frames, channels)


def dbfs(value):
    return -120.0 if value <= 0 else 20.0 * np.log10(value / 32768.0)


def silent_gaps(samples, rate):
    """[(start seconds, length seconds)] of stretches where every channel is at or below the silence level."""
    quiet = (np.abs(samples.astype(np.int32)) <= SILENCE_LEVEL).all(axis=1)
    edges = np.flatnonzero(np.diff(np.concatenate(([False], quiet, [False])).astype(np.int8)))
    starts, ends = edges[0::2], edges[1::2]
    keep = (ends - starts) >= int(SILENCE_SECONDS * rate)
    return [(int(s) / rate, int(e - s) / rate) for s, e in zip(starts[keep], ends[keep])]


def clicks(channel, rate):
    """Sample positions of isolated steps: one jump that dwarfs the jumps on both sides of it.

    A drum hit or any loud high note also has large jumps, but in runs; a dropped or repeated buffer,
    a decoder restart or a bad loop point leaves a single step in an otherwise smooth stretch.
    """
    jump = np.abs(np.diff(channel.astype(np.int32))).astype(np.float64)
    if len(jump) < 2 * CLICK_NEIGHBOURS + 1:
        return np.array([], dtype=np.int64)
    padded = np.pad(jump, CLICK_NEIGHBOURS, mode='edge')
    around = np.stack([padded[CLICK_NEIGHBOURS + d:CLICK_NEIGHBOURS + d + len(jump)] for d in range(-CLICK_NEIGHBOURS, CLICK_NEIGHBOURS + 1) if d != 0])
    local = np.maximum(np.median(around, axis=0), 1.0)
    return np.flatnonzero((jump >= CLICK_FLOOR) & (jump > CLICK_FACTOR * local)) + 1


def batch_ratio(channel, batch):
    """Mean jump across every batch-th sample boundary over the mean jump elsewhere (1.0 = no seam)."""
    jump = np.abs(np.diff(channel.astype(np.int32))).astype(np.float64)
    at_seam = np.zeros(len(jump), dtype=bool)
    at_seam[batch - 1::batch] = True
    inside = jump[~at_seam].mean() if (~at_seam).any() else 0.0
    return float(jump[at_seam].mean() / inside) if inside > 0 else 0.0


def band_shares(mono, rate):
    """Share of the energy in each band, from a magnitude spectrum averaged over the span."""
    size = 4096
    if len(mono) < size:
        return {}
    count = min(len(mono) // size, 2000)
    step = max((len(mono) - size) // max(count - 1, 1), 1)
    window = np.hanning(size)
    power = np.zeros(size // 2 + 1)
    for i in range(count):
        power += np.abs(np.fft.rfft(mono[i * step:i * step + size] * window)) ** 2
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    total = power.sum()
    bands = [(0, 100), (100, 500), (500, 2000), (2000, 6000), (6000, 12000), (12000, rate / 2 + 1)]
    return {f'{lo}-{int(hi)} Hz': float(power[(freqs >= lo) & (freqs < hi)].sum() / total) if total > 0 else 0.0 for lo, hi in bands}


def write_png(path, grey):
    """An 8-bit greyscale PNG from a 2D uint8 array."""
    height, width = grey.shape
    raw = b''.join(b'\x00' + grey[y].tobytes() for y in range(height))

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def spectrogram(mono, rate, path, width=1200, height=400):
    """Time left to right, 0 Hz at the bottom to rate/2 at the top, 90 dB of range, brighter = louder."""
    size = 2048
    if len(mono) < size * 2:
        return False
    step = max((len(mono) - size) // width, 1)
    window = np.hanning(size)
    columns = [np.abs(np.fft.rfft(mono[i * step:i * step + size] * window)) for i in range(min(width, (len(mono) - size) // step))]
    image = np.array(columns).T                              # (bins, time)
    bins = np.linspace(0, image.shape[0] - 1, height).astype(int)
    level = 20.0 * np.log10(np.maximum(image[bins], 1e-3) / (32768.0 * size / 4))
    grey = np.clip((level + 90.0) / 90.0 * 255.0, 0, 255).astype(np.uint8)[::-1]
    write_png(path, np.ascontiguousarray(grey))
    return True


def measure(samples, rate, window_seconds=5.0, batch=0, offset=0.0):
    """Every figure the tool reports, as a dict."""
    frames, channels = samples.shape
    wide = samples.astype(np.float64)
    report = {'rate': rate, 'channels': channels, 'frames': int(frames), 'seconds': frames / rate, 'offset': offset}
    report['peak_dbfs'] = [dbfs(float(np.abs(wide[:, c]).max())) if frames else -120.0 for c in range(channels)]
    report['rms_dbfs'] = [dbfs(float(np.sqrt((wide[:, c] ** 2).mean()))) if frames else -120.0 for c in range(channels)]
    report['dc'] = [float(wide[:, c].mean()) if frames else 0.0 for c in range(channels)]
    # At or within two steps of the 16-bit limits: the SPU2 clamps and then applies the master volume, so a
    # game whose master volume is one step under full (0x3FFF) clips at +32765 and -32766.
    report['clipped_share'] = float((np.abs(samples.astype(np.int32)) >= 32765).mean()) if frames else 0.0
    gaps = silent_gaps(samples, rate)
    report['silent_gaps'] = [(offset + s, n) for s, n in gaps]
    report['silent_share'] = sum(n for _, n in gaps) / (frames / rate) if frames else 0.0
    if channels >= 2 and frames > 1 and wide[:, 0].std() > 0 and wide[:, 1].std() > 0:
        report['left_right_correlation'] = float(np.corrcoef(wide[:, 0], wide[:, 1])[0, 1])
    found = sorted({int(p) for c in range(channels) for p in clicks(samples[:, c], rate)})
    merged = [p for i, p in enumerate(found) if i == 0 or p - found[i - 1] > rate // 100]     # one per 10 ms
    report['clicks'] = len(merged)
    report['click_times'] = [offset + p / rate for p in merged[:40]]
    mono = wide.mean(axis=1)
    report['bands'] = band_shares(mono, rate)
    if batch:
        report['batch_seam_ratio'] = [batch_ratio(samples[:, c], batch) for c in range(channels)]
    size = int(window_seconds * rate)
    table = []
    for start in range(0, frames, size):
        part = wide[start:start + size]
        table.append((offset + start / rate, dbfs(float(np.sqrt((part ** 2).mean()))), dbfs(float(np.abs(part).max()))))
    report['windows'] = table
    return report


def render(report):
    lines = [f'{report["seconds"]:.3f} s, {report["rate"]} Hz, {report["channels"]} channels, {report["frames"]} frames']
    lines.append('peak dBFS ' + ' '.join(f'{v:.1f}' for v in report['peak_dbfs']) + '   rms dBFS ' + ' '.join(f'{v:.1f}' for v in report['rms_dbfs'])
                 + '   dc ' + ' '.join(f'{v:.1f}' for v in report['dc']))
    lines.append(f'clipped {report["clipped_share"] * 100:.4f}% of samples   silent {report["silent_share"] * 100:.1f}% of the time in {len(report["silent_gaps"])} gaps'
                 + (f'   left/right correlation {report["left_right_correlation"]:.3f}' if 'left_right_correlation' in report else ''))
    if report['silent_gaps']:
        lines.append('silent gaps (start s, length s): ' + ', '.join(f'{s:.2f}+{n:.2f}' for s, n in report['silent_gaps'][:30])
                     + (' ...' if len(report['silent_gaps']) > 30 else ''))
    lines.append(f'clicks {report["clicks"]}' + (': at ' + ', '.join(f'{t:.3f}' for t in report['click_times']) if report['clicks'] else ''))
    if report['bands']:
        lines.append('energy by band: ' + ', '.join(f'{name} {share * 100:.1f}%' for name, share in report['bands'].items()))
    if 'batch_seam_ratio' in report:
        lines.append('jump at batch seams over jump elsewhere: ' + ' '.join(f'{v:.3f}' for v in report['batch_seam_ratio']))
    lines.append('window start s, rms dBFS, peak dBFS:')
    lines += [f'  {start:8.1f}  {rms:7.1f}  {peak:7.1f}' for start, rms, peak in report['windows']]
    return '\n'.join(lines)


def main(argv):
    options = {'--from': 0.0, '--to': None, '--window': 5.0, '--spectrogram': None, '--batch': 0}
    paths, as_json, i = [], False, 1
    try:
        while i < len(argv):
            a = argv[i]
            if a == '--json':
                as_json = True
            elif a in options:
                value = argv[i + 1]
                options[a] = value if a == '--spectrogram' else (int(value) if a == '--batch' else float(value))
                i += 1
            elif a.startswith('--'):
                raise ValueError(a)
            else:
                paths.append(a)
            i += 1
        if len(paths) != 1 or options['--window'] <= 0:
            raise ValueError('one file')
    except (ValueError, IndexError):
        print(__doc__.strip().split('\n\n')[1], file=sys.stderr)
        return 2
    try:
        rate, samples = read_wav(paths[0])
    except (OSError, ValueError) as e:
        print(f'audio-check: {e}', file=sys.stderr)
        return 1
    first = int(options['--from'] * rate)
    last = len(samples) if options['--to'] is None else int(options['--to'] * rate)
    samples = samples[first:last]
    report = measure(samples, rate, options['--window'], options['--batch'], first / rate)
    if options['--spectrogram']:
        report['spectrogram'] = spectrogram(samples.astype(np.float64).mean(axis=1), rate, options['--spectrogram'])
    print(json.dumps(report) if as_json else render(report))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
