#!/usr/bin/env python3
"""Arayuz surucusu istemcisi (docs/ARAYUZ-SURUCUSU.md): penceresiz editoru CANLI surer.

  tools/arayuz.py baslat [--size 1600x900] [--dizin D] [-- editor argumanlari...]
  tools/arayuz.py tikla "Dosya"          # her komut bir satir; yaniti basar, HATA'da cikis 1
  tools/arayuz.py ekran menu             # -> <dizin>/menu.png
  tools/arayuz.py ogeler / metinler [suzgec] [pencere=AD]
  tools/arayuz.py bitir

Oturum: $TULPAR_ARAYUZ_DIZIN (yoksa $TMPDIR/tulpar_arayuz). Editor `--surucu-canli <fifo>` ile
penceresiz kosar (pencere ACILMAZ; TULPAR_ENGINE_HEADLESS oyun icin de verilir), komutlar FIFO'dan
gelir, yanitlar <dizin>/yanit.txt'ye `.son <n> tamam|HATA` satiriyla biter.
"""
import os
import shlex
import signal
import subprocess
import sys
import time

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def dizin():
    d = os.environ.get("TULPAR_ARAYUZ_DIZIN")
    if d:
        return d
    return os.path.join(os.environ.get("TMPDIR", "/tmp"), "tulpar_arayuz")


def yol(*p):
    return os.path.join(dizin(), *p)


def canli_mi():
    try:
        pid = int(open(yol("pid")).read())
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError):
        return 0


def yanit_oku():
    try:
        return open(yol("yanit.txt"), encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def gunluk_kuyrugu(n=15):
    try:
        return "".join(open(yol("editor.log"), encoding="utf-8", errors="replace").readlines()[-n:])
    except OSError:
        return "(gunluk yok)"


def baslat(argv):
    if canli_mi():
        print(f"zaten calisiyor (pid {canli_mi()}); once: arayuz.py bitir")
        return 1
    size, ek = "1600x900", []
    i = 0
    while i < len(argv):
        if argv[i] == "--size":
            size = argv[i + 1]; i += 2
        elif argv[i] == "--dizin":
            os.environ["TULPAR_ARAYUZ_DIZIN"] = os.path.abspath(argv[i + 1]); i += 2
        elif argv[i] == "--":
            ek = argv[i + 1:]; break
        else:
            ek.append(argv[i]); i += 1
    os.makedirs(dizin(), exist_ok=True)
    for f in ("komut", "yanit.txt", "pid", "sayac"):
        try:
            os.remove(yol(f))
        except OSError:
            pass
    os.mkfifo(yol("komut"))
    ed = os.environ.get("ED", os.path.join(KOK, "yapi", "engine_editor"))
    env = dict(os.environ)
    env["DISPLAY"] = ""
    env["WAYLAND_DISPLAY"] = ""
    env.setdefault("TULPAR_ENGINE_HEADLESS", "60")   # editorden acilan oyun da penceresiz
    log = open(yol("editor.log"), "w")
    p = subprocess.Popen([ed, "--size", size, "--surucu-canli", yol("komut"), "--surucu-dizin", dizin()] + ek,
                         cwd=KOK, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                         start_new_session=True)
    open(yol("pid"), "w").write(str(p.pid))
    open(yol("sayac"), "w").write("0")
    for _ in range(600):
        if ".hazir" in yanit_oku():
            print(f"hazir: pid {p.pid}, {size}, dizin {dizin()}")
            return 0
        if p.poll() is not None:
            print("HATA: editor acilmadi:\n" + gunluk_kuyrugu())
            return 1
        time.sleep(0.1)
    print("HATA: editor 60 sn'de hazir olmadi:\n" + gunluk_kuyrugu())
    return 1


def gonder(satir, zaman_asimi=180.0):
    pid = canli_mi()
    if not pid:
        print("HATA: surucu calismiyor (arayuz.py baslat)\n" + gunluk_kuyrugu())
        return 1
    n = int(open(yol("sayac")).read() or "0") + 1
    once = len(yanit_oku())
    with open(yol("komut"), "w") as f:   # FIFO: editor okuyana kadar bekler
        f.write(satir.replace("\n", " ") + "\n")
    open(yol("sayac"), "w").write(str(n))
    isaret = f".son {n} "
    t0 = time.time()
    while time.time() - t0 < zaman_asimi:
        y = yanit_oku()
        k = y.find(isaret, once)
        if k >= 0:
            blok = y[once:]
            son = blok.find(isaret)
            satirlar = [s for s in blok[:son].splitlines() if not s.startswith("> ")]
            durum = blok[son:].split("\n", 1)[0].split()[-1]
            print("\n".join(satirlar))
            return 0 if durum == "tamam" else 1
        if not canli_mi():
            print("HATA: editor kapandi\n" + y[once:] + "\n" + gunluk_kuyrugu())
            return 1
        time.sleep(0.03)
    print("HATA: yanit gelmedi (zaman asimi)")
    return 1


def bitir():
    pid = canli_mi()
    if not pid:
        print("calismiyor")
        return 0
    gonder("cik", 30)
    for _ in range(100):
        if not canli_mi():
            print("kapandi")
            return 0
        time.sleep(0.1)
    os.kill(pid, signal.SIGTERM)
    print("kapanmadi, SIGTERM gonderildi")
    return 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    k = sys.argv[1]
    if k == "baslat":
        return baslat(sys.argv[2:])
    if k == "bitir":
        return bitir()
    if k == "gunluk":
        print(gunluk_kuyrugu(60))
        return 0
    # Geri kalan her sey surucu komutu: argumanlar tirnaklanip tek satir yapilir.
    satir = " ".join([k] + [a if (a and " " not in a and '"' not in a) else '"' + a.replace('"', '\\"') + '"' for a in sys.argv[2:]])
    return gonder(satir)


if __name__ == "__main__":
    sys.exit(main())
