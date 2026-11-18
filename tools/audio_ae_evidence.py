#!/usr/bin/env python3
"""Capture before/after callbacks, WAVs, source grains and spectral tables outside git.

Requires numpy. Build both capture binaries with the same harness; override
AUDIO_AE_ENGINE_SOURCE/AUDIO_AE_GAME_SOURCE to the baseline source copies.
Usage: audio_ae_evidence.py ROOT BEFORE_BINARY AFTER_BINARY OUTDIR
"""
import argparse
import csv
import hashlib
import json
import struct
import os
from pathlib import Path
import re
import subprocess
import wave
import numpy as np


def measure(path, rate):
    x = np.fromfile(path, dtype='<i2').astype(float)
    p = abs(np.fft.rfft((x - x.mean()) * np.hanning(len(x)))) ** 2
    hz = np.fft.rfftfreq(len(x), 1 / rate)
    total = max(float(p.sum()), 1e-30)
    bands = [(0, 200), (200, 2000), (2000, 5000), (5000, 10000), (10000, rate / 2 + 1)]
    return [len(x), float(np.sqrt(np.mean(x*x))), float(x.mean()), int(abs(x).max()),
            int(np.count_nonzero((x == 32767) | (x == -32768))),
            float(hz[p.argmax()]), 100 * float(p[hz > 2000].sum()) / total] + [
                100 * float(p[(hz >= lo) & (hz < hi)].sum()) / total for lo, hi in bands]


