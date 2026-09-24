#!/usr/bin/env python3
"""Sahne metin bicimi kapisi: yazici ile ayristirici AYNI dili konussun.

NEDEN: `.sahne` yazicisi (write_entity) ve ayristiricisi (scene_parse) elle
tutulan iki ayri tablodur. Biri degisip oteki degismediginde derleyici hicbir
sey demez -- sahne yazilir, geri okunamaz ya da ALANLAR KAYAR. Bu oturumda
olculdu:

  * `su` ve `ruzgar` satirlarinda yon Vec2 ama yazici vec(Vec3) cagiriyordu:
    hem derleme hatasi hem de ayristiriciyla uyusmayan jeton sayisi.
  * `partikul` bloku TEK satira yaziliyordu; ayristirici omur/boyut/hiz/
    dagilim'i AYRI satirlarda bekliyor ve satir kMaxTok=8 tavanini asiyordu
    (split() *bad=true -> sahne geri okunamaz).

Kapi iki sey olcer:
  1. Yazicinin urettigi her anahtar kelimenin ayristiricida bir dali var mi
     (ve tersi).
  2. Yazicinin o satirda kac JETON urettigi, ayristiricinin bekledigi sayiyla
     uyusuyor mu.

Ayrica satir basina jeton sayisinin kMaxTok tavanini asmadigini dogrular.

Kullanim:  python tools/scene_check.py [kok]
           python tools/scene_check.py --oz-sinama   (kapinin kendi sinamasi)
Donus:     0 = temiz, 1 = uyusmazlik (derlemeyi kir)
"""
import os
import re
import sys

SRC = "content/scene.cpp"

# Jeton sayimi DALLANMAYI bilmez: bir yazici satiri if/else ile iki ayri sekil
# yaziyorsa iki dalin jetonlari TOPLANIR ve sayi sisirilir. Asagidaki anahtarlar
# bu yuzden muaf; her birinin GERCEK en buyuk jeton sayisi elle yazili ve
# kMaxTok tavani yine de bu sayiyla olculur.
#
# Muafiyet EKLEMEK, sayiyi degistirmekten farkli bir sey: bir satir gercekten
# ayristiriciyla uyusmuyorsa buraya yazilmaz, DUZELTILIR.
BRANCHY = {
    # "govde kutu x y z dinamik" (6) | "govde kure r sabit" (4).
    # Sayici iki dali birlestirip fazla sayiyor; gercegi 6.
    "govde": 6,
}

# Yazici cagrilarinin CIKTIYA koydugu metin (jeton sayimi icin temsilci).
# Sayilar tek kelime, str() TIRNAKLI tek jeton -- ayristiricinin split()'i
# tirnak icini bosluk olsa da tek jeton sayar.
EMIT = {
    "num": "N",
    "vec": "N N N",
    "vec2": "N N",
    "str": '"S"',
}


def read(root):
    with open(os.path.join(root, SRC), encoding="utf-8") as f:
        return f.read()


