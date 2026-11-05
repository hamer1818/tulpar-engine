#!/usr/bin/env python3
"""faz8_shader_audit.py — Faz 8 shader alt kumesi prototipinin kapisi.

Ne olcer
--------
1. GERCEK VARLIKLAR: tests/assets/shader/*.tprs cevrilir, glslc ile
   SPIR-V'ye derlenir ve depodaki rhi/shaders/*_spv.h ile BAYT
   karsilastirilir. Tek bir fark bile kirmizi.
2. POZITIF KONTROL: bilerek bozulmus girdiler verilir. Her biri KIRMIZI
   olmali; biri bile gecerse kapi hicbir sey olcmuyor demektir ve denetim
   yine kirmizi olur. Dort kontrol var, uc ayri katmani ayri ayri yokluyor:
     - sozdizimi hatasi          -> ceviricinin ayristiricisi yakalamali
     - bilinmeyen tip / fonksiyon -> ceviricinin anlamsal denetimi yakalamali
     - tip uyusmazligi            -> cevirici YAKALAMAZ, glslc yakalamali
       (prototip tip denetimi yapmiyor; bu kontrol o siniri gorunur tutar)
     - degistirilmis sabit        -> bayt karsilastirmasi yakalamali
       (yoksa "bayt ayni" satiri bos bir iddia olurdu)

glslc yoksa SPIR-V'ye bagli adimlar ACIKCA "ATLANDI" yazar ve sayilir;
sessizce gecmez.

REFERANS AYNI DERLEYICIDEN (2026-09-27)
---------------------------------------
Bayt karsilastirmasi eskiden depodaki *_spv.h ile yapiliyordu. O basliklar
glslang 16.6.0 ile uretilmis (`// URETEC:`); bu makinedeki glslc 2026.3 ayni
kaynaktan her shader'da 12 bayt farkli (uretec kimligi) SPIR-V veriyor. Sonuc:
denetim "0/19 bayt ayni, 17 kirmizi" diyordu — cevirici hakkinda HICBIR SEY
soylemeden. Ustelik "degistirilmis sabit" pozitif kontrolu de farki basliga
karsi olctugu icin bu durumda HER ZAMAN yesil veriyordu (bos kontrol).
Artik iki taraf AYNI yerel derleyiciden geciyor: cevrilmis .tprs'in SPIR-V'si,
referans GLSL'in (rhi/shaders/<ad>) YEREL derlemesiyle karsilastiriliyor ve
pozitif kontrol degismis sabiti degismemis .tprs'in yerel derlemesine karsi
olcuyor. Basliklarla bayt esitligi ancak uretec kimligi tutunca (ayrica)
raporlanir. Kimlik denetimi tools/shader_check.py'deki ile ayni.

Kosum:  python3 tools/faz8_shader_audit.py
Hicbir kapiya bagli DEGIL (CMake/CI/derle.sh); elle calistirilir. Once
docs/PLAN.md ve docs/FAZ3.md "build.sh suites icinde" diyordu — yanlis.
"""

import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TOOL = os.path.join(REPO, "tools", "tpr_shader.py")
ASSETS = os.path.join(REPO, "tests", "assets", "shader")


