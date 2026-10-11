#!/usr/bin/env python3
"""pokeycmp.py -- the firmware's POKEY against MAME's, by ear made numeric.

    pokeycmp.py [--mame build/wav/mame.wav]
    pokeycmp.py --game "/tmp/atari7800/Ballblazer (USA).bin"

Plays testrom/pokeytest's step table through tools/pokeyrender.c (the cart's
own pokey.c) and compares each half-second step with MAME's recording of the
same client on the FujiNet cart (its POKEY at $0450, the TIA silent): the
dominant pitch must agree within 2%, and the step's loudness relative to the
first step within 25% (50% with a high-pass filter on). Noise steps compare
spectral centroids instead.
Records MAME first if the WAV is missing.

--game logs a POKEY game's register writes in MAME (emu/pokeylog.lua) and
renders them the same way, then compares 100 ms windows with MAME's recording
of the game from the first one our POKEY sounds in (the TIA may play before
it): the loudness envelopes must correlate at 0.9 or better, and the median
band-spectrum similarity must be 0.9 or better.
"""

import argparse
import os
import re
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(HERE, "build")
MAME = os.environ.get("MAME", os.path.expanduser("~/Workspace/mame-a7800"))
ROMS = os.environ.get("ROMS", os.path.expanduser("~/Workspace/mame/roms"))
CLOCK = 1789773
FRAME = CLOCK / 59.92                  # clocks in an NTSC 7800 frame
STEP_FRAMES = 30


def steps_from_source():
    src = open(os.path.join(HERE, "testrom", "pokeytest.s")).read()
    body = src.split("steps:", 1)[1].split("NSTEPS", 1)[0]
    rows = []
    for line in body.splitlines():
        m = re.match(r"\s*\.byte\s+(.*?)(;.*)?$", line)
        if m:
            v = [int(x.strip().lstrip("$"), 16) for x in m.group(1).split(",")]
            rows.append((v[:5], (m.group(2) or "").lstrip("; ")))
    return rows


def record(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    subprocess.run(["./build.sh", "pokeytest"], cwd=HERE, check=True, capture_output=True)
    subprocess.run(["./a7800", "a7800", "-rompath", ROMS, "-cartslot", "fujinet",
                    "-cart", os.path.join(B, "pokeytest.a78"), "-video", "none", "-nothrottle",
                    "-seconds_to_run", "12", "-wavwrite", path, "-samplerate", "48000"],
                   cwd=MAME, check=True, capture_output=True)


def read_wav(path):
    w = wave.open(path)
    n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, rate


def render(rows):
    exe = os.path.join(B, "pokeyrender")
    subprocess.run(["cc", "-O2", "-I", os.path.join(HERE, "firmware", "include"), "-o", exe,
                    os.path.join(HERE, "tools", "pokeyrender.c"),
                    os.path.join(HERE, "firmware", "src", "pokey.c")], check=True)
    lines = ["0 f 0", "0 f 3"]
    t = 0.0
    for v, _ in rows:
        c = int(t)
        for reg, val in zip((8, 0, 1, 2, 3), v):
            lines.append("%d %x %x" % (c, reg, val))
        t += STEP_FRAMES * FRAME
    lines.append("%d 1 0" % int(t))
    out = subprocess.run([exe], input="\n".join(lines).encode(), capture_output=True).stdout
    return np.frombuffer(out, dtype=np.int16).astype(np.float64), CLOCK / 57


def features(seg, rate):
    seg = seg - seg.mean()
    rms = float(np.sqrt((seg ** 2).mean()))
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / rate)
    spec[freqs < 20] = 0
    peak = float(freqs[int(spec.argmax())])
    centroid = float((freqs * spec).sum() / max(spec.sum(), 1e-9))
    return rms, peak, centroid


def segments(audio, rate, start, n):
    step = STEP_FRAMES * FRAME / CLOCK
    out = []
    for k in range(n):
        a = int((start + k * step + 0.1) * rate)
        b = int((start + (k + 1) * step - 0.1) * rate)
        out.append(audio[a:b])
    return out