# --------------------------------------------------------------------------
# Yazici tarafi: write_entity govdesi -- CIKTI AKISI BENZETIMI
# --------------------------------------------------------------------------
# Yazicinin her `o.xxx(...)` cagrisi, ciktiya koyacagi metne cevrilir (dizgi
# kacislari cozulur, sayi/str cagrilari temsilci jetonla); akis '\n'lerden
# satirlara bolunur ve her satir ayristiricinin split() KURALIYLA jetonlanir:
# bosluk ayirir, "..." tek jeton. Satirin ilk jetonu anahtar kelimedir.
#
# NEDEN BENZETIM (regex ile satir basi aramak yerine): eski kapi satir basini
# `o.puts("  kw ")` bicimiyle ariyordu ve TIRNAKLA baslayan bir operand
# (`o.puts("  betik \"")`) bu kalibe uymuyordu -- `betik` ve `ses` satirlari
# kapida HIC GORUNMUYORDU. Ayni sekilde `o.puts(lt)` gibi bir degisken
# satiri kapatmiyor, `isik` satiri bir sonraki satirla BIRLESIYORDU.
# Olculdu 2026-09-25, ayni (E3 oncesi) scene.cpp uzerinde: eski kapi 35
# yazici satiri goruyordu, benzetim 38 -- betik (3 jeton), ses (6) ve
# spot_koni (3) hic olculmuyordu; isik spot_koni'yi yutup 9 sayiliyordu
# (gercegi 7). Ucu de ayristiriciyla UYUSUYORDU; kapi sansla yesildi.
# Benzetimde tirnak bir durum (acik/kapali), degisken ise govdedeki
# atamalarindan cozulur; cozulemeyen ifade tirnak DISINDA ise sayilamaz ve
# KIRMIZIDIR (sessizce "0 jeton" saymak kapiyi kor ederdi).
CALL_ANY = re.compile(r"\bo\.(puts|ch|num|vec2|vec|str)\(")
STR_LIT = re.compile(r'^"((?:[^"\\]|\\.)*)"$')
TERNARY = re.compile(r'^[^?"]*\?\s*"((?:[^"\\]|\\.)*)"\s*:\s*"((?:[^"\\]|\\.)*)"$')
CHAR_LIT = re.compile(r"^'((?:[^'\\]|\\.))'$")
IDENT = re.compile(r"^[A-Za-z_]\w*$")
# Eski kapinin satir basi kalibi. Yalniz --oz-sinama kullanir: tirnakli
# operandli satirin eski kapida GORUNMEDIGINI (sinamanin gercekten yeni bir
# seyi olctugunu) kanitlamak icin.
ESKI_KW_START = re.compile(r'o\.puts\("(\s*[a-z0-9_-]+(?:\s+[a-z0-9_-]+)*\s*)(\\n)?"\)')

# Cozulemeyen ifadenin temsilcisi: bosluk/tirnak/satir sonu icermeyen tek parca.
OPAQUE = "\x01"


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "0": "\0"}.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def call_arg(code, start):
    """`(` sonrasindan eslesen `)`e kadar arguman metni ve bitis indeksi."""
    depth, i, q = 1, start, None
    while i < len(code):
        c = code[i]
        if q:
            if c == "\\":
                i += 2
                continue
            if c == q:
                q = None
        elif c in "\"'":
            q = c
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return code[start:i].strip(), i + 1
        i += 1
    return code[start:].strip(), len(code)


def shape(text):
    """Bir parcanin sekli: (jeton sayisi, satir sonu var mi). Dal karsilastirmasi icin."""
    toks = 0
    in_word = False
    for ch in text.replace('"', " S "):
        if ch in " \t\n":
            in_word = False
        elif not in_word:
            toks += 1
            in_word = True
    return toks, "\n" in text


