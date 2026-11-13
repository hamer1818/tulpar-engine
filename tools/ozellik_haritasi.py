#!/usr/bin/env python3
"""Motor ÖZELLİK HARİTASI üretici (docs/OZELLIK-HARITASI.md).

Amaç: motorun bütün özelliklerini TEK sayfada, aranabilir tutmak (ajan ve insan için hızlı erişim):
"X nerede?" -> dosya / köprü fonksiyonu / editör komutu / test / belge. Elle yazılmaz, KAYNAKTAN üretilir
(deterministik: zaman damgası yok), bu yüzden eskimez; eskiyince kapı kırmızı döner.

  python3 tools/ozellik_haritasi.py            # docs/OZELLIK-HARITASI.md'yi yazar
  python3 tools/ozellik_haritasi.py --denetle  # dosya taze mi (kapı: farklıysa çıkış 1)
  python3 tools/ozellik_haritasi.py --ara kelime   # haritada ara (satır satır)

Kaynaklar: dizin/dosya başlık yorumları (// L? KATMAN — ...), tools/gen_engine_bindings.py SPEC (köprü API),
app/editor_commands.hpp CommandId, ENGINE_TEST adları, tools/ betik başlıkları, docs/ başlıkları, docs/TUZAKLAR.md
başlıkları, TULPAR_* ortam değişkenleri, constexpr kapasite sabitleri.
"""
import os, re, sys, collections

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CIKTI = os.path.join(KOK, "docs", "OZELLIK-HARITASI.md")
KATMANLAR = [("platform", "L0"), ("core", "L1"), ("rhi", "L2"), ("renderer", "L3"), ("audio", "L3"), ("sim", "L4"),
             ("content", "L6"), ("app", "L6"), ("bridge", "L6"), ("tools", "L7"), ("tulpar", "Tulpar"), ("tests", "test")]
UZANTILAR = (".hpp", ".h", ".cpp", ".c", ".mm", ".S", ".py", ".sh", ".tpr")
ATLA = {"third_party", "yapi", "build-android", "build-android-x86_64", ".git", "__pycache__", "generated"}


