#!/usr/bin/env python3
"""Tulpar Engine — Android yerli "Google ile giris" paketi (KOPRU §16) + motorun Java bilesenleri.

NativeActivity oyunlarinin Java'si/Gradle'i yok. Bu arac Google'in hesap secicisini (Credential Manager:
androidx.credentials + play-services-auth + googleid) Gradle'siz APK'ya sokar:

    tools/android_google.py hazirla [CIKTI]      # varsayilan yapi/tulpar-ext/android/google
        Maven'dan kilitli surumleri indirir (tools/android_google.kilit: ad + sha256; onbellek
        ~/.cache/tulpar-engine/maven), platform/android/java/**.java'yi derler, R8 ile kucultur ->
        classes.dex; kutuphane kaynaklarini aapt2 ile derler (res/*.zip), manifest parcalarini birlestirir.
        Motorun kendi Java bilesenleri de buradan girer: ZamanliBildirim alicisi (KOPRU §15.1, MOTOR_*).
    tools/android_google.py ekle STAGE [PAKET]   # `tulpar build --target=android` staging dizinine
        manifest: hasCode="true" + kutuphane etkinlikleri/saglayicilari/izinleri; STAGE/android_lib/
        (classes.dex, res/*.zip, paketler.txt). TulparLang android/package_apk.sh android_lib'i gorunce
        kaynaklari -R ile baglar, kutuphane R siniflarini SON kaynak kimlikleriyle uretip dex'e katar.
    tools/android_google.py kilitle              # bagimlilik agacini yeniden coz, kilidi yaz (surum yukseltme)

Profiller (--profil, varsayilan giris): her profil kendi kokleri, kilidi, Java siniflari ve ciktisiyla.
    giris    Google ile giris (GoogleGiris, KOPRU §16) + motorun ZamanliBildirim alicisi (§15.1)
             kilit tools/android_google.kilit, cikti yapi/tulpar-ext/android/google
    oyunlar  Google Play Oyunlar v2: giris + kayitli oyun (PlayOyunlar, KOPRU §17); bildirim alicisi YOK
             (izin kumesine RECEIVE_BOOT_COMPLETED girmez). kilit tools/android_oyunlar.kilit,
             cikti yapi/tulpar-ext/android/oyunlar. ekle --oyunlar-kimligi <Play Console oyun kimligi>
             zorunlu: manifeste com.google.android.gms.games.APP_ID (+ res/values dizgesi) yazilir.

R siniflari R8'e KUTUPHANE olarak verilir (cikti dex'te yok): kaynak kimlikleri ancak oyunun son
aapt2 link'inde belli olur, R orada uretilir. Boylece kutuphane kodundaki R.string.x gibi atiflar
oyunun kendi kaynaklariyla cakismadan dogru kimligi bulur.
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile, urllib.request, zipfile
import xml.etree.ElementTree as ET

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KILIT = os.path.join(KOK, "tools", "android_google.kilit")
JAVA_KOK = os.path.join(KOK, "platform", "android", "java")
ONBELLEK = os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "tulpar-engine", "maven")
GOOGLE = "https://dl.google.com/android/maven2/"
CENTRAL = "https://repo1.maven.org/maven2/"
KOKLER = ["androidx.credentials:credentials:1.6.0",
          "androidx.credentials:credentials-play-services-auth:1.6.0",
          "com.google.android.libraries.identity.googleid:googleid:1.2.1"]
# Kotlin 1.8+ stdlib'i jdk7/jdk8'i icerir (ayri jar = cift sinif); -common yalniz meta veri.
DISLA = {"org.jetbrains.kotlin:kotlin-stdlib-jdk7", "org.jetbrains.kotlin:kotlin-stdlib-jdk8", "org.jetbrains.kotlin:kotlin-stdlib-common"}
# Oyun parmak izi/biyometri kullanmaz: biometric kutuphanesinin izinleri manifeste YAZILMAZ (Play
# listesinde "parmak izi" gorunmesin). Kod yolu biyometri istemez (yalniz Google kimligi).
IZIN_DISLA = {"android.permission.USE_BIOMETRIC", "android.permission.USE_FINGERPRINT"}
# Motorun kendi Java bilesenleri (Maven kutuphanesi degil): zamanli bildirim alicisi (KOPRU §15.1). Disa acik
# degil; AlarmManager PendingIntent'i ve sistemin BOOT_COMPLETED yayini (system uid) yine ulasir. Yeniden
# baslatmada alarmlar silindigi icin alici kayitlari SharedPreferences'tan yeniden kurar.
MOTOR_UST = ['<uses-permission android:name="android.permission.RECEIVE_BOOT_COMPLETED" />']
MOTOR_UYGULAMA = ['<receiver android:name="dev.tulparlang.engine.ZamanliBildirim" android:exported="false">'
                  '<intent-filter><action android:name="android.intent.action.BOOT_COMPLETED" />'
                  '<action android:name="android.intent.action.MY_PACKAGE_REPLACED" /></intent-filter></receiver>']
# Profil: kokler, kilit dosyasi, derlenen Java dosyalari, R8 keep kurallari, motor manifest parcalari, cikti dizini.
PROFILLER = {
    "giris": {"kokler": KOKLER, "kilit": KILIT, "java": ["GoogleGiris.java", "ZamanliBildirim.java"],
              "keep": ["-keep class dev.tulparlang.engine.GoogleGiris { public static *; }",
                       "-keep class dev.tulparlang.engine.ZamanliBildirim { public *; }"],
              "ust": MOTOR_UST, "uyg": MOTOR_UYGULAMA, "cikti": "google"},
    "oyunlar": {"kokler": ["com.google.android.gms:play-services-games-v2:22.1.0"],
                "kilit": os.path.join(KOK, "tools", "android_oyunlar.kilit"), "java": ["PlayOyunlar.java"],
                "keep": ["-keep class dev.tulparlang.engine.PlayOyunlar { public static *; }"],
                "ust": [], "uyg": [], "cikti": "oyunlar"},
}
A = "{http://schemas.android.com/apk/res/android}"
NS = "{http://maven.apache.org/POM/4.0.0}"


def sdk_araclari():
    sdk = os.environ.get("TULPAR_ANDROID_SDK") or os.environ.get("ANDROID_HOME") or os.path.expanduser("~/Android/Sdk")
    bts = sorted((d for d in os.listdir(os.path.join(sdk, "build-tools"))), key=lambda v: [int(x) if x.isdigit() else 0 for x in re.split(r"[.-]", v)])
    bt = os.path.join(sdk, "build-tools", bts[-1])
    plats = sorted(os.listdir(os.path.join(sdk, "platforms")), key=lambda v: [int(x) if x.isdigit() else 0 for x in re.split(r"[-.]", v.replace("android-", ""))])
    return {"aapt2": os.path.join(bt, "aapt2"), "d8jar": os.path.join(bt, "lib", "d8.jar"),
            "android_jar": os.path.join(sdk, "platforms", plats[-1], "android.jar"), "bt": os.path.basename(bt)}


def kos(arg, **k):
    r = subprocess.run(arg, capture_output=True, text=True, **k)
    if r.returncode != 0:
        sys.stderr.write(" ".join(map(str, arg))[:400] + "\n" + r.stdout[-3000:] + r.stderr[-3000:])
        raise SystemExit("HATA: komut basarisiz: " + os.path.basename(str(arg[0])))
    return r.stdout


# ---------------------------------------------------------------- kilit ------
def _al(url):
    try:
        return urllib.request.urlopen(url, timeout=60).read()
    except Exception:
        return None


def _vkey(v):
    return [int(x) if x.isdigit() else -1 for x in re.split(r"[.-]", v)]


def kilitle(pr):
    """Maven POM'larindan derleme+calisma bagimliliklarini coz (en yuksek surum kazanir), kilidi yaz."""
    secilen, kuyruk = {}, [tuple(x.split(":")) for x in pr["kokler"]]
    while kuyruk:
        g, a, v = kuyruk.pop()
        v = v.strip("[]")
        if f"{g}:{a}" in DISLA or ((g, a) in secilen and _vkey(secilen[(g, a)][0]) >= _vkey(v)):
            continue
        base = p = None
        for b in (GOOGLE, CENTRAL):
            d = _al(f"{b}{g.replace('.', '/')}/{a}/{v}/{a}-{v}.pom")
            if d:
                base, p = b, ET.fromstring(d)
                break
        if p is None:
            raise SystemExit(f"HATA: POM yok {g}:{a}:{v}")
        secilen[(g, a)] = (v, base, p.findtext(NS + "packaging") or "jar")
        ds = p.find(NS + "dependencies")
        for d in (list(ds) if ds is not None else []):
            if (d.findtext(NS + "scope") or "compile") not in ("compile", "runtime") or d.findtext(NS + "optional") == "true":
                continue
            dv = d.findtext(NS + "version")
            if dv and "$" not in dv:
                kuyruk.append((d.findtext(NS + "groupId"), d.findtext(NS + "artifactId"), dv))
    satir = []
    for (g, a), (v, base, pk) in sorted(secilen.items()):
        if pk == "pom":
            continue
        uz = "aar" if pk == "aar" else "jar"
        url = f"{base}{g.replace('.', '/')}/{a}/{v}/{a}-{v}.{uz}"
        veri = _al(url)
        if veri is None:
            raise SystemExit("HATA: indirilemedi " + url)
        satir.append(f"{g}:{a}:{v}:{uz} {hashlib.sha256(veri).hexdigest()} {url}")
    with open(pr["kilit"], "w") as f:
        f.write("# tools/android_google.py kilitle — elle duzenleme; surum: KOKLER\n" + "\n".join(satir) + "\n")
    print(f"kilit: {len(satir)} kutuphane -> {os.path.relpath(pr['kilit'], KOK)}")