def load_tool():
    spec = importlib.util.spec_from_file_location("tpr_shader", TOOL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Audit:
    def __init__(self):
        self.fail = 0
        self.skip = 0
        self.ok = 0

    def yesil(self, msg):
        self.ok += 1
        print("  YESIL   %s" % msg)

    def kirmizi(self, msg):
        self.fail += 1
        print("  KIRMIZI %s" % msg)

    def atlandi(self, msg):
        self.skip += 1
        print("  ATLANDI %s" % msg)


BOZUK = [
    ("sozdizimi", "cevirici-parse", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { o_color = vec4(1.0, 0.0 0.0, 1.0); }
'''),
    ("bilinmeyen tip", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { qvec9 x = qvec9(1.0); o_color = vec4(1.0); }
'''),
    ("bilinmeyen fonksiyon", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func main() { o_color = vec4(kokusuz_fonksiyon(1.0)); }
'''),
    # Demet donusu -> `out` parametresi (2026-11-05, mesh.frag): ad sayisi
    # tutmayan acma ve demet dondurmeyen fonksiyonu acma ceviricide durmali —
    # yoksa eksik/fazla `out` argumani glslc'ye "no matching function" diye,
    # ya da (ayni sayida farkli tip) HIC hata vermeden gecebilirdi.
    ("demet ad sayisi", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func f(): (float, float) { return 1.0, 2.0; }
func main() { float a, b, c = f(); o_color = vec4(a); }
'''),
    ("demet olmayan acma", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func g(): float { return 1.0; }
func main() { float a, b = g(); o_color = vec4(a); }
'''),
    ("demet return sayisi", "cevirici-anlam", '''str asama = "frag";
vec4 o_color = cikti(0);
func f(): (float, float) { return 1.0; }
func main() { float a, b = f(); o_color = vec4(a, b, 0.0, 1.0); }
'''),
    ("dizi literali boyu", "cevirici-anlam", '''str asama = "vert";
func main() { const vec2[3] p = [vec2(0.0), vec2(1.0)]; gl_Position = vec4(p[0], 0.0, 1.0); }
'''),
    ("comp disinda is_grubu", "cevirici-anlam", '''str asama = "frag";
uvec3 g = is_grubu(64);
vec4 o_color = cikti(0);
func main() { o_color = vec4(1.0); }
'''),
    ("is_grubu'suz comp", "cevirici-anlam", '''str asama = "comp";
uint[64] s = paylasimli();
func main() { s[0] = 0u; barrier(); }
'''),
    ("tip uyusmazligi", "glslc", '''str asama = "frag";
vec2 v_uv = girdi(0);
sampler2D u_src = ornek(0, 0);
vec4 o_color = cikti(0);
func main() { vec3 c = texture(u_src, v_uv); o_color = vec4(c, 1.0); }
'''),
]


def main():
    a = Audit()
    if not os.path.exists(TOOL):
        print("KIRMIZI: %s yok" % TOOL)
        return 1
    tool = load_tool()
    have_glslc = shutil.which("glslc") is not None
    tmp = tempfile.mkdtemp(prefix="faz8audit")
    # Basliklari ureten derleyici ile yereldeki ayni mi? (shader_check.py ile
    # ayni kimlik tanimi.) Ayniysa baslikla bayt esitligi de raporlanir.
    same_gen = False
    ids = ""
    if have_glslc:
        spec = importlib.util.spec_from_file_location(
            "shader_check", os.path.join(REPO, "tools", "shader_check.py"))
        sc = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sc)
        mine = sc.local_glslc_id(shutil.which("glslc"))
        gens = sc.header_generators(tool.REPO_SHADERS)
        same_gen = gens == {mine}
        ids = "yerel: %s | basliklar: %s" % (mine, ", ".join(sorted(gens)))
        print("derleyici kimligi — %s -> baslikla bayt karsilastirmasi %s"
              % (ids, "KOSUYOR" if same_gen else "KOSMADI (farkli uretec)"))

    def derle(glsl, stage):
        """(bayt, hata) — glslc yoksa (None, 'glslc yok')."""
        return tool.compile_spv(glsl, stage, None, tmp)

    # ---------------------------------------------------------------- 1
    print("\n[1] Gercek varliklar: .tprs -> GLSL -> SPIR-V -> depodaki *_spv.h")
    files = sorted(f for f in os.listdir(ASSETS) if f.endswith(".tprs")) \
        if os.path.isdir(ASSETS) else []
    if not files:
        a.kirmizi("%s altinda .tprs yok — kapi olcecek sey bulamadi" % ASSETS)
    ayni = 0
    for f in files:
        path = os.path.join(ASSETS, f)
        base = f[:-5]
        try:
            glsl = tool.translate(path)
        except tool.ShaderError as e:
            a.kirmizi("%-24s cevrilemedi: %s" % (base, e))
            continue
        if not have_glslc:
            a.atlandi("%-24s cevrildi, glslc yok — SPIR-V dogrulanmadi" % base)
            continue
        data, err = derle(glsl, tool.tprs_stage(path))
        if data is None:
            first = (err or "").strip().splitlines()
            a.kirmizi("%-24s glslc: %s" % (base, first[0] if first else "?"))
            continue
        # Referans: ayni yerel derleyiciyle derlenmis ORIJINAL GLSL.
        refsrc = tool.source_path(base)
        ref = None
        if os.path.exists(refsrc):
            ref, rerr = derle(open(refsrc).read(), tool.tprs_stage(path))
            if ref is None:
                a.kirmizi("%-24s referans GLSL yerelde derlenmedi: %s"
                          % (base, (rerr or "?").strip().splitlines()[0]))
                continue
        hdr = tool.header_bytes(base) if same_gen else None
        pin = tool.pinned_digest(open(path).read())
        now = tool.source_digest(base)
        if ref is None:
            a.yesil("%-24s %5d bayt (depoda karsiligi yok)" % (base, len(data)))
        elif pin and now and pin != now:
            # Referans shader BASKA biri tarafindan degistirilmis. Sessizce
            # kirmizi (yanlis suclama) ya da sessizce yesil (olcum yok)
            # olmasin: GORUNUR atlama.
            a.atlandi("%-24s kaynak GLSL degismis (%s -> %s); .tprs yeniden "
                      "ported edilmeli" % (base, pin, now))
        elif ref == data:
            ek = ""
            if hdr is not None:
                ek = ", *_spv.h ile de AYNI" if hdr == data else ", *_spv.h'den FARKLI"
            a.yesil("%-24s %5d bayt, referans GLSL ile BAYT AYNI%s"
                    % (base, len(data), ek))
            ayni += 1
        else:
            a.kirmizi("%-24s SPIR-V farkli (%d vs referans %d bayt)"
                      % (base, len(data), len(ref)))
    if files and have_glslc:
        print("  [bilgi] %d/%d dosya referans GLSL'in yerel derlemesiyle bayt "
              "ayni (%d atlandi: kaynak degismis)"
              % (ayni, len(files), a.skip))
    # Kapsam: motorun HER shader'inin bir .tprs karsiligi var mi? 2026-11-05'te
    # 23/23 oldu. Yeni shader tasinmadan gelirse bu GORUNUR ATLANDI'dir
    # (yerel_ci.sh ATLANDI'yi kirmizi sayar) — oran sessizce dusmesin.
    # rhi/shaders/wip/ derlenmeyen taslaklardir, sayilmaz.
    src_dir = os.path.join(os.path.dirname(ASSETS), "..", "..", "rhi", "shaders")
    src_dir = os.path.normpath(src_dir)
    ported = {f[:-5] for f in files}
    shaders = sorted(f for f in os.listdir(src_dir)
                     if os.path.splitext(f)[1] in (".vert", ".frag", ".comp"))
    eksik = [f for f in shaders if f not in ported]
    for f in eksik:
        a.atlandi("%-24s .tprs karsiligi yok — Tulpar alt kumesine tasinmamis" % f)
    print("  [bilgi] kapsam: motorun %d shader'inin %d'i tasinmis"
          % (len(shaders), len(shaders) - len(eksik)))

    # ---------------------------------------------------------------- 2
    print("\n[2] Pozitif kontrol: bozuk girdi KIRMIZI yapmali")
    for ad, katman, src in BOZUK:
        p = os.path.join(tmp, "bozuk.tprs")
        with open(p, "w") as fh:
            fh.write(src)
        yakalandi, nerede, detay = False, "", ""
        try:
            glsl = tool.translate(p)
        except tool.ShaderError as e:
            yakalandi, nerede, detay = True, "cevirici", str(e)
        else:
            if not have_glslc:
                if katman == "glslc":
                    a.atlandi("%-22s glslc yok — bu kontrol kosmadi" % ad)
                    continue
            else:
                data, err = derle(glsl, tool.tprs_stage(p))
                if data is None:
                    lines = (err or "").strip().splitlines()
                    yakalandi, nerede = True, "glslc"
                    detay = lines[0] if lines else "?"
        if yakalandi:
            a.yesil("%-22s %s yakaladi: %s"
                    % (ad, nerede, detay[:58]))
        else:
            a.kirmizi("%-22s BOZUK GIRDI GECTI — kapi bu katmani (%s) "
                      "olcmuyor" % (ad, katman))

    # Demetin GENEL yolu (adli sonuc kosulu tutmuyor: ifade donuyor, iki
    # farkli cikis) gercek varliklarda kullanilmiyor; yine de GECERLI GLSL
    # uretmeli. Olculmeyen yol cururdu.
    if have_glslc:
        p = os.path.join(tmp, "demet.tprs")
        with open(p, "w") as fh:
            fh.write('''str asama = "frag";
float v_t = girdi(0);
vec4 o_color = cikti(0);
func f(float t): (vec3, float) {
  if (t > 0.5) { return vec3(t), 1.0; }
  return vec3(0.0), t * 2.0;
}
func main() {
  vec3 c, float w = f(v_t);
  vec3 x = vec3(0.0);
  float y = 0.0;
  x, y = f(v_t * 0.5);
  o_color = vec4(c * w + x, y);
}
''')
        try:
            glsl = tool.translate(p)
            data, err = derle(glsl, "frag")
        except tool.ShaderError as e:
            data, err = None, "cevirici: %s" % e
        if data is None:
            a.kirmizi("demet genel yolu      gecersiz GLSL: %s"
                      % ((err or "?").strip().splitlines() or ["?"])[0][:58])
        elif "out vec3 _r0" not in glsl or "_r1 = t * 2.0;" not in glsl:
            a.kirmizi("demet genel yolu      beklenen `out` cevirisi yok")
        else:
            a.yesil("demet genel yolu       %d bayt SPIR-V (_rN out parametreleri)"
                    % len(data))

    # bayt karsilastirmasinin kendisi canli mi?
    if have_glslc and files:
        ornek = os.path.join(ASSETS, "bloom_down.frag.tprs")
        if os.path.exists(ornek):
            src = open(ornek).read()
            assert "c * 0.25" in src, "kontrolun dayandigi sabit degismis"
            p = os.path.join(tmp, "bloom_down.frag.tprs")
            with open(p, "w") as fh:
                fh.write(src.replace("c * 0.25", "c * 0.26"))
            data, err = derle(tool.translate(p), "frag")
            # Karsi taraf: DEGISMEMIS .tprs'in ayni derleyiciyle derlemesi.
            # (Basliga karsi olcmek, farkli uretecte her zaman "farkli" der
            # ve bu kontrolu bos yere yesil yapardi.)
            ref, _ = derle(tool.translate(ornek), "frag")
            if data is None:
                a.kirmizi("degistirilmis sabit   glslc derleyemedi: %s" % err)
            elif data == ref:
                a.kirmizi("degistirilmis sabit   SPIR-V hala AYNI — bayt "
                          "karsilastirmasi hicbir sey olcmuyor")
            else:
                a.yesil("degistirilmis sabit    bayt karsilastirmasi farki "
                        "gordu (%d vs %d)" % (len(data), len(ref)))
    else:
        a.atlandi("degistirilmis sabit    glslc yok — bayt kontrolu kosmadi")

    shutil.rmtree(tmp, ignore_errors=True)
    print("\nFaz 8 shader denetimi: %d yesil, %d kirmizi, %d atlandi"
          % (a.ok, a.fail, a.skip))
    if a.skip and not a.fail:
        neden = "glslc kurulu degil" if not have_glslc \
            else ("referans GLSL degismis ya da shader tasinmamis — .tprs'i "
                  "kaynakla esitle, sonra --pin")
        print("UYARI: %d adim ATLANDI (%s) — bu denetim TAM kosmadi."
              % (a.skip, neden))
    return 1 if a.fail else 0


if __name__ == "__main__":
    sys.exit(main())