def oku(yol):
    try:
        with open(yol, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def kisalt(s, n=110):
    s = re.sub(r"\s+", " ", s).strip()
    return s if len(s) <= n else s[: n - 1].rstrip() + "…"


def baslik_satiri(yol):
    """Dosyanın ilk anlamlı yorum satırı (// ya da # ya da \"\"\" docstring), katman önekini atar."""
    for ln in oku(yol).splitlines()[:14]:
        t = ln.strip()
        if t.startswith("#!") or t.startswith("#pragma") or t.startswith("#include") or not t:
            continue
        m = re.match(r"^(?://+|#|/\*+|\"\"\")\s*(.*)$", t)
        if not m:
            return ""
        g = m.group(1).strip(" */\"")
        g = re.sub(r"^L\d\s*[A-Z]+\s*[—\-:]+\s*", "", g)  # "L1 CORE — "
        if g and not g.startswith("-") and not re.match(r"^=+$", g) and not g.startswith("define "):
            return kisalt(g)
    return ""


def dosyalar(dizin, uz=UZANTILAR):
    for kok, dizinler, adlar in os.walk(os.path.join(KOK, dizin)):
        dizinler[:] = sorted(d for d in dizinler if d not in ATLA)
        for a in sorted(adlar):
            if a.endswith(uz):
                yield os.path.join(kok, a)


def katman_bolumu():
    ç = ["## 1. Katmanlar ve dosyalar", "",
         "Her satır: `dosya` — ilk başlık yorumu. Katman kuralı: bir katman yalnız altındakini `#include` eder (`tools/layer_check.py`).", ""]
    for d, k in KATMANLAR:
        if d in ("tests", "tulpar"):
            continue
        fs = [f for f in dosyalar(d) if os.path.sep + "shaders" + os.path.sep not in f]
        if not fs:
            continue
        loc = sum(oku(f).count("\n") for f in fs)
        ç += ["### %s — %s (%d dosya, %d satır)" % (d, k, len(fs), loc), ""]
        for f in fs:
            r = os.path.relpath(f, KOK)
            ç.append("- `%s` — %s" % (r, baslik_satiri(f)))
        ç.append("")
    return ç


def kopru_bolumu():
    s = oku(os.path.join(KOK, "tools", "gen_engine_bindings.py"))
    i = s.index("SPEC = [")
    j = s.index("\n]\n", i)
    grup, gruplar, onceki_yorum = "genel", collections.OrderedDict(), False
    for ln in s[i:j].splitlines()[1:]:
        t = ln.strip()
        if t.startswith("#"):
            # Yalnız yorum BLOĞUNUN ilk satırı grup başlığıdır (devam satırları cümlenin ortası olur).
            if not onceki_yorum:
                grup = kisalt(t.lstrip("# ").strip().strip("-").strip(), 60) or grup
            onceki_yorum = True
            continue
        onceki_yorum = False
        m = re.match(r'\("(eng_\w+)", "(\w+)", \[(.*?)\], "(.*)"\)', t)
        if m:
            gruplar.setdefault(grup, []).append((m.group(1), m.group(2), m.group(3), kisalt(m.group(4), 90)))
    toplam = sum(len(v) for v in gruplar.values())
    ç = ["## 2. Köprü API (TulparLang ↔ motor)", "",
         "Düz skaler C ABI (`bridge/engine_api.h`; C adı `teng_x` = aşağıdaki `eng_x` önüne `t` konmuş hali, Tulpar yüzü Türkçe `motor_ac`... + İngilizce ikiz). "
         "Tek kaynak: `tools/gen_engine_bindings.py` `SPEC`. Toplam **%d** fonksiyon. Sözleşme: `docs/KOPRU.md`." % toplam, ""]
    for g, v in gruplar.items():
        ç.append("### %s (%d)" % (g, len(v)))
        ç.append("")
        for ad, ret, args, ac in v:
            ç.append("- `%s(%s)` → %s — %s" % (ad, re.sub(r'\("?(\w+)"?, "?(\w+)"?\)', r"\1:\2", args).replace('"', ""), ret, ac))
        ç.append("")
    return ç


def editor_bolumu():
    h = oku(os.path.join(KOK, "app", "editor_commands.hpp"))
    m = re.search(r"enum class CommandId[^{]*\{(.*?)\};", h, re.S)
    komutlar = []
    if m:
        for ln in m.group(1).splitlines():
            mm = re.match(r"\s*(\w+),?\s*(?://\s*(.*))?$", ln)
            if mm and mm.group(1) not in ("None", "Count"):
                komutlar.append((mm.group(1), (mm.group(2) or "").strip()))
    ç = ["## 3. Editör", "",
         "Çalıştırma: `./yapi/engine_editor [--scene x.sahne | --karsilama | --kip basit]`; penceresiz doğrulama: `--headless N --out x.ppm`. "
         "Arayüz sürücüsü: `tools/arayuz.py` (docs/ARAYUZ-SURUCUSU.md). Dil: Tercihler > Dil / `TULPAR_EDITOR_DIL` (docs: app/editor_dil.hpp).", "",
         "### Komutlar (`CommandId`, kısayolla; %d adet)" % len(komutlar), ""]
    for ad, k in komutlar:
        ç.append("- `%s`%s" % (ad, " — " + k if k else ""))
    ç += ["", "### Editör dosyaları", ""]
    for f in dosyalar("app"):
        b = os.path.basename(f)
        if b.startswith("editor") and b.endswith(".cpp"):
            ç.append("- `app/%s` — %s" % (b, baslik_satiri(f)))
    ç.append("")
    return ç


def test_bolumu():
    sayac = collections.Counter()
    dosya_say = collections.Counter()
    for f in dosyalar("tests", (".cpp",)):
        s = oku(f)
        adlar = re.findall(r"ENGINE_TEST\((\w+)\)", s)
        for a in adlar:
            sayac[a.split("_")[0]] += 1
        if adlar:
            dosya_say[os.path.relpath(f, KOK)] = len(adlar)
    toplam = sum(sayac.values())
    ç = ["## 4. Testler ve kapılar", "",
         "`./yapi/engine_tests [alt dize]` — adın ALT DİZE eşleşmesi. Toplam **%d** `ENGINE_TEST` (koşan sayı için özet satırı: `M atlandi`'yı oku). "
         "Yeni test dosyası `CMakeLists.txt` `ENGINE_TEST_SOURCES`'a ELLE eklenir." % toplam, "",
         "### Ad önekine göre (öne göre süz: `engine_tests <önek>`)", ""]
    for k, n in sorted(sayac.items(), key=lambda x: (-x[1], x[0])):
        ç.append("- `%s` — %d" % (k, n))
    ç += ["", "### Test dosyaları", ""]
    for f, n in sorted(dosya_say.items()):
        ç.append("- `%s` — %d test" % (f, n))
    ç.append("")
    return ç


def arac_bolumu():
    ç = ["## 5. Araçlar ve kapılar (`tools/`)", ""]
    for f in dosyalar("tools", (".py", ".sh")):
        if os.path.sep + "kit" + os.path.sep in f or os.path.sep + "kurulum" + os.path.sep in f:
            continue
        ç.append("- `%s` — %s" % (os.path.relpath(f, KOK), baslik_satiri(f)))
    ç += ["", "Kurulum/paketleme: `tools/kurulum/*` (AppImage, Inno Setup), ortak oyun kiti: `tools/kit/`.", ""]
    return ç


def belge_bolumu():
    ç = ["## 6. Belgeler (`docs/`)", ""]
    for a in sorted(os.listdir(os.path.join(KOK, "docs"))):
        if not a.endswith(".md") or a == "OZELLIK-HARITASI.md":
            continue
        s = oku(os.path.join(KOK, "docs", a))
        t = next((l.lstrip("# ").strip() for l in s.splitlines() if l.startswith("#")), "")
        ç.append("- `docs/%s` — %s" % (a, kisalt(t, 100)))
    ç.append("")
    return ç


def tuzak_bolumu():
    s = oku(os.path.join(KOK, "docs", "TUZAKLAR.md"))
    ç = ["## 7. Tuzaklar dizini (`docs/TUZAKLAR.md`)", "",
         "Kaynak yorumları buraya \"Tuzaklar 8l\" diye atıf yapar. Bir şey kırıldığında ÖNCE buraya bak.", ""]
    for ln in s.splitlines():
        m = re.match(r"^###\s+(\d+[a-z]+)\.\s+(.*)$", ln)
        if m:
            ç.append("- **%s** — %s" % (m.group(1), kisalt(m.group(2), 120)))
    ç.append("")
    return ç


def ortam_bolumu():
    gor = collections.defaultdict(set)
    for d in ("app", "bridge", "core", "platform", "renderer", "rhi", "sim", "audio", "content", "tests", "tools"):
        for f in dosyalar(d):
            s = oku(f)
            for m in re.finditer(r"\b(TULPAR_[A-Z0-9_]+)\b", s):
                gor[m.group(1)].add(d)
    ç = ["## 8. Ortam değişkenleri (`TULPAR_*`)", "",
         "Hangi dizinde geçtiği yanında; anlamı için ilgili dosyada/`CLAUDE.md`'de ara.", ""]
    for k in sorted(gor):
        ç.append("- `%s` — %s" % (k, ", ".join(sorted(gor[k]))))
    ç.append("")
    return ç


def kapasite_bolumu():
    ç = ["## 9. Kapasiteler ve sınırlar (`constexpr`)", "",
         "Kapasiteler init'te alınır; dolunca geçersiz handle döner ve SAYILIR (sessizce büyümez). Yeni özellik eklerken ilk bakılacak yer.", ""]
    for d in ("content", "sim", "renderer", "core", "rhi", "audio", "bridge", "app"):
        for f in dosyalar(d, (".hpp", ".h")):
            satirlar = []
            for ln in oku(f).splitlines():
                m = re.match(r"^\s*(?:inline )?constexpr\s+(?:uint32_t|uint16_t|int|size_t|int32_t|uint64_t)\s+(k\w*(?:Max|Cap|Len|Count|Limit|Size)\w*)\s*=\s*([0-9xXa-fA-F'u]+)", ln)
                if m:
                    satirlar.append("`%s`=%s" % (m.group(1), m.group(2)))
            if satirlar:
                ç.append("- `%s`: %s" % (os.path.relpath(f, KOK), ", ".join(satirlar[:14])))
    ç.append("")
    return ç


def uret():
    n_kopru = len(re.findall(r'^\s*\("eng_', oku(os.path.join(KOK, "tools", "gen_engine_bindings.py")), re.M))
    n_test = sum(len(re.findall(r"ENGINE_TEST\(", oku(f))) for f in dosyalar("tests", (".cpp",)))
    n_cmd = len(re.findall(r"^\s+\w+,\s*//", oku(os.path.join(KOK, "app", "editor_commands.hpp")), re.M))
    ç = ["<!-- ÜRETİLDİ: tools/ozellik_haritasi.py — elle düzenleme; `python3 tools/ozellik_haritasi.py` ile yenile. -->",
         "# Tulpar Engine — Özellik Haritası", "",
         "Bu sayfa motorun **bütün özelliklerinin** aranabilir haritasıdır: bir şey nerede, hangi köprü fonksiyonu, hangi editör komutu, "
         "hangi test, hangi belge. Kaynaktan üretilir (eskirse `tools/ozellik_haritasi.py --denetle` kırmızı döner). "
         "Hızlı arama: `python3 tools/ozellik_haritasi.py --ara <kelime>` ya da bu dosyada grep.", "",
         "**Sayılar:** köprü %d fonksiyon · %d `ENGINE_TEST` · ~%d editör komutu." % (n_kopru, n_test, n_cmd), "",
         "## 0. Zihin haritası (üst düzey)", "",
         "```mermaid", "mindmap", "  root((Tulpar Engine))",
         "    Platform L0", "      pencere GLFW dlopen", "      dosya sistemi", "      Android NativeActivity", "      oyun kanalı",
         "    Çekirdek L1", "      fiber iş sistemi", "      arena bellek", "      matematik", "      konteynerler (STL yok)",
         "    RHI L2", "      Vulkan dlopen", "      render graph", "      senkron / bellek bloğu",
         "    Renderer L3", "      PBR + gölge CSM", "      bloom / post / TAA", "      GPU cull + dolaylı çizim", "      kümelenmiş ışık", "      skinning", "      VFX / partikül",
         "    Ses L3", "      miniaudio", "      3B ses + HRTF",
         "    Simülasyon L4", "      ECS archetype", "      Jolt fizik", "      Recast navmesh", "      animasyon sıkıştırma", "      sabit adım + replay",
         "    İçerik L6", "      glTF/GLB", "      sahne .sahne/.sahneb", "      KTX2 / ASTC", "      meshopt + LOD", "      font", "      prefab",
         "    Köprü L6", "      TulparLang C ABI", "      %d fonksiyon" % n_kopru, "      Android / masaüstü host",
         "    Editör", "      panel + gizmo", "      Kod paneli + LSP", "      Oynat / canlı kod+sahne", "      Tercihler + İngilizce",
         "    Araçlar", "      kapılar (layer, shader, scene, icon)", "      yerel CI", "      oyun fabrikası + yayın",
         "```", ""]
    ç += katman_bolumu() + kopru_bolumu() + editor_bolumu() + test_bolumu() + arac_bolumu() + belge_bolumu() + tuzak_bolumu() \
        + ortam_bolumu() + kapasite_bolumu()
    return "\n".join(ç).rstrip() + "\n"


def main():
    a = sys.argv[1:]
    metin = uret()
    if a and a[0] == "--denetle":
        if oku(CIKTI) != metin:
            print("OZELLIK HARITASI ESKI: python3 tools/ozellik_haritasi.py ile yenile", file=sys.stderr)
            return 1
        print("ozellik haritasi taze (%d satir)" % metin.count("\n"))
        return 0
    if a and a[0] == "--ara":
        k = " ".join(a[1:]).lower().replace("teng_", "eng_")   # C adı (teng_x) ve SPEC adı (eng_x) aynı sayılır
        n = 0
        for ln in oku(CIKTI).splitlines():
            if k and k in ln.lower():
                print(ln)
                n += 1
        print("-- %d eşleşme" % n, file=sys.stderr)
        return 0 if n else 1
    with open(CIKTI, "w", encoding="utf-8") as f:
        f.write(metin)
    print("yazildi: %s (%d satir)" % (os.path.relpath(CIKTI, KOK), metin.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