def indir(pr):
    os.makedirs(ONBELLEK, exist_ok=True)
    out = []
    for l in open(pr["kilit"]):
        if not l.strip() or l.startswith("#"):
            continue
        ad, sha, url = l.split()
        yol = os.path.join(ONBELLEK, ad.replace(":", "__") + "." + ad.rsplit(":", 1)[1])
        if not (os.path.exists(yol) and hashlib.sha256(open(yol, "rb").read()).hexdigest() == sha):
            veri = _al(url)
            if veri is None or hashlib.sha256(veri).hexdigest() != sha:
                raise SystemExit(f"HATA: {ad} indirilemedi ya da sha256 tutmuyor ({url})")
            with open(yol + ".gecici", "wb") as f:
                f.write(veri)
            os.replace(yol + ".gecici", yol)
        out.append((ad, yol))
    return out


# ---------------------------------------------------------------- hazirla ----
def _manifest_parcalari(aar_dizinleri):
    """Kutuphane manifestlerinden <application> cocuklari + ust duzey izin/sorgu ogeleri (metin)."""
    ET.register_namespace("android", A[1:-1])
    ET.register_namespace("tools", "http://schemas.android.com/tools")
    uygulama, ust, gorulen = [], [], set()
    for d in aar_dizinleri:
        mf = os.path.join(d, "AndroidManifest.xml")
        if not os.path.exists(mf):
            continue
        k = ET.parse(mf).getroot()
        for e in list(k):
            if e.tag in ("uses-permission", "permission", "queries", "uses-feature"):
                ad = e.get(A + "name", "")
                if ad in IZIN_DISLA:
                    continue
                anahtar = (e.tag, ad, ET.tostring(e) if e.tag == "queries" else b"")
                if anahtar in gorulen:
                    continue
                gorulen.add(anahtar)
                ust.append(ET.tostring(e, encoding="unicode"))
        app = k.find("application")
        for e in (list(app) if app is not None else []):
            anahtar = (e.tag, e.get(A + "name", ""))
            if anahtar in gorulen:
                # ayni saglayici (androidx.startup) birden cok kutuphanede: meta-data cocuklari birlesir
                if e.tag == "provider":
                    for i, s in enumerate(uygulama):
                        if f'android:name="{anahtar[1]}"' in s and s.startswith("<provider"):
                            cocuk = "".join(ET.tostring(c, encoding="unicode") for c in e)
                            uygulama[i] = s.replace("</provider>", cocuk + "</provider>") if "</provider>" in s else s
                continue
            gorulen.add(anahtar)
            uygulama.append(ET.tostring(e, encoding="unicode"))
    # tools:* oznitelikleri yalniz Gradle birlestiricisi icin (son manifestte anlamsiz; onek baglanmamis olur)
    temiz = lambda s: re.sub(r'\s+tools:[A-Za-z]+="[^"]*"', "", re.sub(r'\s+xmlns:(android|tools)="[^"]+"', "", s))
    return [temiz(s) for s in ust], [temiz(s) for s in uygulama]


