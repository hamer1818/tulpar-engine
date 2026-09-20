#!/usr/bin/env python3
"""Uretilmis SPIR-V basliklari GLSL kaynagiyla TAZE mi? (derleme on kosulu)

NEDEN VAR. Depo kurali: shader'lar GLSL yazilir, `tools/compile_shaders.py` ile
CEVRIMDISI derlenir ve olusan `rhi/shaders/*_spv.h` dizileri DEPOYA GIRER
(kosum aninda shader derlemesi yok, CI'da glslc gerekmez). Kurali zorlayan
hicbir sey yoktu: GLSL degistiginde basligi yeniden uretmeyi ne CMake, ne CI,
ne bir test istiyordu. Sonuc olculdu (ENTEGRASYON-PLANI EP-01): `compose.frag`
uc commit once uretilmis basligiyla depoda duruyordu, yani kaynaktaki FSR1/RCAS
dali (`kind == 3`) IKILIDE HIC YOKTU. `UpscalerKind::FSR` secen kod hata da
uyari da almadan `else` dalina, yani bilinear'a dusuyordu. Tuzak 8am'in ("tureti
len dosya bayatlayinca kapi sessizce atlanir") gerceklesmis hali.

NASIL OLCER (iki katman).

  1) KAYNAK OZETI -- ARAC GEREKTIRMEZ, HER MAKINEDE KOSAR.
     Uretilen her baslik kaynaginin SHA-256'sini `// KAYNAK-SHA256:` satirinda
     tasir. Kapi ozeti yeniden hesaplar ve karsilastirir. Bu katman glslc
     olmadan da calisir -- yani CI'da (glslc YOK) da gercekten olcer. Asil
     hata sinifini (kaynak duzenlendi, baslik uretilmedi) bu katman yakalar ve
     HER ZAMAN sert doner.

  2) DERIN DOGRULAMA -- glslc varsa BAYT karsilastirmasi.
     Basliktaki dizinin gercekten o kaynaktan cikan SPIR-V oldugunu dogrular
     (elle duzenlenmis baslik / ozet satiri oynanmis baslik). Yalniz basliklari
     ureten derleyicinin KIMLIGI (`// URETEC:`) yereldekiyle ayni oldugunda
     kosar; cunku farkli bir glslc/SPIRV-Tools surumu ayni kaynaktan ANLAMCA
     AYNI ama BAYT OLARAK FARKLI modul uretir (olculdu: godray/mesh basliklari
     glslang ile uretilmisti, kimlik-normalize komut akislari glslc ciktisiyla
     birebir ayni cikti). Kosmadigi durumda SESSIZ KALMAZ: neden kosmadigini
     ve ozet kapisinin kostugunu satir satir yazar.

POZITIF KONTROL: `--kontrol`. Kapinin gercekten olctugunu kanitlar -- kaynaga
tek bir bosluk eklenmis KOPYA uzerinde KIRMIZI, dokunulmamis kopyada YESIL
donmeli. Ikisinden biri tutmazsa cikis 2 ("KAPI BOZUK").

Kosum:  python3 tools/shader_check.py [<depo_kok>] [--kontrol] [--ayrinti]
Cikis:  0 yesil, 1 bayat/eksik (derleme hatasi), 2 kapinin kendisi bozuk.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.dirname(HERE)

EXTS = (".vert", ".frag", ".comp")
SHA_RE = re.compile(r"^//\s*KAYNAK-SHA256:\s*([0-9a-fA-F]{64})\s*$", re.M)
GEN_RE = re.compile(r"^//\s*URETEC:\s*(.+?)\s*$", re.M)
FIX = "python3 tools/compile_shaders.py %s"


def shaders_dir(root):
    return os.path.join(root, "rhi", "shaders")


def sources(sd):
    """rhi/shaders ALTINDAKI (alt dizinler haric) GLSL kaynaklari.

    `wip/` bilerek disarida: orada kaynak var, uretilmis baslik yok -- onlar
    henuz baglanmamis taslaklar (bkz. rhi/shaders/wip/README.md).
    """
    return sorted(n for n in os.listdir(sd)
                  if n.endswith(EXTS) and os.path.isfile(os.path.join(sd, n)))


def header_path(sd, name):
    return os.path.join(sd, name.replace(".", "_") + "_spv.h")


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def sha_of(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def words_from_header(path):
    """Basliktaki uint32 dizisini cikarir (yorum satirlari haric)."""
    txt = read_text(path)
    body = txt.split("{", 1)[1].split("}", 1)[0]
    return [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{8}", body)]


# ---------------------------------------------------------------------------
# 1) Kaynak ozeti katmani (arac gerektirmez)
# ---------------------------------------------------------------------------
def hash_layer(sd, verbose=False):
    out, bad = [], 0
    names = sources(sd)
    for name in names:
        hdr = header_path(sd, name)
        if not os.path.exists(hdr):
            out.append("  KIRMIZI %-18s uretilmis baslik YOK (%s)" % (name, os.path.basename(hdr)))
            out.append("          Cozum: " + FIX % name)
            bad += 1
            continue
        txt = read_text(hdr)
        m = SHA_RE.search(txt)
        if not m:
            out.append("  KIRMIZI %-18s baslikta `// KAYNAK-SHA256:` satiri yok" % name)
            out.append("          (eski bicimli baslik: tazeligi OLCULEMEZ)")
            out.append("          Cozum: " + FIX % name)
            bad += 1
            continue
        want, got = m.group(1).lower(), sha_of(os.path.join(sd, name))
        if want != got:
            out.append("  KIRMIZI %-18s baslik BAYAT: ESKI kaynaktan uretilmis" % name)
            out.append("          kaynagin ozeti  = %s" % got)
            out.append("          baslikta yazan  = %s" % want)
            out.append("          Cozum: " + FIX % name)
            bad += 1
        elif verbose:
            out.append("  tamam   %-18s %s" % (name, got[:16]))
    out.append("  ozet kapisi: %d shader, %d bayat/eksik  (glslc GEREKMEZ)" % (len(names), bad))
    return bad, out


# ---------------------------------------------------------------------------
# 2) Derin katman (glslc ile bayt karsilastirmasi)
# ---------------------------------------------------------------------------
def header_generators(sd):
    gens = set()
    for name in sources(sd):
        hdr = header_path(sd, name)
        if os.path.exists(hdr):
            m = GEN_RE.search(read_text(hdr))
            gens.add(m.group(1) if m else "(kayitsiz)")
    return gens


def local_glslc_id(glslc):
    r = subprocess.run([glslc, "--version"], capture_output=True)
    txt = (r.stdout or b"").decode("utf-8", "replace") + (r.stderr or b"").decode("utf-8", "replace")
    lines = [l.strip() for l in txt.splitlines() if l.strip()]
    return "glslc " + " ".join(lines[:2]) if lines else "glslc ?"


def deep_layer(sd):
    """(bad, satirlar). KOSMADIGINDA sebebini YAZAR -- sessiz atlama yok."""
    out = []
    glslc = shutil.which("glslc")
    if not glslc:
        out.append("  KOSMADI: glslc bu makinede yok -> bayt karsilastirmasi yapilamadi.")
        out.append("           Ozet kapisi (1) KOSTU: kaynak degisip baslik uretilmemisse")
        out.append("           bu denetim glslc olmadan da KIRMIZI doner.")
        out.append("           Acmak icin: shaderc (glslc) kur.")
        return 0, out
    mine = local_glslc_id(glslc)
    gens = header_generators(sd)
    if gens != {mine}:
        out.append("  KOSMADI: yereldeki derleyici basliklari ureten(ler)le ayni degil ->")
        out.append("           bayt karsilastirmasi ANLAMSIZ olurdu (ayni kaynak, farkli bayt).")
        out.append("           yerel     : %s" % mine)
        for g in sorted(gens):
            out.append("           baslikta  : %s" % g)
        out.append("           Ozet kapisi (1) KOSTU. Butun basliklari bu makinede")
        out.append("           yeniden uretmek derin katmani da acar:")
        out.append("           python3 tools/compile_shaders.py")
        return 0, out
    bad = 0
    for name in sources(sd):
        r = subprocess.run([glslc, "-O", "--target-env=vulkan1.1", "-o", "-",
                            os.path.join(sd, name)], capture_output=True)
        if r.returncode != 0:
            out.append("  KIRMIZI %-18s GLSL derlenmedi:" % name)
            out.append("          " + r.stderr.decode("utf-8", "replace").strip().replace("\n", "\n          "))
            bad += 1
            continue
        fresh = [int.from_bytes(r.stdout[i:i + 4], "little") for i in range(0, len(r.stdout), 4)]
        hdr = header_path(sd, name)
        if not os.path.exists(hdr) or words_from_header(hdr) != fresh:
            out.append("  KIRMIZI %-18s basliktaki dizi bu kaynaktan CIKMIYOR (elle duzenlenmis?)" % name)
            out.append("          Cozum: " + FIX % name)
            bad += 1
    out.append("  bayt kapisi: %d shader, %d uyusmaz  (%s)" % (len(sources(sd)), bad, mine))
    return bad, out


# ---------------------------------------------------------------------------
# 3) Pozitif kontrol
# ---------------------------------------------------------------------------
def kontrol(sd):
    """Kapi gercekten olcuyor mu? Kopya uzerinde: dokunulmamis YESIL, tek
    bosluk eklenmis kaynak KIRMIZI donmeli."""
    print()
    print("=== POZITIF KONTROL (kopya uzerinde; depo dosyalarina dokunulmaz) ===")
    tmp = tempfile.mkdtemp(prefix="shader_check_")
    try:
        kopya = os.path.join(tmp, "shaders")
        shutil.copytree(sd, kopya)
        n0, l0 = hash_layer(kopya)
        print("  NEGATIF kontrol (dokunulmamis kopya) -> %d bayat (0 bekleniyor)" % n0)
        hedef = "compose.frag" if os.path.exists(os.path.join(kopya, "compose.frag")) else sources(kopya)[0]
        with open(os.path.join(kopya, hedef), "ab") as f:
            f.write(b"\n// pozitif kontrol: tek satir eklendi\n")
        n1, l1 = hash_layer(kopya)
        print("  POZITIF kontrol (%s'e tek satir eklendi) -> %d bayat (>=1 bekleniyor)" % (hedef, n1))
        for line in l1:
            if "KIRMIZI" in line or "Cozum" in line or "ozeti" in line or "yazan" in line:
                print("   " + line.strip())
        if n0 != 0 or n1 < 1:
            print()
            print("KAPI BOZUK: pozitif/negatif kontrol beklendigi gibi donmedi --")
            print("bu denetim hicbir sey olcmuyor.")
            return 2
        print("  -> kontroller 2/2 gecti: kapi gercekten olcuyor.")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = args[0] if args else DEFAULT_ROOT
    verbose = "--ayrinti" in sys.argv
    sd = shaders_dir(root)
    if not os.path.isdir(sd):
        print("shader dizini yok: %s" % sd, file=sys.stderr)
        return 2

    print("=== Uretilmis SPIR-V tazelik denetimi (rhi/shaders) ===")
    print("--- 1) Kaynak ozeti: baslik GLSL kaynaginin SHA-256'sini tasiyor mu ---")
    bad1, l1 = hash_layer(sd, verbose=verbose)
    print("\n".join(l1))
    print()
    print("--- 2) Derin dogrulama: basliktaki dizi gercekten bu kaynaktan mi ---")
    bad2, l2 = deep_layer(sd)
    print("\n".join(l2))

    rc = 1 if (bad1 or bad2) else 0
    if "--kontrol" in sys.argv:
        k = kontrol(sd)
        if k:
            return k

    print()
    if rc:
        print("Bayat baslik, derleyiciden UYARI ALMADAN yanlis shader'i ikiliye koyar.")
        print("Hepsini yeniden uretmek: python3 tools/compile_shaders.py")
    print("SHADER TAZELIK DENETIMI: " + ("KIRMIZI" if rc else "YESIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
