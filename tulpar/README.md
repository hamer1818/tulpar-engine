# `tulpar/` — motorun TulparLang tarafı

Motor C++ kalır, **oyun betikleri Tulpar'da yazılır**. Bu dizin o bağlantının
TulparLang tarafındaki bütün parçalarını tutar. TulparLang derleyicisi motoru
**adıyla tanımaz** (dil deposunda yalnız dilin kendi özellikleri var); motor ona
genel bir **yerel eklenti** olarak bağlanır (TulparLang K303, 2026-10-02).

```
tulpar/
  engine.tpr              -> eklenti paketinin modülü: `import "engine"` (TR/EN sarmalayıcı)
  tulpar.toml             -> [ext] paths = ["../yapi/tulpar-ext"]: bu dizinden `tulpar x.tpr` doğrudan çalışır
  generated/              -> tools/gen_engine_bindings.py üretir, ELLE DÜZENLENMEZ
    tulpar-ext.json            eklenti bildirimi: 238 fonksiyon (ad, C sembolü, tipler, belge),
                               modül, platforma göre link kitaplıkları
  examples/               engine_ilk_oyun.tpr, engine_arena.tpr, engine_aksiyon.tpr,
                          engine_betik_dagitimi.tpr (betik kancalari + tetik bolgeleri),
                          engine_karakter.tpr (karakter denetleyicisi: basamak, ziplama, tetik),
                          engine_dalga.tpr (kodla uretilen dusmanlara `betik_ata`; davranislar
                          davranis/dusman.tpr + davranis/tuzak.tpr — docs/KOPRU.md 7.10),
                          engine_kanca_olcumu.tpr (olcu, oyun degil: kanca basina ns ve
                          kare basina bellek; tools/kanca_olcumu.py kosturur),
                          engine_cagri_olcumu.tpr (olcu: Tulpar -> teng_* cagri maliyeti)
                          (aksiyon: TulparLang P0 dil özellikleri — `enum Ekran/Durum/Hal`,
                          `yon_hesapla(): (float, float)`, düşman kaydı tek `Dusman[]`
                          — 11 paralel dizi 2026-09-21'de kalktı)
                          nesne özellikleri: assets/ozellik.sahne + davranis/muhafiz.tpr
                          (`ozellik_tam/_sayi/_bayrak/_nokta` — docs/KOPRU.md 7.11)
                          bolum isaretleri (E7): aksiyonun oyuncu baslangici, kapisi ve
                          dusmanlari assets/salon1/2.sahne'de; tipler davranis/oyuncu_baslangic.tpr,
                          kapi.tpr, dusman_yeri.tpr (docs/KOPRU.md 7.11)
  oyunlar/kup_kure_savasi/  ilk gerçek oyun "Küpler ile Kürelerin Savaşı" (Çağlar Boyu Savaş tarzı;
                          KENDİ dizininden çalışır, kendi tulpar.toml'u + [android]; OKUBENI.md;
                          bulunan motor/dil sorunları docs/OYUN_GERI_BILDIRIM.md)
  tests/engine_bridge.test.tpr   uçtan uca köprü testi (Tulpar tarafı)
  tests/engine_gpusuz.test.tpr   GPU'suz köprü: kurulumsuz teng_* çağrıları + düşen kurulumun sözleşmesi
```

## Çalıştırmak

Motoru derleyin (`./derle.sh` ya da `cmake --build yapi`): derleme **eklenti paketini**
`yapi/tulpar-ext/` altına koyar — `tulpar-ext.json` + `engine.tpr` + `lib/libengine_*.a`.
Kurulu `tulpar` (yerel eklenti destekli bir sürüm; `tulpar update`) onu kullanır,
derleyici yeniden derlenmez:

```bash
cd tulpar
TULPAR_ENGINE_HEADLESS=60 DISPLAY= tulpar examples/engine_dalga.tpr   # tulpar.toml paketi verir
tulpar --ext ../yapi/tulpar-ext examples/engine_dalga.tpr              # ya da açıkça
TULPAR_EXT_PATH=/yol/tulpar-ext tulpar oyun.tpr                        # ya da ortamdan
tools/tulpar_dogrula.sh --tam        # (depo kökünden) zincirin tamamı + taban çizgileri
tools/tulpar_dogrula.sh --tam --gpusuz-izinli   # GPU'suz makine: GPU kapıları ATLANDI sayılır
```

Platformlar: bildirimde `link.linux`, `link.macos`, `link.windows` (MSYS2 MINGW64; `tulpar.exe`
AOT linkini MINGW64 `clang++` ile yapar — `mingw-w64-x86_64-clang` + `-openssl` kurulu olmalı) ve
`link.android`. Windows'ta motorun kare döngüsü CI'da SwiftShader üzerinde ölçülüyor (2026-10-05'ten
beri, CI ve sürüm; `--uzun-atla`: dalga/aksiyon uzun taban çizgileri Linux/macOS'ta, docs/KOPRU.md §2.1).

Bulunma sırası `--ext` → `TULPAR_EXT_PATH` → `tulpar.toml [ext]`; aynı adlı eklentide önce
gelen kazanır. Editörün F5/Ctrl+F5'i `PATH`'teki `tulpar`ı (önce `--ext <paket> version`
ile sınayarak) ve `<editör dizini>/tulpar-ext` paketini kullanır; `TULPAR_MOTOR_DERLEYICI`
/ `TULPAR_MOTOR_EKLENTI` ikisini ezer.

## Tek kaynak kuralı

208 `eng_*` fonksiyonunun tek kaynağı `tools/gen_engine_bindings.py` içindeki `SPEC`
tablosudur. Tek komut iki dosya üretir:

```bash
python3 tools/gen_engine_bindings.py   # -> tulpar/generated/tulpar-ext.json + bridge/tulpar_ext_abi.inc
```

Bir fonksiyon eklemek: `bridge/engine_api.h` + `bridge/engine_api.cpp` yaz, `SPEC`'e
satır ekle, betiği yeniden koştur. Derleme iki kapıyla korunur:

- **Tazelik** (`engine_bindings_check`, CMake ön koşulu): depodaki üretilmiş dosyalar
  `SPEC`'in şimdiki çıktısıyla bayt bayt aynı değilse derleme durur (kapının kendi
  pozitif kontrolü: `--oz-sinama`).
- **ABI kilidi** (`bridge/tulpar_abi.cpp`): derleyici statik arşivde C tipi göremez;
  bildirim `i32` derken C `double` alıyorsa hata vermez, çöp okur. Kilit her imzayı
  `teng_*`'in gerçek bildirimine tipli bir işaretçi olarak atar — kayma **derleme
  hatası**; tablo `engine_tests`'e bağlanır, yani bildirimdeki her sembol arşivde
  tanımlı olmak zorunda (link hatası). Pozitif kontrol yapılandırmada: kasıtlı bir
  kayma (`TULPAR_ABI_KILIDI_BOZ`) derlenmeye çalışılır ve düşmesi beklenir.

## Eski yol (2026-10-02'ye kadar)

`tools/motor_derleyici.sh` derleyici deposunun `86e2c4e` commit'inin **tersini** 13
dosyaya uygulayıp motoru tanıyan ayrı bir derleyici kuruyordu; üretilmiş dört dosya
(`engine_bindings.cpp` VMValue bindingleri + üç derleyici tablosu) TulparLang
kopyasına `--tulpar <kök>` ile kurulurdu ve Android köprü arşivi TulparLang kaynak
ağacını (`TULPAR_ROOT`, `vm.hpp`) isterdi. Ters yama iki günde iki kez kırıldı ve
VMValue bindingleri derleyicinin iç değer yerleşimine bağlıydı (2026-10-02'de
`ObjString` karakterleri nesnenin içine alınınca eski arşiv onları işaretçi sanardı).
Hepsi kaldırıldı; ayrıntı ve gerekçe [docs/KOPRU.md §2.1](../docs/KOPRU.md).