def write_headers(root, out):
    """Read only the BNKl/PT and Gnsu20 header data; exclude executable bank prefixes."""
    banks, sweeps = {}, {}
    for path in sorted((Path(root) / 'SOUND/ENGINE').glob('*.abk')):
        with path.open('rb') as f:
            f.seek(0x18)
            offset = struct.unpack('<I', f.read(4))[0]
            f.seek(offset)
            bank = f.read()
        count = struct.unpack_from('<H', bank, 6)[0] - 1
        entries = []
        for index in range(count):
            pos = 24 + 4*index
            pos += struct.unpack_from('<I', bank, pos)[0]
            assert bank[pos:pos+4] == b'PT\0\0'
            pos += 4
            tags = {}
            while bank[pos] not in (254, 255):
                tag = bank[pos]
                pos += 1
                if tag == 253:
                    continue
                size = bank[pos]
                pos += 1
                tags[tag] = int.from_bytes(bank[pos:pos+size], 'big')
                pos += size
            entries.append({'index': index, 'rate': tags.get(0x84, 22050),
                            'frames': tags[0x85], 'loop_start': tags.get(0x86),
                            'loop_end': tags.get(0x87), 'level': tags.get(6, 100),
                            'channels': tags.get(0x82, 1), 'codec': tags.get(0x83, 0)})
        banks[path.name] = entries
    for path in sorted((Path(root) / 'SOUND/ENGINE').glob('*.gin')):
        with path.open('rb') as f:
            header = f.read(32)
            assert header[:6] == b'Gnsu20'
            low, high, nr, ng, count, rate = struct.unpack_from('<ffIIII', header, 8)
            rpm_pos = list(struct.unpack('<' + 'I'*(nr+1), f.read(4*(nr+1))))
            grains = list(struct.unpack('<' + 'I'*(ng+1), f.read(4*(ng+1))))
        sweeps[path.name] = {'rpm_min': low, 'rpm_max': high, 'rate': rate,
                             'frames': count, 'rpm_positions': rpm_pos, 'grain_points': grains}
    (out / 'engine-bank-headers.json').write_text(json.dumps(banks, indent=2) + '\n')
    (out / 'engine-sweep-headers.json').write_text(json.dumps(sweeps, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['root', 'before', 'after', 'directory']:
        parser.add_argument(name)
    parser.add_argument('--reuse', action='store_true', help='reuse existing PCM and metadata')
    args = parser.parse_args()
    root, before, after = args.root, args.before, args.after
    out = Path(args.directory)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    env = dict(os.environ, SDL_AUDIODRIVER='dummy')
    lock = os.environ.get('OPENUG2_HEAVY_LOCK')
    prefix = ['flock', lock, 'nice', '-n', '10'] if lock else []

    def capture(binary, tag, mode, subject, value, rate=44100):
        path = out / (tag + '.pcm')
        metadata = out / (tag + '.txt')
        if args.reuse and path.exists() and metadata.exists():
            details = metadata.read_text()
        else:
            result = subprocess.run(prefix + [binary, root, mode, subject, str(value), str(path)],
                                    env=env, check=True, capture_output=True, text=True)
            details = result.stderr
            metadata.write_text(details)
        if mode == 'source':
            rate = int(re.search(r'rate=(\d+)', details)[1])
        elif mode in ['grain', 'coast-grain']:
            rate = int(re.search(r'sweep rate=(\d+)', details)[1])
        with wave.open(str(out / (tag + '.wav')), 'wb') as wav:
            wav.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
            wav.writeframes(path.read_bytes())
        rows.append([tag, rate] + measure(path, rate))

    for label, binary in [('before', before), ('after', after)]:
        for car in ['GOLF', '240SX', '350Z', 'HUMMER']:
            for mode in ['engine', 'coast']:
                for rpm in [1000, 3000, 6000, 8000]:
                    capture(binary, f'{label}-{mode}-{car}-{rpm}', mode, car, rpm)
        for hit in ['.05', '.2', '.45']:
            for repeat in [0, 1]:
                capture(binary, f'{label}-crash-{hit}-{repeat}', 'crash', hit, repeat)
        for repeat in [0, 1]:
            capture(binary, f'{label}-wall-20-{repeat}', 'wall', '20', repeat)
    for rpm in [1000, 3000, 6000, 8000]:
        capture(after, f'source-GOLF-grain-{rpm}', 'grain', 'GOLF', rpm)
    for car in ['GOLF', '240SX', '350Z']:
        for rpm in [1000, 3000, 6000, 8000]:
            capture(after, f'source-{car}-coast-grain-{rpm}', 'coast-grain', car, rpm)
    # Every collision sample, including the ones selected by the fixed-seed cases.
    for index in range(94):
        capture(after, f'source-crash-{index:02}', 'source',
                'SOUND/IG_GLOBAL/MB_Stich_Collision.abk', index)
    with (out / 'spectra-rms.csv').open('w') as f:
        writer = csv.writer(f)
        writer.writerow(['case', 'rate', 'frames', 'rms_s16', 'dc_s16', 'peak_s16',
                         'rail_frames', 'peak_hz', 'above_2khz_percent', '0_200hz_percent',
                         '200_2000hz_percent', '2_5khz_percent', '5_10khz_percent', 'above_10khz_percent'])
        writer.writerows(rows)
    by_name = {row[0]: row for row in rows}
    report = ["# Audio measurements", "", "Mono s16; output 44100 Hz. Engine captures use full master after warmup; engine=full load, coast=closed throttle.",
              "Hann FFT, DC removed; RMS includes DC. Repeated crashes every 8192 frames.",
              "Wall cases use continuous 20 m/s inward contact or one contact followed by separation.", "",
              "| Case | Before RMS | After RMS | Before >2 kHz % | After >2 kHz % | Before/after rail frames |",
              "|---|---:|---:|---:|---:|---:|"]
    for row in rows:
        if not row[0].startswith('before-'):
            continue
        case = row[0][7:]
        a, b = row, by_name['after-' + case]
        report.append(f'|{case}|{a[3]:.3f}|{b[3]:.3f}|{a[8]:.5f}|{b[8]:.5f}|{a[6]}/{b[6]}|')
    for car in ['240SX', '350Z', 'HUMMER']:
        for rpm in [1000, 3000, 6000, 8000]:
            a = (out / f'before-engine-{car}-{rpm}.pcm').read_bytes()
            b = (out / f'after-engine-{car}-{rpm}.pcm').read_bytes()
            if a != b:
                raise AssertionError(f'Unchanged engine control differs: {car} {rpm}')
    for rpm in [1000, 3000, 6000, 8000]:
        assert (out / f'before-coast-HUMMER-{rpm}.pcm').read_bytes() == (out / f'after-coast-HUMMER-{rpm}.pcm').read_bytes()
    report += ['', 'PASS: 240SX, 350Z and HUMMER full-load captures are byte-identical before/after at all four RPM points; HUMMER coast is also byte-identical.',
               'Source collision rows cover all 94 bank entries; source grain rows include exact loop points and recorded RPM in companion text files.']
    (out / 'MEASUREMENTS.md').write_text('\n'.join(report) + '\n')
    write_headers(root, out)
    manifest = {Path(binary).name: hashlib.sha256(Path(binary).read_bytes()).hexdigest()
                for binary in [before, after]}
    (out / 'capture-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'PASS: {len(rows)} WAVs and spectra/RMS rows in {out}')


if __name__ == '__main__':
    main()