def entity_body(src):
    # Govdeyi PARANTEZ DENGESIYLE kes. "sonraki void'e kadar" demek dunya
    # yazicisina tasiyor ve oradaki "kamera" satirini varlik dalina sayiyordu.
    start = src.index("void write_entity(")
    depth, j = 0, start
    for j in range(src.index("{", start), len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                break
    return src[start:j + 1], src[:start].count("\n")


def writer_lines(src, problems=None):
    """[(anahtar, jeton_sayisi, kaynak_satir)] dondurur. Sayilamayan yapilar
    `problems` listesine (verildiyse) eklenir."""
    if problems is None:
        problems = []
    body, base = entity_body(src)
    lines = [ln.split("//")[0] for ln in body.split("\n")]
    # Degisken -> atanan dizgiler (`const char *lt = " nokta\n";` / `lt = " yonlu\n";`).
    assigns = {}
    for code in lines:
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*=\s*("(?:[^"\\]|\\.)*")\s*;', code):
            assigns.setdefault(m.group(1), []).append(unescape(m.group(2)[1:-1]))

    # 1) Akis: [(metin, kaynak_satir)]
    frags = []
    for ln, code in enumerate(lines, 1):
        src_ln = base + ln
        for m in CALL_ANY.finditer(code):
            kind = m.group(1)
            arg, _ = call_arg(code, m.end())
            if kind in EMIT:
                frags.append((EMIT[kind], src_ln))
            elif kind == "ch":
                cm = CHAR_LIT.match(arg)
                frags.append((unescape(cm.group(1)) if cm else OPAQUE, src_ln))
            else:  # puts
                sm, tm = STR_LIT.match(arg), TERNARY.match(arg)
                if sm:
                    frags.append((unescape(sm.group(1)), src_ln))
                elif tm:
                    a, b = unescape(tm.group(1)), unescape(tm.group(2))
                    if shape(a) != shape(b):
                        problems.append("scene.cpp:%d: o.puts(... ? %r : %r) dallari FARKLI sekilde "
                                        "(jeton/satir sonu) -> satirin jeton sayisi dala bagli" % (src_ln, a, b))
                    frags.append((a, src_ln))
                elif IDENT.match(arg) and arg in assigns:
                    vals = assigns[arg]
                    for v in vals[1:]:
                        if shape(v) != shape(vals[0]):
                            problems.append("scene.cpp:%d: o.puts(%s) degiskeninin atamalari FARKLI sekilde: %r / %r"
                                            % (src_ln, arg, vals[0], v))
                    frags.append((vals[0], src_ln))
                else:
                    frags.append((OPAQUE, src_ln))  # tirnak disindaysa asagida KIRMIZI

    # 2) Jetonla (ayristiricinin split() kurali) ve satirlara bol.
    out = []
    toks, first_ln, in_q, cur = [], None, False, None

    def close_line(ln):
        nonlocal toks, first_ln, cur
        if cur is not None:
            toks.append(cur)
            cur = None
        if toks:
            out.append((toks[0], len(toks), first_ln))
        toks, first_ln = [], None

    for text, ln in frags:
        for ch in text:
            if in_q:
                if ch == '"':
                    in_q = False
                    toks.append(cur)
                    cur = None
                elif ch == "\n":
                    problems.append("scene.cpp:%d: tirnak kapanmadan satir bitti -> ayristirici "
                                    "'tirnak kapanmadi' der, sahne GERI OKUNAMAZ" % ln)
                    in_q = False
                    close_line(ln)  # yarim jeton sayilir: tek sorun, turetilmis sayi farki degil
                else:
                    cur += ch
                continue
            if ch == '"':
                if cur is not None:
                    toks.append(cur)
                in_q, cur = True, ""
                if first_ln is None:
                    first_ln = ln
            elif ch == "\n":
                close_line(ln)
            elif ch in " \t\r":
                if cur is not None:
                    toks.append(cur)
                    cur = None
            else:
                if ch == OPAQUE:
                    problems.append("scene.cpp:%d: cozulemeyen o.puts/o.ch ifadesi tirnak DISINDA -> "
                                    "jeton sayilamaz (dizgi degismezi ya da atanmis degisken kullan)" % ln)
                if cur is None:
                    cur = ""
                    if first_ln is None:
                        first_ln = ln
                cur += ch
    if in_q:
        problems.append("scene.cpp: write_entity tirnak ACIK bitiyor")
    close_line(None)
    return out


# --------------------------------------------------------------------------
# Ayristirici tarafi: tok_is(t[0], "kw") dallari
# --------------------------------------------------------------------------
BRANCH = re.compile(r'tok_is\(t\[0\],\s*"([a-z0-9_-]+)"\)')
NCHECK = re.compile(r"\bn\s*(!=|<|>|<=|>=)\s*(\d+)")