def game(image, secs=30):
    name = re.sub(r"\W+", "_", os.path.splitext(os.path.basename(image))[0]).strip("_")
    wav, pk = (os.path.join(B, "wav", name + ext) for ext in (".wav", ".pk"))
    os.makedirs(os.path.dirname(wav), exist_ok=True)
    subprocess.run(["./build.sh", "hello"], cwd=HERE, check=True, capture_output=True)
    run = ["./a7800", "a7800", "-rompath", ROMS, "-cartslot", "fujinet",
           "-cart", os.path.join(B, "hello.a78"), "-video", "none", "-nothrottle"]
    env = dict(os.environ, FUJINET_IMAGE=image, POKEYLOG_SECS=str(secs),
               A78_EMU_DIR=os.path.join(HERE, "emu"))
    log = subprocess.run(run + ["-sound", "none", "-autoboot_script",
                                os.path.join(HERE, "emu", "pokeylog.lua")],
                         cwd=MAME, env=env, check=True, capture_output=True, text=True).stdout
    with open(pk, "w") as f:
        f.writelines(l[3:] + "\n" for l in log.splitlines() if l.startswith("PK "))
    subprocess.run(run + ["-seconds_to_run", str(secs), "-wavwrite", wav, "-samplerate", "48000"],
                   cwd=MAME, env=env, check=True, capture_output=True)
    out = subprocess.run([os.path.join(B, "pokeyrender")], stdin=open(pk, "rb"),
                         capture_output=True, check=True).stdout
    ours, orate = np.frombuffer(out, dtype=np.int16).astype(np.float64), CLOCK / 57
    mame, mrate = read_wav(wav)
    edges = np.geomspace(40, 8000, 49)

    def bands(seg, rate):
        seg = seg - seg.mean()
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1.0 / rate)
        return np.array([spec[(f >= lo) & (f < hi)].sum() for lo, hi in zip(edges, edges[1:])])

    eo, em, sim = [], [], []
    for k in range(int(min(len(ours) / orate, len(mame) / mrate) / 0.1)):
        o = ours[int(k * 0.1 * orate):int((k + 1) * 0.1 * orate)]
        m = mame[int(k * 0.1 * mrate):int((k + 1) * 0.1 * mrate)]
        ro, rm = features(o, orate)[0], features(m, mrate)[0]
        if not eo and ro <= 200:
            continue
        eo.append(ro)
        em.append(rm)
        if ro > 200 and rm > 200:
            x, y = bands(o, orate), bands(m, mrate)
            sim.append(float(x @ y / max(np.linalg.norm(x) * np.linalg.norm(y), 1e-9)))
    if len(sim) < 10:
        print("pokeycmp: %s: too little POKEY sound to compare" % name)
        return 1
    corr, med = float(np.corrcoef(eo, em)[0, 1]), float(np.median(sim))
    ok = corr >= 0.9 and med >= 0.9
    print("pokeycmp: %s %s: %.1f s compared, loudness correlation %.2f, spectrum similarity "
          "%.2f" % ("ok" if ok else "BAD", name, len(eo) * 0.1, corr, med))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mame", default=os.path.join(B, "wav", "mame.wav"))
    ap.add_argument("--game", action="append", default=[], help="a POKEY game image")
    a = ap.parse_args()
    if a.game:
        subprocess.run(["cc", "-O2", "-I", os.path.join(HERE, "firmware", "include"), "-o",
                        os.path.join(B, "pokeyrender"), os.path.join(HERE, "tools", "pokeyrender.c"),
                        os.path.join(HERE, "firmware", "src", "pokey.c")], check=True)
        return max(game(g) for g in a.game)
    if not os.path.exists(a.mame):
        record(a.mame)
    rows = steps_from_source()
    mame, mrate = read_wav(a.mame)
    ours, orate = render(rows)
    # MAME's recording starts at power-on: the first sound is step 0
    loud = np.abs(mame - np.median(mame)) > 200
    start = int(np.argmax(loud)) / mrate
    ms = [features(s, mrate) for s in segments(mame, mrate, start, len(rows))]
    os_ = [features(s, orate) for s in segments(ours, orate, 0.0, len(rows))]
    bad = 0
    for k, ((v, label), m, o) in enumerate(zip(rows, ms, os_)):
        noise = (v[2] & 0xE0) in (0x80, 0xC0, 0x00, 0x40) and (v[2] & 0x10) == 0
        vol_only = (v[2] | v[4]) & 0x10
        rel_m = m[0] / max(ms[0][0], 1e-9)
        rel_o = o[0] / max(os_[0][0], 1e-9)
        if vol_only:
            ok = True                   # a constant level: nothing to hear
            what = "level"
        elif noise:
            ok = abs(m[2] - o[2]) <= 0.15 * max(m[2], 1)
            what = "centroid %.0f/%.0f Hz" % (m[2], o[2])
        else:
            ok = abs(m[1] - o[1]) <= 0.02 * max(m[1], 1) + 2
            what = "pitch %.1f/%.1f Hz" % (m[1], o[1])
        # MAME's output stage is a resistor network (pokey.cpp vol_init), ours
        # a linear sum: they part most where the high-pass chops the wave
        tol = 0.5 if v[0] & 0x06 else 0.25
        loud_ok = vol_only or abs(rel_m - rel_o) <= tol * max(rel_m, 0.05)
        ok = ok and loud_ok
        bad += not ok
        print("%2d %-4s %-34s %-28s level %.2f/%.2f" % (k, "ok" if ok else "BAD", label, what,
                                                        rel_m, rel_o))
    print("pokeycmp: %d of %d steps agree with MAME" % (len(rows) - bad, len(rows)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