def hazirla(cikti, pr):
    ar = sdk_araclari()
    kut = indir(pr)
    is_ = tempfile.mkdtemp(prefix="tulpar_google_")
    try:
        jarlar, aarlar, kurallar, paketler, res_zip = [], [], [], [], []
        os.makedirs(os.path.join(is_, "res"), exist_ok=True)
        for ad, yol in kut:
            if yol.endswith(".jar"):
                jarlar.append(yol)
                continue
            d = os.path.join(is_, "aar", os.path.basename(yol)[:-4])
            zipfile.ZipFile(yol).extractall(d)
            aarlar.append(d)
            if os.path.exists(os.path.join(d, "classes.jar")):
                jarlar.append(os.path.join(d, "classes.jar"))
            for j in sorted(os.listdir(os.path.join(d, "libs"))) if os.path.isdir(os.path.join(d, "libs")) else []:
                jarlar.append(os.path.join(d, "libs", j))
            if os.path.exists(os.path.join(d, "proguard.txt")):
                kurallar.append(os.path.join(d, "proguard.txt"))
            pk = ET.parse(os.path.join(d, "AndroidManifest.xml")).getroot().get("package")
            rd = os.path.join(d, "res")
            if os.path.isdir(rd) and any(os.scandir(rd)):
                z = os.path.join(is_, "res", f"{len(res_zip):02d}_{pk}.zip")
                kos([ar["aapt2"], "compile", "--dir", rd, "-o", z])
                res_zip.append(z)
                paketler.append(pk)
        # R siniflari (kutuphane olarak; gecici kimlikler): tum kaynaklarla sahte bir uygulama bagla
        ust, uyg = _manifest_parcalari(aarlar)
        ust += pr["ust"]
        uyg += pr["uyg"]
        sahte = os.path.join(is_, "sahte.xml")
        with open(sahte, "w") as f:
            f.write('<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="dev.tulparlang.googlegecici">'
                    '<uses-sdk android:minSdkVersion="26" android:targetSdkVersion="36"/>' + "".join(ust).replace("${applicationId}", "dev.tulparlang.googlegecici")
                    + '<application android:hasCode="true">' + "".join(uyg).replace("${applicationId}", "dev.tulparlang.googlegecici") + "</application></manifest>")
        gen = os.path.join(is_, "gen")
        kural_manifest = os.path.join(is_, "manifest.pro")
        link = [ar["aapt2"], "link", "-o", os.path.join(is_, "sahte.apk"), "-I", ar["android_jar"], "--manifest", sahte,
                "--auto-add-overlay", "--java", gen, "--proguard", kural_manifest]
        if paketler:
            link += ["--extra-packages", ":".join(paketler)]
        for z in res_zip:
            link += ["-R", z]
        kos(link)
        rcls = os.path.join(is_, "rcls")
        rjava = [os.path.join(dp, f) for dp, _, fs in os.walk(gen) for f in fs if f.endswith(".java")]
        kos(["javac", "--release", "11", "-nowarn", "-d", rcls] + rjava)
        rjar = os.path.join(is_, "r.jar")
        with zipfile.ZipFile(rjar, "w") as z:
            for dp, _, fs in os.walk(rcls):
                for f in fs:
                    z.write(os.path.join(dp, f), os.path.relpath(os.path.join(dp, f), rcls))
        # Motorun Java'si
        cls = os.path.join(is_, "cls")
        kaynak = [os.path.join(dp, f) for dp, _, fs in os.walk(JAVA_KOK) for f in fs if f in pr["java"]]
        if len(kaynak) != len(pr["java"]):
            raise SystemExit(f"HATA: profilin Java dosyalari eksik: {pr['java']} ({JAVA_KOK})")
        cp = os.pathsep.join([ar["android_jar"], rjar] + jarlar)
        kos(["javac", "--release", "11", "-nowarn", "-cp", cp, "-d", cls] + kaynak)
        cjar = os.path.join(is_, "motor.jar")
        with zipfile.ZipFile(cjar, "w") as z:
            for dp, _, fs in os.walk(cls):
                for f in fs:
                    z.write(os.path.join(dp, f), os.path.relpath(os.path.join(dp, f), cls))
        # Ayni sinif iki jarda (androidx.annotation ile -jvm gibi): ilk goren kazanir
        tekil, gorulen = [], set()
        for j in jarlar:
            zj = zipfile.ZipFile(j)
            adlar = [n for n in zj.namelist() if n.endswith(".class") and not n.startswith("META-INF/")]
            if not adlar:
                continue
            if any(n in gorulen for n in adlar):
                yeni = os.path.join(is_, "tekil", f"{len(tekil)}.jar")
                os.makedirs(os.path.dirname(yeni), exist_ok=True)
                with zipfile.ZipFile(yeni, "w") as zo:
                    for n in zj.namelist():
                        if n.endswith(".class") and n in gorulen:
                            continue
                        zo.writestr(n, zj.read(n))
                j = yeni
            gorulen.update(adlar)
            tekil.append(j)
        pro = os.path.join(is_, "motor.pro")
        with open(pro, "w") as f:
            f.write("\n".join(pr["keep"]) + "\n"
                    "-dontobfuscate\n-ignorewarnings\n-dontwarn **\n"
                    "-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod\n")
        dexd = os.path.join(is_, "dex")
        os.makedirs(dexd)
        r8 = ["java", "-Xmx2g", "-cp", ar["d8jar"], "com.android.tools.r8.R8", "--release", "--min-api", "26",
              "--lib", ar["android_jar"], "--lib", rjar, "--output", dexd, "--pg-conf", pro, "--pg-conf", kural_manifest]
        for k in kurallar:
            r8 += ["--pg-conf", k]
        kos(r8 + [cjar] + tekil)
        # Cikti
        if os.path.isdir(cikti):
            shutil.rmtree(cikti)
        os.makedirs(os.path.join(cikti, "res"))
        dexler = sorted(f for f in os.listdir(dexd) if f.endswith(".dex"))
        for f in dexler:
            shutil.copy(os.path.join(dexd, f), os.path.join(cikti, f))
        for z in res_zip:
            shutil.copy(z, os.path.join(cikti, "res", os.path.basename(z)))
        with open(os.path.join(cikti, "paketler.txt"), "w") as f:
            f.write("\n".join(paketler) + "\n")
        with open(os.path.join(cikti, "manifest_ust.xml"), "w") as f:
            f.write("\n".join(ust) + "\n")
        with open(os.path.join(cikti, "manifest_uygulama.xml"), "w") as f:
            f.write("\n".join(uyg) + "\n")
        bilgi = {"kilit_sha256": hashlib.sha256(open(pr["kilit"], "rb").read()).hexdigest(),
                 "java_sha256": hashlib.sha256(b"".join(open(k, "rb").read() for k in sorted(kaynak))).hexdigest(),
                 "dex": {f: os.path.getsize(os.path.join(cikti, f)) for f in dexler}, "paket": len(paketler), "build_tools": ar["bt"]}
        with open(os.path.join(cikti, "bilgi.json"), "w") as f:
            json.dump(bilgi, f, indent=1)
        print(f"google paketi: {cikti} — dex {bilgi['dex']}, kaynak paketi {len(paketler)}, "
              f"izin/sorgu {len(ust)}, bilesen {len(uyg)}")
    finally:
        shutil.rmtree(is_, ignore_errors=True)


