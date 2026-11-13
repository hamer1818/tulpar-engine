#!/usr/bin/env python3
"""Tulpar oyunu — Google Play yayın aracı (motor deposu: tools/yayin; Küpler ile Küreler betiğinden genelleştirildi) (Android Publisher API v3).

MyTinyURL'nin tool/play_publish.py'si (iurl sunucusunda /usr/local/bin/play-publish) örnek alındı: aynı
hizmet hesabı, aynı kanal düzeni (her derleme önce DAHİLİ kanala, sonra üretime terfi). Farkları:
paket adı ve anahtar yolu parametre, mağaza girişini (metin + görseller) de yükleyebiliyor.

  play_yayinla.py durum
  play_yayinla.py yukle <kup_kure.aab> <surum-notlari.md> [--semboller kup_kure_semboller.zip]
  play_yayinla.py kapali [--kanal alpha]           # en yeni dahili derleme -> kapalı test
  play_yayinla.py terfi [--oran 1]                 # en yeni dahili derleme -> üretim (incelemeye gider)
  play_yayinla.py tamamla [--gun 2]                # aşamalı dağıtım -> %100 (bu aracın başlattığıysa)
  play_yayinla.py liste <magaza_dizini> <ekran_dizini>   # tr-TR + en-US metin, ikon, tanıtım, ekranlar
  play_yayinla.py detay --site <url> --eposta <adres>    # mağazadaki web sitesi + iletişim e-postası

Sürüm notları: <tr-TR> ... </tr-TR> ve <en-US> ... </en-US> blokları, her biri en çok 500 karakter.
Kimlik: hizmet hesabı JSON'u, TULPAR_PLAY_ANAHTAR (varsayılan /root/.config/play-publisher/key.json).
DEPOYA ASLA GİRMEZ. Hizmet hesabının Play Console'da bu uygulamaya yetkisi olmalı.

ÖN KOŞULLAR (API yapamaz, kullanıcı Play Console'da bir kez yapar): uygulamayı oluşturmak (paket adı
ilk yüklemede kalıcılaşır), uygulama içeriği formları (gizlilik politikası adresi, reklam yok, içerik
derecelendirmesi, hedef kitle, veri güvenliği), yükleme anahtarı (./anahtar_olustur.sh).
"""
import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

PAKET = os.environ.get("TULPAR_PLAY_PAKET", "")
if not PAKET and "-h" not in sys.argv and "--help" not in sys.argv:
    sys.exit("TULPAR_PLAY_PAKET yok: Android paket adi (ornek dev.tulparlang.oyun). play_sunucu.sh bunu yayin.env'den verir.")
ANAHTAR = os.environ.get("TULPAR_PLAY_ANAHTAR", "/root/.config/play-publisher/key.json")
DURUM = Path(os.environ.get("TULPAR_PLAY_DURUM", str(Path(ANAHTAR).parent / f"durum-{PAKET}.json")))
API = f"https://androidpublisher.googleapis.com/androidpublisher/v3/applications/{PAKET}"
UPLOAD = f"https://androidpublisher.googleapis.com/upload/androidpublisher/v3/applications/{PAKET}"
DILLER = {"tr-TR": "liste_tr.md", "en-US": "liste_en.md"}
SINIR = {"title": 30, "shortDescription": 80, "fullDescription": 4000}


def log(m):
    print(f"{time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())} {m}", flush=True)


def oturum():
    from google.auth.transport.requests import AuthorizedSession
    from google.oauth2 import service_account
    c = service_account.Credentials.from_service_account_file(
        ANAHTAR, scopes=["https://www.googleapis.com/auth/androidpublisher"])
    return AuthorizedSession(c)


def bak(r):
    if r.status_code >= 400:
        sys.exit(f"API hatası {r.status_code}: {r.text[:800]}")
    return r.json() if r.content else {}


class Duzenleme:
    """Tek Play düzenlemesi: aç, değiştir, işle (hata olursa bırak)."""

    def __init__(self, s):
        self.s = s
        self.id = bak(s.post(f"{API}/edits"))["id"]

    def kanal(self, k):
        return bak(self.s.get(f"{API}/edits/{self.id}/tracks/{k}"))

    def kanal_yaz(self, k, surumler):
        return bak(self.s.put(f"{API}/edits/{self.id}/tracks/{k}", json={"track": k, "releases": surumler}))

    def paket_yukle(self, yol):
        with open(yol, "rb") as f:
            r = self.s.post(f"{UPLOAD}/edits/{self.id}/bundles", params={"uploadType": "media"},
                            headers={"Content-Type": "application/octet-stream"}, data=f, timeout=900)
        return bak(r)["versionCode"]

    def semboller(self, kod, yol):
        with open(yol, "rb") as f:
            r = self.s.post(f"{UPLOAD}/edits/{self.id}/apks/{kod}/deobfuscationFiles/nativeCode",
                            params={"uploadType": "media"},
                            headers={"Content-Type": "application/octet-stream"}, data=f, timeout=900)
        return bak(r)

    def liste_yaz(self, dil, govde):
        return bak(self.s.put(f"{API}/edits/{self.id}/listings/{dil}", json=govde))

    def resimleri_sil(self, dil, tur):
        return bak(self.s.delete(f"{API}/edits/{self.id}/listings/{dil}/{tur}"))

    def resim_yukle(self, dil, tur, yol):
        with open(yol, "rb") as f:
            r = self.s.post(f"{UPLOAD}/edits/{self.id}/listings/{dil}/{tur}", params={"uploadType": "media"},
                            headers={"Content-Type": "image/png"}, data=f, timeout=300)
        return bak(r)

    def detay_yaz(self, govde):
        return bak(self.s.patch(f"{API}/edits/{self.id}/details", json=govde))

    def isle(self):
        return bak(self.s.post(f"{API}/edits/{self.id}:commit"))

    def birak(self):
        self.s.delete(f"{API}/edits/{self.id}")


