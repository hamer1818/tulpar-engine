#!/usr/bin/env python3
"""MIT KEMAR "compact" HRIR kumesi -> audio/hrtf_kemar.h (depoya girer; _spv.h gibi).

Kaynak: https://sound.media.mit.edu/resources/KEMAR/compact.zip (Gardner & Martin,
MIT Media Lab, 1994). Lisans (KEMAR.html, 2026-11-04'te okundu): "This data is
Copyright 1994 by the MIT Media Laboratory. It is provided free with no
restrictions on use, provided the authors are cited when the data is used in any
research or commercial application." Atif: audio/hrtf_kemar.h basligi + OKUBENI.

Kume (hrtfdoc.txt): 128 tap, 44.1 kHz, 16 bit, (sol, sag) kulak; kaynak SAG
yarimkurede (azimut 0..180, 0 = on, 90 = tam sag); sol yarimkure simetriyle
(azimut A -> 360 - A, kulaklar yer degistirir). Hoparlor (Optimus Pro 7) tepkisi
duzeltilmis, 26. ornekten itibaren kirpilmis.

Islem: her yanit 48 kHz'e FFT ile yeniden orneklenir (karistiricinin hizi), ilk
TAPS tap tutulur, son 8 tap yarim Hann ile kapanir (kesik yanit tiklamasin).
Tutulan enerji orani basilir ve basliga yazilir (olcum, iddia degil).

  python3 tools/hrtf_kemar.py <compact.zip> [--taps 64] [--out audio/hrtf_kemar.h]
  python3 tools/hrtf_kemar.py <compact.zip> --denetle   # basligin taze oldugunu dogrula
"""
import argparse, hashlib, io, re, sys, wave, zipfile
import numpy as np

KAYNAK_RATE, HEDEF_RATE = 44100, 48000


def oku(zpath):
    raw = open(zpath, "rb").read()
    sha = hashlib.sha256(raw).hexdigest()
    z = zipfile.ZipFile(io.BytesIO(raw))
    pos = {}
    for n in z.namelist():
        m = re.search(r"H(-?\d+)e(\d+)a\.wav$", n)
        if not m:
            continue
        el, az = int(m.group(1)), int(m.group(2))
        with wave.open(io.BytesIO(z.read(n))) as w:
            assert w.getnchannels() == 2 and w.getsampwidth() == 2 and w.getframerate() == KAYNAK_RATE, n
            d = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64) / 32768.0
        pos[(el, az)] = d
    return sha, pos