def parser_expect(src):
    """{anahtar: (op, sayi, satir)} dondurur."""
    out = {}
    lines = src.split("\n")
    for i, line in enumerate(lines):
        m = BRANCH.search(line)
        if not m:
            continue
        kw = m.group(1)
        # Jeton sayisi kontrolu ayni satirda ya da sonraki birkac satirda.
        # YALNIZ p.fail'e giden kontrol sayilir: "if (n >= 2 && tok_is(...))"
        # gibi satirlar bir dal secimidir, jeton SOZLESMESI degil -- onlari
        # sozlesme sanmak govde satirini yanlis yere kirmizi yapiyordu.
        for j in range(i, min(i + 4, len(lines))):
            code = lines[j].split("//")[0]
            if "p.fail" not in code:
                continue
            nm = NCHECK.search(code)
            if nm:
                out.setdefault(kw, (nm.group(1), int(nm.group(2)), j + 1))
                break
        else:
            out.setdefault(kw, (None, None, i + 1))
    return out


def max_tok(src):
    m = re.search(r"kMaxTok\s*=\s*(\d+)", src)
    return int(m.group(1)) if m else None


def check_format(src):
    """Yazici <-> ayristirici uyumu. (yazici_satirlari, ayristirici_dallari, sorunlar)."""
    problems = []
    cap = max_tok(src)
    w = writer_lines(src, problems)
    # Dallanan satirlarin sayisini elle bilinen GERCEK en buyukle degistir.
    w = [(kw, BRANCHY.get(kw, ntok), ln) for kw, ntok, ln in w]
    p = parser_expect(src)

    # 1) Yazicinin urettigi anahtarin ayristiricida dali var mi
    for kw, ntok, ln in w:
        if kw not in p:
            problems.append("yazici '%s' uretiyor, ayristiricida DAL YOK (scene.cpp:%d)" % (kw, ln))

    # 2) Jeton sayisi uyusuyor mu
    for kw, ntok, ln in w:
        if kw not in p:
            continue
        op, want, pln = p[kw]
        if want is None:
            continue
        # `if (n >= K) fail` -> n < K. Eskiden `ntok <= want` yaziyordu (bir
        # fazlayi kabul): bugun hicbir dal `n >=` ile fail etmiyor (2026-09-25),
        # yani sonucu degismedi, ama ilk kullanan yanlis yesil gorurdu.
        ok = {"!=": ntok == want, "<": ntok >= want, "<=": ntok > want,
              ">": ntok <= want, ">=": ntok < want}.get(op, True)
        if not ok:
            problems.append(
                "'%s': yazici %d jeton uretiyor, ayristirici 'n %s %d' istiyor "
                "(yazici scene.cpp:%d, ayristirici scene.cpp:%d)" % (kw, ntok, op, want, ln, pln))

    # 3) kMaxTok tavani
    if cap:
        for kw, ntok, ln in w:
            if ntok > cap:
                problems.append(
                    "'%s': %d jeton, kMaxTok=%d tavanini ASIYOR -> split() *bad=true, "
                    "sahne GERI OKUNAMAZ (scene.cpp:%d)" % (kw, ntok, cap, ln))
    return w, p, problems


# --------------------------------------------------------------------------
# Bilesen canlilik: her kSceneXxx biti icin YAZ + OKU + ESITLIK var mi.
#
# Bu kapi olmadan bir bilesen "panelde var ama diske hic yazilmiyor" halinde
# kalabiliyor ve kimse fark etmiyor. Olculdu: NavAgent, Joint, Skybox,
# RefProbe, Reverb -- besinin de tam calisan paneli vardi, content/scene.cpp
# icinde SIFIR kez geciyorlardi. Kullanici degerleri giriyor, kaydediyor,
# aciyor; hepsi gitmis, hata da yok.
# --------------------------------------------------------------------------
BIT_RE = re.compile(r"(kScene[A-Za-z]+)\s*=\s*1u\s*<<\s*\d+")
# Bayraklar (kSceneHidden/kSceneLocked) bilesen DEGIL: ayri enum, serilestirmesi
# "bayrak" satiriyla toplu yapilir.
NOT_COMPONENT = {"kSceneHidden", "kSceneLocked"}