def en_buyuk(surum):
    return max((int(c) for c in surum.get("versionCodes", [])), default=0)


def notlar(yol):
    t = Path(yol).read_text(encoding="utf-8")
    n = [{"language": d, "text": g.strip()} for d, g in re.findall(r"<([A-Za-z0-9-]+)>\n(.*?)\n</\1>", t, re.S)]
    if not n:
        sys.exit(f"{yol}: <dil> bloğu yok")
    for x in n:
        if len(x["text"]) > 500:
            sys.exit(f"{x['language']} sürüm notu 500 karakteri aşıyor ({len(x['text'])})")
    return n


def liste_oku(md):
    """liste_tr.md / liste_en.md: '## <başlık> (≤N)' bölümleri -> title / short / full (Play düz metin)."""
    t = Path(md).read_text(encoding="utf-8")
    bol = re.split(r"^## ", t, flags=re.M)[1:]
    alan = {}
    for b in bol:
        bas, _, govde = b.partition("\n")
        g = govde.strip()
        if "(≤30)" in bas:
            alan["title"] = g
        elif "(≤80)" in bas:
            alan["shortDescription"] = g
        elif "(≤4000)" in bas:
            alan["fullDescription"] = g.replace("**", "")  # Play markdown göstermez
    for k, s in SINIR.items():
        if k not in alan:
            sys.exit(f"{md}: {k} bölümü yok")
        if len(alan[k]) > s:
            sys.exit(f"{md}: {k} {len(alan[k])} > {s} karakter")
    return alan


def durum_oku():
    try:
        return json.loads(DURUM.read_text())
    except (OSError, ValueError):
        return {}


def k_durum(a):
    e = Duzenleme(oturum())
    try:
        for k in ("internal", "alpha", "production"):
            for r in e.kanal(k).get("releases", []):
                o = r.get("userFraction")
                print(f"{k:10} {r.get('name', ''):24} kodlar={r.get('versionCodes')} durum={r.get('status')}"
                      + (f" oran={o}" if o else ""))
    finally:
        e.birak()
    print("durum:", json.dumps(durum_oku()))


def k_yukle(a):
    n = notlar(a.notlar)
    e = Duzenleme(oturum())
    try:
        log(f"yükleniyor {a.aab}")
        kod = e.paket_yukle(a.aab)
        log(f"yüklendi versionCode {kod}")
        if a.semboller:
            e.semboller(kod, a.semboller)
            log("yerel hata ayıklama sembolleri yüklendi")
        e.kanal_yaz("internal", [{"versionCodes": [str(kod)], "status": "completed", "releaseNotes": n}])
        e.isle()
    except BaseException:
        e.birak()
        raise
    log(f"dahili kanal artık {kod}")


def k_terfi(a):
    e = Duzenleme(oturum())
    try:
        ic = e.kanal("internal").get("releases", [])
        ur = e.kanal("production").get("releases", [])
        if not ic:
            log("dahili sürüm yok")
            return e.birak()
        aday = max(ic, key=en_buyuk)
        kod = en_buyuk(aday)
        canli = max((en_buyuk(r) for r in ur), default=0)
        if kod <= canli:
            log(f"dahili {kod} üretimdeki {canli}'den yeni değil")
            return e.birak()
        r = {"versionCodes": [str(kod)], "releaseNotes": aday.get("releaseNotes", [])}
        if a.oran >= 1:
            r["status"] = "completed"
        else:
            r.update(status="inProgress", userFraction=a.oran)
        e.kanal_yaz("production", [r])
        e.isle()
    except BaseException:
        e.birak()
        raise
    DURUM.write_text(json.dumps({"terfi_kod": kod, "terfi_zaman": int(time.time()), "oran": a.oran}, indent=2))
    log(f"{kod} üretime terfi etti ({a.oran:.0%}), incelemeye gönderildi")