def yeniden_ornekle(x, n_out):
    X = np.fft.rfft(x)
    n_in = len(x)
    Y = np.zeros(n_out // 2 + 1, dtype=complex)
    k = min(len(X), len(Y))
    Y[:k] = X[:k]
    return np.fft.irfft(Y, n_out) * (n_out / n_in)


def uret(zpath, taps):
    sha, pos = oku(zpath)
    els = sorted({e for e, _ in pos})
    n48 = int(round(128 * HEDEF_RATE / KAYNAK_RATE))
    pencere = np.ones(taps)
    kapan = 8
    pencere[-kapan:] = 0.5 * (1 + np.cos(np.linspace(0, np.pi, kapan)))
    halkalar, veri, oranlar, gecikmeler = [], [], [], []
    for e in els:
        azs = sorted(a for ee, a in pos if ee == e)
        halkalar.append((e, azs))
        for a in azs:
            d = pos[(e, a)]
            kul = []
            gec = []
            for c in range(2):
                y = yeniden_ornekle(d[:, c], n48)
                tam = float(np.sum(y * y))
                # BASLANGIC (ITD): ilk |y| >= tepe*%10 ornegi; filtre ondan 2 tap
                # once baslar, oncesi saf GECIKME olarak saklanir. Uzak kulagin
                # yaniti ITD kadar gec basliyor — kirpma ondan baslasaydi 32 tapta
                # enerjinin TAMAMI kesilirdi (olculdu: en az %0).
                o = int(np.argmax(np.abs(y) >= 0.1 * np.max(np.abs(y))))
                bas = max(0, o - 2)
                t = np.zeros(taps)
                parca = y[bas:bas + taps]
                t[:len(parca)] = parca
                t *= pencere
                oranlar.append(float(np.sum(t * t)) / tam if tam > 0 else 1.0)
                kul.append(t)
                gec.append(bas)
            veri.append(kul)
            gecikmeler.append(gec)
    veri = np.array(veri)  # [n][2][taps]
    tepe = float(np.max(np.abs(veri)))
    olcek = tepe / 32767.0
    q = np.round(veri / olcek).astype(np.int32)
    return sha, els, halkalar, q, olcek, oranlar, np.array(gecikmeler, dtype=np.int32)


def baslik(sha, halkalar, q, olcek, oranlar, taps, gec):
    n = q.shape[0]
    L = []
    L.append("// URETILMIS DOSYA — tools/hrtf_kemar.py (elle duzenleme).")
    L.append("//")
    L.append("// HRIR: MIT KEMAR \"compact\" kumesi — Bill Gardner & Keith Martin, MIT Media Lab")
    L.append("// (1994), https://sound.media.mit.edu/resources/KEMAR.html. Lisans: \"This data is")
    L.append("// Copyright 1994 by the MIT Media Laboratory. It is provided free with no")
    L.append("// restrictions on use, provided the authors are cited when the data is used in")
    L.append("// any research or commercial application.\"")
    L.append(f"// Kaynak compact.zip SHA-256 {sha}")
    L.append(f"// Islem: 44.1 -> 48 kHz (FFT); kulak basina baslangic (|y| >= tepe*%10) - 2 ornek saf gecikme,")
    L.append(f"// ondan {taps} tap, son 8 tap yarim Hann. Tutulan enerji")
    L.append(f"// (kulak basina, {len(oranlar)} yanit): en az {min(oranlar) * 100:.2f}%, ortalama {np.mean(oranlar) * 100:.2f}%.")
    L.append("// Konum: kaynak SAG yarimkurede (azimut 0..180, 0 on, 90 tam sag); sol yarimkure")
    L.append("// simetriyle: azimut A -> 360-A, kulaklar yer degistirir (audio/hrtf.hpp).")
    L.append("#pragma once")
    L.append("#include <cstdint>")
    L.append("")
    L.append("namespace tulpar::engine::audio::hrtf_kemar {")
    L.append(f"constexpr uint32_t kRate = {HEDEF_RATE};")
    L.append(f"constexpr uint32_t kTaps = {taps};")
    L.append(f"constexpr uint32_t kPositions = {n};")
    L.append(f"constexpr uint32_t kRings = {len(halkalar)};")
    L.append(f"constexpr float kScale = {olcek:.9e}f; // int16 -> genlik")
    L.append("struct Ring { int16_t elevation; uint16_t first; uint16_t count; };")
    L.append("constexpr Ring kRingTable[kRings] = {")
    ilk = 0
    for e, azs in halkalar:
        L.append(f"    {{{e}, {ilk}, {len(azs)}}},")
        ilk += len(azs)
    L.append("};")
    L.append("// Konum basina azimut (derece, 0..180), halka sirasiyla.")
    azl = [a for _, azs in halkalar for a in azs]
    L.append("constexpr uint8_t kAzimuth[kPositions] = {")
    for i in range(0, len(azl), 24):
        L.append("    " + ", ".join(str(a) for a in azl[i:i + 24]) + ",")
    L.append("};")
    L.append(f"// [konum][kulak] saf gecikme (ornek, 48 kHz): yanitin baslangicindan once (ITD); en cok {int(gec.max())}.")
    L.append(f"constexpr uint32_t kMaxDelay = {int(gec.max())};")
    L.append("constexpr uint8_t kDelay[kPositions][2] = {")
    for i in range(0, n, 12):
        L.append("    " + ", ".join(f"{{{int(gec[j,0])},{int(gec[j,1])}}}" for j in range(i, min(n, i + 12))) + ",")
    L.append("};")
    L.append("// [konum][kulak 0 = sol, 1 = sag][tap] (gecikmeden SONRA baslar)")
    L.append("constexpr int16_t kHrir[kPositions][2][kTaps] = {")
    for i in range(n):
        L.append("    {{" + ",".join(str(int(v)) for v in q[i, 0]) + "},")
        L.append("     {" + ",".join(str(int(v)) for v in q[i, 1]) + "}},")
    L.append("};")
    L.append("} // namespace tulpar::engine::audio::hrtf_kemar")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("--taps", type=int, default=64)
    ap.add_argument("--out", default="audio/hrtf_kemar.h")
    ap.add_argument("--denetle", action="store_true")
    a = ap.parse_args()
    sha, els, halkalar, q, olcek, oranlar, gec = uret(a.zip, a.taps)
    txt = baslik(sha, halkalar, q, olcek, oranlar, a.taps, gec)
    print(f"hrtf_kemar: {q.shape[0]} konum, {len(halkalar)} halka ({els[0]}..{els[-1]} derece), {a.taps} tap @ 48 kHz; "
          f"tutulan enerji en az {min(oranlar) * 100:.2f}%, ortalama {np.mean(oranlar) * 100:.2f}%; olcek {olcek:.3e}")
    if a.denetle:
        mevcut = open(a.out, encoding="utf-8").read()
        if mevcut != txt:
            print(f"HATA: {a.out} bayat (yeniden uret)")
            return 1
        print(f"{a.out} taze")
        return 0
    open(a.out, "w", encoding="utf-8").write(txt)
    print(f"yazildi: {a.out} ({len(txt) // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