# Nesne ozellikleri (E3) yaprak tablosuna GIREMEZ: ada gore sirali bir liste,
# props[k] baska varlikta baska ada karsilik gelir. Muafiyet KOSULLU: coklu
# duzenleme dosyasi ada gore birlestirmeyi (ekle/degistir + sil) KODDA
# cagirmiyorsa muafiyet yok ve alanlar "yayilmiyor" diye kirmizi. Yorumda
# gecen ad sayilmaz (yorumlar soyulur) -- "yapacagiz" demek yapmak degil.
PROPS_FIELDS = {"props", "prop_count"}
PROPS_MERGE_CALLS = ("scene_prop_set(", "scene_prop_remove(")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(ln.split("//")[0] for ln in text.split("\n"))


def entity_fields(hdr):
    body = re.search(r"struct SceneEntity\s*\{(.*?)\n\};", hdr, re.S).group(1)
    fields = []
    for line in body.split("\n"):
        l = line.split("//")[0].strip()
        m = re.match(r"^([A-Za-z_][\w:<>]*)\s+(.+);$", l)
        if not m:
            continue
        for part in m.group(2).split(","):
            n = re.match(r"\s*([A-Za-z_]\w*)", part)
            if n:
                fields.append(n.group(1))
    return fields


def multiedit_coverage_text(hdr, table):
    """Yaprak tablosu SceneEntity'nin TUM alanlarini kapsiyor mu. Kapsamazsa
    yeni bir alan coklu secimde SESSIZCE yayilmaz -- kullanici 5 nesne secip
    degeri degistirir, yalniz biri degisir."""
    fields = entity_fields(hdr)
    code = strip_comments(table)
    # Bilerek disarida: ad/ebeveyn hic yayilmaz, bit alanlari ayri islenir.
    exempt = {"name", "parent", "components", "flags"}
    merge_ok = all(c in code for c in PROPS_MERGE_CALLS)
    out = []
    for fld in fields:
        if fld in exempt:
            continue
        if fld in PROPS_FIELDS:
            if not merge_ok:
                out.append("SceneEntity::%s yaprak degil ve app/editor_multiedit.cpp ADA GORE "
                           "birlestirmiyor (%s cagrisi yok) -> coklu secimde ozellikler yayilmaz"
                           % (fld, " + ".join(PROPS_MERGE_CALLS)))
            continue
        if not re.search(r"offsetof\(SceneEntity, " + re.escape(fld) + r"[.)]", code):
            out.append("SceneEntity::%s coklu duzenleme tablosunda YOK -> coklu secimde "
                       "yayilmaz (app/editor_multiedit.cpp)" % fld)
    return out


def multiedit_coverage(root):
    with open(os.path.join(root, "content/scene.hpp"), encoding="utf-8") as f:
        hdr = f.read()
    path = os.path.join(root, "app/editor_multiedit.cpp")
    if not os.path.exists(path):
        return ["app/editor_multiedit.cpp yok"]
    with open(path, encoding="utf-8") as f:
        table = f.read()
    return multiedit_coverage_text(hdr, table)


def component_liveness(root, src):
    with open(os.path.join(root, "content/scene.hpp"), encoding="utf-8") as f:
        hdr = f.read()
    bits = [b for b in BIT_RE.findall(hdr) if b not in NOT_COMPONENT]

    write_body = src[src.index("void write_entity("):src.index("bool scene_entity_equal")]
    eq_body = src[src.index("bool scene_entity_equal"):]
    eq_body = eq_body[:eq_body.index("\n}")]
    parse_body = src[src.index("scene_parse"):]

    out = []
    for b in bits:
        has_write = b in write_body
        has_eq = b in eq_body
        has_parse = ("seen_comp |= " + b) in parse_body
        if not (has_write and has_eq and has_parse):
            missing = []
            if not has_write:
                missing.append("YAZ")
            if not has_eq:
                missing.append("ESITLIK")
            if not has_parse:
                missing.append("OKU")
            out.append("%s: %s yok -> bu bilesen .sahne'ye tam gitmiyor" % (b, "/".join(missing)))
    return bits, out