def k_kapali(a):
    """En yeni dahili derleme -> kapalı test kanalı (alpha). Test kullanıcı listesi Console'da."""
    e = Duzenleme(oturum())
    try:
        ic = e.kanal("internal").get("releases", [])
        if not ic:
            log("dahili sürüm yok")
            return e.birak()
        aday = max(ic, key=en_buyuk)
        kod = en_buyuk(aday)
        e.kanal_yaz(a.kanal, [{"versionCodes": [str(kod)], "status": "completed",
                               "releaseNotes": aday.get("releaseNotes", [])}])
        e.isle()
    except BaseException:
        e.birak()
        raise
    log(f"{kod} kapalı test kanalına ({a.kanal}) gönderildi; incelemeden sonra test kullanıcılarına açılır")


def k_tamamla(a):
    d = durum_oku()
    e = Duzenleme(oturum())
    try:
        asamali = [r for r in e.kanal("production").get("releases", []) if r.get("status") == "inProgress"]
        if not asamali:
            log("aşamalı dağıtım yok")
            return e.birak()
        r = asamali[0]
        kod = en_buyuk(r)
        if d.get("terfi_kod") != kod:
            log(f"{kod} dağıtımını bu araç başlatmadı; dokunulmuyor")
            return e.birak()
        if time.time() - d.get("terfi_zaman", 0) < a.gun * 86400:
            log(f"{kod} %{r.get('userFraction', 0) * 100:.0f}'de, {a.gun} gün bekleniyor")
            return e.birak()
        e.kanal_yaz("production", [{"versionCodes": r["versionCodes"], "status": "completed",
                                    "releaseNotes": r.get("releaseNotes", [])}])
        e.isle()
    except BaseException:
        e.birak()
        raise
    log(f"{kod} %100")


def k_liste(a):
    m, ek = Path(a.magaza), Path(a.ekran)
    plan = {}
    for dil, dosya in DILLER.items():
        alan = liste_oku(m / dosya)
        k = "tr" if dil == "tr-TR" else "en"
        ekranlar = sorted((ek / f"ekran_{k}").glob("*.png"))
        if not 2 <= len(ekranlar) <= 8:
            sys.exit(f"{dil}: {len(ekranlar)} ekran görüntüsü (Play 2..8 ister)")
        plan[dil] = (alan, m / f"tanitim_1024x500_{k}.png", ekranlar)
    ikon = m / "ikon_play_512.png"
    for dil, (alan, tan, ekr) in plan.items():
        print(f"{dil}: ad {len(alan['title'])}, kısa {len(alan['shortDescription'])}, tam {len(alan['fullDescription'])} kr; "
              f"tanıtım {tan.name}, {len(ekr)} ekran")
    if a.kuru:
        return
    e = Duzenleme(oturum())
    try:
        for dil, (alan, tan, ekr) in plan.items():
            e.liste_yaz(dil, dict(alan, language=dil))
            for tur, dosyalar in (("icon", [ikon]), ("featureGraphic", [tan]), ("phoneScreenshots", ekr)):
                e.resimleri_sil(dil, tur)
                for f in dosyalar:
                    e.resim_yukle(dil, tur, f)
            log(f"{dil}: metin + ikon + tanıtım + {len(ekr)} ekran")
        e.isle()
    except BaseException:
        e.birak()
        raise
    log("mağaza girişi güncellendi")


def k_detay(a):
    g = {"defaultLanguage": a.dil}
    if a.site: g["contactWebsite"] = a.site
    if a.eposta: g["contactEmail"] = a.eposta
    e = Duzenleme(oturum())
    try:
        e.detay_yaz(g)
        e.isle()
    except BaseException:
        e.birak()
        raise
    log(f"uygulama ayrıntıları: {g}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    alt = p.add_subparsers(dest="komut", required=True)
    alt.add_parser("durum")
    u = alt.add_parser("yukle")
    u.add_argument("aab")
    u.add_argument("notlar")
    u.add_argument("--semboller")
    t = alt.add_parser("terfi")
    t.add_argument("--oran", type=float, default=1.0)
    kp = alt.add_parser("kapali")
    kp.add_argument("--kanal", default="alpha")
    c = alt.add_parser("tamamla")
    c.add_argument("--gun", type=float, default=2)
    li = alt.add_parser("liste")
    li.add_argument("magaza")
    li.add_argument("ekran")
    li.add_argument("--kuru", action="store_true", help="yalnız denetle, yükleme")
    d = alt.add_parser("detay")
    d.add_argument("--site")
    d.add_argument("--eposta")
    d.add_argument("--dil", default="tr-TR")
    a = p.parse_args()
    {"durum": k_durum, "yukle": k_yukle, "terfi": k_terfi, "tamamla": k_tamamla, "liste": k_liste, "detay": k_detay, "kapali": k_kapali}[a.komut](a)


if __name__ == "__main__":
    main()