# ---------------------------------------------------------------- ekle -------
def ekle(stage, paket, oyunlar_kimligi=""):
    mf = os.path.join(stage, "AndroidManifest.xml")
    if not os.path.exists(mf):
        raise SystemExit("HATA: staging manifesti yok: " + mf)
    if not os.path.exists(os.path.join(paket, "paketler.txt")):
        raise SystemExit(f"HATA: Google paketi yok: {paket} (once: tools/android_google.py hazirla)")
    s = open(mf, encoding="utf-8").read()
    if "android_lib:google" in s:
        raise SystemExit("HATA: Google paketi bu staging'e zaten eklenmis")
    pk = re.search(r'package="([^"]+)"', s).group(1)
    ust = open(os.path.join(paket, "manifest_ust.xml")).read().replace("${applicationId}", pk)
    uyg = open(os.path.join(paket, "manifest_uygulama.xml")).read().replace("${applicationId}", pk)
    if 'android:hasCode="false"' not in s:
        raise SystemExit('HATA: manifestte android:hasCode="false" yok (derleyici bicimi degismis?)')
    s = s.replace('android:hasCode="false"', 'android:hasCode="true"', 1)
    s = s.replace("  <application ", "  <!-- android_lib:google (tulpar-engine tools/android_google.py) -->\n  " + ust.replace("\n", "\n  ") + "\n  <application ", 1)
    oyunlar = "PlayGamesInitProvider" in uyg   # oyunlar profili (play-services-games-v2 saglayicisi)
    if oyunlar:
        # Play Oyunlar: oyun kimligi (Play Console > Play Games Services > Yapilandirma) olmadan SDK baslamaz.
        # Sayisal deger dogrudan android:value'ya yazilamaz (tamsayi olur): res/values dizgesi uzerinden.
        if not re.fullmatch(r"[0-9]{6,20}", oyunlar_kimligi or ""):
            raise SystemExit("HATA: oyunlar profili --oyunlar-kimligi <sayisal Play Oyunlar kimligi> ister")
        uyg += '\n<meta-data android:name="com.google.android.gms.games.APP_ID" android:value="@string/tulpar_oyunlar_kimligi" />'
        rv = os.path.join(stage, "res", "values")
        os.makedirs(rv, exist_ok=True)
        with open(os.path.join(rv, "tulpar_oyunlar.xml"), "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                    f'  <string name="tulpar_oyunlar_kimligi" translatable="false">{oyunlar_kimligi}</string>\n</resources>\n')
    s = s.replace("  </application>", "    " + uyg.replace("\n", "\n    ") + "\n  </application>", 1)
    open(mf, "w", encoding="utf-8").write(s)
    hedef = os.path.join(stage, "android_lib")
    if os.path.isdir(hedef):
        shutil.rmtree(hedef)
    shutil.copytree(paket, hedef)
    print(f"google paketi eklendi: {stage} (paket {pk}, hasCode=true)")


def _secenek(arg, ad):
    if ad in arg:
        i = arg.index(ad)
        if i + 1 >= len(arg):
            raise SystemExit(f"HATA: {ad} deger ister")
        v = arg[i + 1]
        del arg[i:i + 2]
        return v
    return ""


def main():
    arg = sys.argv[1:]
    profil = _secenek(arg, "--profil") or "giris"
    kimlik = _secenek(arg, "--oyunlar-kimligi")
    if not arg or arg[0] not in ("hazirla", "ekle", "kilitle") or profil not in PROFILLER:
        print(__doc__)
        return 2
    pr = PROFILLER[profil]
    varsayilan = os.path.join(KOK, "yapi", "tulpar-ext", "android", pr["cikti"])
    if arg[0] == "kilitle":
        kilitle(pr)
    elif arg[0] == "hazirla":
        hazirla(arg[1] if len(arg) > 1 else varsayilan, pr)
    else:
        if len(arg) < 2:
            raise SystemExit("kullanim: android_google.py ekle STAGE [PAKET] [--profil oyunlar --oyunlar-kimligi N]")
        ekle(arg[1], arg[2] if len(arg) > 2 else varsayilan, kimlik)
    return 0


if __name__ == "__main__":
    sys.exit(main())