# --------------------------------------------------------------------------
# --oz-sinama: kapinin KENDI pozitif/negatif kontrolleri (CMake her derlemede
# kosturur). Her fikstur kucuk bir scene.cpp/scene.hpp parcasidir; kapi bozuk
# fiksturu YAKALAMALI, dogrusunu GECIRMELI. "Kapi yesil" demek ancak kapinin
# kirmiziyi gorebildigi gosterilmisse bir sey soyler (Tuzaklar 8bd).
# --------------------------------------------------------------------------
def _fixture(writer, parser):
    return ("constexpr size_t kMaxTok = 16;\n"
            "void write_entity(Out &o, const SceneEntity &e) {\n" + writer + "\n}\n"
            "bool scene_parse(const char *text) {\n" + parser + "\n}\n")


FIX_HDR = ("struct SceneEntity {\n  char name[4];\n  int32_t parent = -1;\n  uint32_t flags = 0;\n"
           "  uint32_t components = 0;\n  float a = 0;\n  SceneProp props[16] = {};\n  uint32_t prop_count = 0;\n};\n")


def oz_sinama():
    cases = [
        # (ad, fikstur, beklenen sorun sayisi, sorunda gecmesi gereken metin)
        ("tirnakli operand: yazici 2 jeton, ayristirici 3 ister -> KIRMIZI (pozitif kontrol)",
         _fixture('  o.puts("  betik \\""); o.puts(e.script_file); o.puts("\\"\\n");',
                  '  if (tok_is(t[0], "betik")) {\n    if (n != 3) return p.fail("betik");\n  }'),
         1, "'betik': yazici 2 jeton"),
        ("tirnakli operand + kacisli ucl: 3 jeton = 3 -> YESIL",
         _fixture('  o.puts("  betik \\""); o.puts(e.script_file); o.puts(e.on ? "\\" etkin\\n" : "\\" kapali\\n");',
                  '  if (tok_is(t[0], "betik")) {\n    if (n != 3) return p.fail("betik");\n  }'),
         0, None),
        ("ucl dallari farkli sekil -> KIRMIZI",
         _fixture('  o.puts("  a "); o.num(e.x); o.puts(e.on ? " evet\\n" : " hayir yok\\n");',
                  '  if (tok_is(t[0], "a")) {\n    if (n != 3) return p.fail("a");\n  }'),
         1, "dallari FARKLI"),
        ("tirnak kapanmadan satir sonu -> KIRMIZI",
         _fixture('  o.puts("  a \\""); o.puts(e.x); o.ch(\'\\n\');',
                  '  if (tok_is(t[0], "a")) {\n    if (n != 2) return p.fail("a");\n  }'),
         1, "tirnak kapanmadan"),
        ("tirnak disinda cozulemeyen ifade -> KIRMIZI",
         _fixture('  o.puts("  a "); o.puts(ad()); o.ch(\'\\n\');',
                  '  if (tok_is(t[0], "a")) {\n    if (n != 2) return p.fail("a");\n  }'),
         1, "cozulemeyen"),
        ("atanmis degisken cozulur ve satiri kapatir -> YESIL",
         _fixture('  const char *lt = " nokta\\n";\n  if (e.y) lt = " yonlu\\n";\n'
                  '  o.puts("  isik "); o.num(e.x); o.puts(lt);\n  o.puts("  koni "); o.num(e.a); o.ch(\'\\n\');',
                  '  if (tok_is(t[0], "isik")) {\n    if (n != 3) return p.fail("isik");\n  }\n'
                  '  if (tok_is(t[0], "koni")) {\n    if (n != 2) return p.fail("koni");\n  }'),
         0, None),
        ("ozellik satiri: str + sayi = 3 -> YESIL; nokta 5 isterken 4 -> KIRMIZI",
         _fixture('  o.puts("  ozellik_sayi "); o.str(p.name); o.ch(\' \'); o.num(p.v[0]); o.ch(\'\\n\');\n'
                  '  o.puts("  ozellik_nokta "); o.str(p.name); o.ch(\' \'); o.num(p.v[0]); o.ch(\' \'); o.num(p.v[1]); o.ch(\'\\n\');',
                  '  if (tok_is(t[0], "ozellik_sayi")) {\n    if (n != 3) return p.fail("s");\n  }\n'
                  '  if (tok_is(t[0], "ozellik_nokta")) {\n    if (n != 5) return p.fail("n");\n  }'),
         1, "'ozellik_nokta': yazici 4 jeton"),
    ]
    fails = 0
    for name, src, want_n, want_sub in cases:
        _, _, probs = check_format(src)
        ok = len(probs) == want_n and (want_sub is None or any(want_sub in p for p in probs))
        print("scene_check --oz-sinama: %s: %d sorun %s" % (name, len(probs), "OK" if ok else "HATA"))
        if not ok:
            fails += 1
            for x in probs:
                print("    " + x)

    # Tirnakli satir ESKI kapida gorunmuyordu: ilk fiksturun satir basi eski
    # kalipla eslesmemeli, yeni benzetim onu gormeli. Olmazsa sinama yeni bir
    # sey olcmuyordur.
    first = cases[0][1]
    old_blind = not ESKI_KW_START.search(first)
    new_sees = any(kw == "betik" for kw, _, _ in writer_lines(first))
    ok = old_blind and new_sees
    print("scene_check --oz-sinama: tirnakli satir eski kapida gorunmez (%s), yenisinde gorunur (%s) %s"
          % ("evet" if old_blind else "HAYIR", "evet" if new_sees else "HAYIR", "OK" if ok else "HATA"))
    fails += 0 if ok else 1

    # Coklu duzenleme kapsami: ozellik muafiyeti KOSULLU.
    me_cases = [
        ("yaprak eksik (a) -> KIRMIZI", "offsetof(SceneEntity, props) scene_prop_set( scene_prop_remove(", 1, "::a "),
        ("ozellik birlestirmesi yok -> props + prop_count KIRMIZI", "offsetof(SceneEntity, a)", 2, "ADA GORE"),
        ("yalniz YORUMDA scene_prop_set -> yine KIRMIZI",
         "offsetof(SceneEntity, a)\n// scene_prop_set( scene_prop_remove( yapilacak", 2, "ADA GORE"),
        ("ada gore birlestirme kodda -> YESIL", "offsetof(SceneEntity, a)\nscene_prop_set(x); scene_prop_remove(y);", 0, None),
    ]
    for name, table, want_n, want_sub in me_cases:
        probs = multiedit_coverage_text(FIX_HDR, table)
        ok = len(probs) == want_n and (want_sub is None or any(want_sub in p for p in probs))
        print("scene_check --oz-sinama: coklu duzenleme, %s: %d sorun %s" % (name, len(probs), "OK" if ok else "HATA"))
        if not ok:
            fails += 1
            for x in probs:
                print("    " + x)
    total = len(cases) + 1 + len(me_cases)
    print("scene_check --oz-sinama: %d/%d sinama tuttu" % (total - fails, total))
    return 1 if fails else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--oz-sinama":
        return oz_sinama()
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    src = read(root)
    cap = max_tok(src)
    w, p, problems = check_format(src)

    # 4) Bilesen canliligi
    bits, dead = component_liveness(root, src)
    problems.extend(dead)

    # 5) Coklu duzenleme tablosu kapsami
    problems.extend(multiedit_coverage(root))

    print("sahne bicim kapisi: %d yazici satiri, %d ayristirici dali, %d bilesen biti, kMaxTok=%s"
          % (len(w), len(p), len(bits), cap))
    if not problems:
        print("sahne bicim kapisi: 0 uyusmazlik")
        return 0
    print("sahne bicim kapisi: %d UYUSMAZLIK" % len(problems))
    for x in problems:
        print("  " + x)
    return 1


if __name__ == "__main__":
    sys.exit(main())
