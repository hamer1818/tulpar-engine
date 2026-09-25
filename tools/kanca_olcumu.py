#!/usr/bin/env python3
"""Betik kancasi olcumu: kanca basina dagitim suresi + kare basina kalici bellek.

  python3 tools/kanca_olcumu.py [--derleyici yapi/tulpar-motor/tulpar] [--kare 2000] [--tekrar 3] [--varlik 200]
                                [--kare-bellegi-kapali]

tulpar/examples/engine_kanca_olcumu.tpr'yi motoru taniyan derleyiciyle
(tools/motor_derleyici.sh) BIR KEZ derler, sonra ikiliyi pencersiz kosturur.
Oyun her 100 karede "olcu kare K" satiri basar (logla her satirda stdout'u
bosaltir); betik o anda surecin VmRSS'ini okur. Egim, ISINMA karesinden
sonraki orneklere en kucuk kareler dogrusu: kare basina bayt.

ISINMA 700 kare, 300 DEGIL: kopru profiler'i 600 karelik bir halkaya yaziyor
(teng_init, ProfilerConfig::frame_capacity) ve halkanin sayfalari ilk
dokunusta RSS'e giriyor. Olculdu (2026-09-25, RTX 5080): kancasiz kosumda
kare 300..2000 egimi +1.4 KB/kare, kare 500..3000 smaps farki yalniz +192 kB
(anonim bellek duz) — "buyume" halkanin dolmasiydi, sizinti degil.

KONTROL: once ayni ikili KANCASIZ (TULPAR_KANCA_OLCU_N=0) bir kez kosar. Motorun
ve surucunun kanca disi buyumesi o egimdir; kancaya dusen pay = egim - kontrol.
Kontrol olmadan kalan kucuk bir egim "kanca hala sizdiriyor" diye okunurdu.

KARE BELLEGI (#56): engine.tpr kare_basla..kare_bitir'i arena_save/arena_drop
ile sariyor ve kancalar eng_frame_end'in ICINDE kostugu icin, adla cagri yolunun
cagri basina dizgisi de kare sonunda geri veriliyor. Cagri basina AYIRMANIN
kendisini gormek icin --kare-bellegi-kapali (TULPAR_KARE_BELLEK=0): #56 oncesi
her oyunun durumu, bugun de kare disinda (yukleme, iki kare arasi) cagrilan
kancalarin durumu.

Kanca suresi motorun kendi kapanis satirindan okunur ("kapanis (betik
dagitimi): ... cagri basina X ns"): teng_frame_end'in kanca asamalarinin
duvar saati / kare ici cagri sayisi; bos govdeli kancalarla bu dagitimin
kendisidir.

Yalniz Linux (VmRSS /proc'tan). Tulpar'in read_file'i /proc dosyalarini
okuyamiyor (boyut 0 bildiriyorlar), olcu bu yuzden oyunun disinda.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISINMA = 700  # profiler halkasi (600 kare) + pay; yukaridaki nota bak


def vmrss_kb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for satir in f:
                if satir.startswith("VmRSS:"):
                    return int(satir.split()[1])
    except OSError:
        return None
    return None


def egim(ornek):
    """(kare, kB) orneklerine en kucuk kareler: kare basina BAYT."""
    n = len(ornek)
    if n < 2:
        return None
    mx = sum(k for k, _ in ornek) / n
    my = sum(r for _, r in ornek) / n
    pay = sum((k - mx) * (r - my) for k, r in ornek)
    payda = sum((k - mx) ** 2 for k, _ in ornek)
    return pay / payda * 1024.0 if payda else None


def kostur(ikili, kare, cwd, varlik, kb_kapali=False):
    ortam = dict(os.environ, TULPAR_ENGINE_HEADLESS=str(kare), DISPLAY="", TULPAR_KANCA_OLCU_N=str(varlik))
    if kb_kapali:
        ortam["TULPAR_KARE_BELLEK"] = "0"
    p = subprocess.Popen([ikili], cwd=cwd, env=ortam, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    ornek, satirlar = [], []
    for satir in p.stdout:
        satirlar.append(satir.rstrip("\n"))
        m = re.search(r"\[tpr\] olcu kare (\d+)", satir)
        if m:
            r = vmrss_kb(p.pid)
            if r is not None:
                ornek.append((int(m.group(1)), r))
    p.wait()
    metin = "\n".join(satirlar)
    ns = re.search(r"cagri basina ([0-9.]+) ns", metin)
    cagri = re.search(r"kare icinde (\d+) kanca cagrisi", metin)
    p50 = re.search(r"bilgi kapanis: .*?p50 ([0-9.]+) ms", metin)
    hata = re.search(r"\[olcu\] bagli=(\d+) hata=(\d+)", metin)
    sonra = [o for o in ornek if o[0] >= ISINMA]
    return {
        "cikis": p.returncode,
        "ns": float(ns.group(1)) if ns else None,
        "cagri": int(cagri.group(1)) if cagri else None,
        "p50": float(p50.group(1)) if p50 else None,
        "bagli": int(hata.group(1)) if hata else None,
        "hata": int(hata.group(2)) if hata else None,
        "egim": egim(sonra),
        "rss": (sonra[0][1], sonra[-1][1]) if sonra else None,
        "ornek": len(sonra),
        "metin": metin,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--derleyici", default=os.path.join(KOK, "yapi", "tulpar-motor", "tulpar"))
    ap.add_argument("--kare", type=int, default=2000)
    ap.add_argument("--tekrar", type=int, default=3)
    ap.add_argument("--varlik", type=int, default=200)
    ap.add_argument("--kare-bellegi-kapali", action="store_true", help="TULPAR_KARE_BELLEK=0: cagri basina ayirmayi gorunur kilar")
    a = ap.parse_args()
    if not sys.platform.startswith("linux"):
        print("kanca olcumu: ATLANDI (VmRSS yalniz Linux'ta /proc'tan okunuyor)")
        return 0
    if not os.access(a.derleyici, os.X_OK):
        print(f"kanca olcumu: derleyici yok: {a.derleyici} (once tools/motor_derleyici.sh)", file=sys.stderr)
        return 1
    cwd = os.path.join(KOK, "tulpar")
    with tempfile.TemporaryDirectory() as tmp:
        ikili = os.path.join(tmp, "kanca_olcumu")
        d = subprocess.run([a.derleyici, "build", "examples/engine_kanca_olcumu.tpr", ikili], cwd=cwd, capture_output=True, text=True)
        if d.returncode != 0 or not os.path.exists(ikili):
            print("kanca olcumu: derlenemedi", file=sys.stderr)
            print((d.stdout + d.stderr)[-2000:], file=sys.stderr)
            return 1
        print(f"  kare bellegi: {'KAPALI (TULPAR_KARE_BELLEK=0)' if a.kare_bellegi_kapali else 'acik (varsayilan)'}")
        k = kostur(ikili, a.kare, cwd, 0, a.kare_bellegi_kapali)
        if k["cikis"] != 0 or k["egim"] is None or k["hata"] != 0:
            print(f"  kontrol (kancasiz): OLCULEMEDI (cikis {k['cikis']}, hata {k['hata']})")
            print("\n".join(k["metin"].splitlines()[-12:]))
            return 1
        print(f"  kontrol (kancasiz, 0 varlik): VmRSS {k['rss'][0]} -> {k['rss'][1]} kB ({k['ornek']} ornek): kare basina {k['egim']:+.0f} bayt, "
              f"kare p50 {k['p50']:.2f} ms")
        kotu = 0
        for i in range(a.tekrar):
            s = kostur(ikili, a.kare, cwd, a.varlik, a.kare_bellegi_kapali)
            if s["cikis"] != 0 or s["ns"] is None or s["egim"] is None or s["hata"] != 0:
                kotu += 1
                print(f"  kosum {i + 1}: OLCULEMEDI (cikis {s['cikis']}, hata {s['hata']})")
                print("\n".join(s["metin"].splitlines()[-12:]))
                continue
            print(f"  kosum {i + 1}: {s['bagli']} bagli varlik, {s['cagri']} kare ici cagri, kanca basina {s['ns']:.1f} ns, "
                  f"kare p50 {s['p50']:.2f} ms, VmRSS {s['rss'][0]} -> {s['rss'][1]} kB ({s['ornek']} ornek, kare >= {ISINMA}): "
                  f"kare basina {s['egim']:+.0f} bayt, kontrolden fark {s['egim'] - k['egim']:+.0f} bayt "
                  f"(cagri basina {(s['egim'] - k['egim']) * a.kare / s['cagri']:+.1f} bayt)")
    return 1 if kotu else 0


if __name__ == "__main__":
    sys.exit(main())
