# Arayüz sürücüsü — editörü komutla sürmek

Penceresiz editörü bir tarayıcı sürücüsü gibi kullanmak için: "Dosya'ya tıkla", "Kaydet
göründü mü", "ekran al". Pencere açılmaz. Girdi, gerçek kullanıcının girdisiyle aynı yoldan
geçer: `g_synth` → `EditorUi::push_input` → ImGui olayları. Hedefler etiketle bulunur.

- Kaynak: `app/editor_surucu.{hpp,cpp}`.
- İstemci: `tools/arayuz.py`.
- Kapı: `tools/arayuz_kapi.sh`, yerel CI'da "Arayuz surucusu".

## Nasıl çalışır

- **Ögeler.** ImGui'nin kendi test motoru kancaları açık (`IMGUI_ENABLE_TEST_ENGINE`,
  `third_party/imgui/imconfig.h` TULPAR YAMASI). `ItemAdd` ve `ItemInfo` her düğme, menü,
  sekme ve pencere için kimliği, dikdörtgeni, etiketi ve durumu verir (işaretli, açık, soluk).
  Tanımları `third_party/imgui/imgui_tulpar.cpp` dosyasında; dış kütüphane yok.
- **Metinler.** `ImFont::RenderText` yaması çizilen her metnin yerini ve kırpmasını verir.
  Editörün doğrudan `dl->AddText` ile çizdiği metinler de buna dahil (hiyerarşi satırları,
  araç çubuğu, durum çubuğu). Etiketi gizli (`##`) ögeler, üstlerindeki metinle tıklanır.
- **Zamanlama.** Komut kare başında, **önceki karenin** ögeleri ve pikselleriyle işlenir;
  ekranda ne görünüyorsa ona tıklanır. Tıklama birden çok kare sürer (gel → bas → bırak →
  iki kare yerleşme). ImGui, hover ve aktif durumlarından gerçek kullanıcıdaki gibi geçer.
- **Otomatik bekleme.** Hedefli komutlar öge görünene kadar bekler. Varsayılan sınır 120
  karedir, `kare=N` ile değişir. Süre dolarsa `HATA` döner.
- **Ürün gibi açılış.** Sürücü koşumunda yerleşik kapılar ve betikli seçim/oynatma yoktur;
  kullanıcının gördüğü editör açılır. Ayar dizini geçicidir, kullanıcının ayarlarına dokunulmaz.

## Canlı kullanım

```bash
tools/arayuz.py baslat [--size 1600x900] [-- --scene x.sahne | --karsilama ...]
tools/arayuz.py tikla "Dosya"
tools/arayuz.py ogeler pencere=Menu      # görünen ögeler: etiket, pencere, merkez, ölçü, durum
tools/arayuz.py ekran menu               # -> $TULPAR_ARAYUZ_DIZIN/menu.png
tools/arayuz.py bitir
```

Oturum dizini `$TULPAR_ARAYUZ_DIZIN`, yoksa `$TMPDIR/tulpar_arayuz` olur. Editör
`--surucu-canli <fifo>` ile koşar; her komut `yanit.txt` dosyasına `.son <n> tamam|HATA`
satırıyla biter. İstemci `HATA` aldığında 1 ile çıkar. Editörden açılan oyun da penceresizdir:
istemci `TULPAR_ENGINE_HEADLESS` değişkenini verir.

## Betik

```bash
engine_editor --surucu senaryo.txt --surucu-dizin cikti/ [--size WxH] [--scene ...]
```

Betik ilk `HATA`'da durur. Çıkış kodu 0 ise bütün doğrulamalar geçmiştir, 1 ise biri
düşmüştür. Örnek senaryo: `tests/arayuz/temel.txt`.

## Komutlar

Hedef `"etiket"` ya da `x y` (piksel) olarak verilir. Etiket eşleşmesi:

- Görünen etiket tam aynıysa tam eşleşme sayılır; ikonlu etikette ikondan sonraki metin
  tam aynıysa da öyle.
- Yoksa kısmi eşleşme aranır (ASCII büyük/küçük harf duyarsız).
- Hiç öge yoksa çizilen metinler aranır.
- Birden çok aday varsa `HATA` ile adaylar listelenir. Seçim için `pencere=AD` (pencere
  adında geçen) ya da `sira=N` verilir.

| komut | ne yapar |
|---|---|
| `tikla` / `sag_tikla` / `cift_tikla` hedef | gel, bas, bırak (soluk ögeye tıklama HATA) |
| `uzerine` hedef | fareyi üstüne getir (ipucu) |
| `surukle` hedef dx dy / `surukle` x y dx dy | bas, 10 karede sürükle, bırak |
| `kaydir` hedef miktar | fare tekeri |
| `tus` Ctrl+S / Escape / F5 / Delete / Alt+1 … | kombinasyon, bir kare basılı |
| `yaz` "metin" | UTF-8 karakter girişi (kare başına 16) |
| `fare` x y / `bekle` N | fare taşı / N kare bekle |
| `dogrula_oge` / `bekle_oge` hedef | görünene kadar bekle, yoksa HATA |
| `dogrula_yok` hedef | kaybolana kadar bekle |
| `dogrula_metin` / `metin_yok` "metin" | çizilen metinde var / yok |
| `ekran` [ad] | PNG ekran görüntüsü (önceki kare) |
| `ogeler` / `metinler` [süzgeç] [pencere=AD] | görünenlerin listesi (DOM görüntüsü gibi) |
| `cik` | editörü kapat |

## Kapı ve pozitif kontrol

- `tests/test_editor_surucu.cpp`:
  - Tuş çözümü, jetonlar ve etiket eşleşmesi.
  - Test motoru kancası, düğmenin dikdörtgenini ImGui'nin kendi `GetItemRect` değeriyle
    **aynı** verir.
  - Kontrol: `TestEngineHookItems` kapalıyken hiçbir şey gelmez.
- `tools/arayuz_kapi.sh` aşağıdaki senaryoları koşar (yerel CI'da ~1 sn):
  - **`temel.txt`:**
    - menü açılır ve kapanır;
    - satıra tıklayınca Dönüşüm paneli gelir; tıklamadan önce yoktur;
    - Delete varlık sayısını 7'ye indirir, Ctrl+Z 8'e geri getirir;
    - Konsol araması eşleşmeyen satırı gizler.
  - **`kaydet.txt`:** geçici kopya sahnede iki Ctrl+S; yol korunmalı, ikinci kayıtta diyalog
    açılmamalı, dosyada 7 varlık olmalı (TUZAKLAR 8dt).
  - **`ornek.txt`:** karşılamadaki "Örnek: Bilardo" geçici oyun klasörüne üretilip açılmalı.
  - **`betik_yeni.txt` / `betik_eski.txt` (U10 B1):** duvara atanmış betik, editörde Derle'den sonra oyunda
    gerçekten çalışmalı; eski projede editör "Bağla" diye sorar. Pozitif kontrol: bağlanmamış projede aynı
    betik çalışmaz ("betik kancası YOK"). `tulpar` yoksa ATLANDI.
  - **`kod.txt` (U10 B2):** Kaynaklar'da betiğe çift tık Kod panelinde açar, panelden nesneye ekler; yazarken
    Ctrl+Z sahneyi geri almaz, Ctrl+S dosyaya yazar.
  - **`yeni_betik.txt` (U10 B4):** "Davranış ekle (betik)" → Boş → "Deneme Davranış" → oluşan
    `deneme_davranis.tpr` oyunda çalışır (`_baslat` logu).
  - **`ekle_menu.txt` (U11 B2):** Basit kipte nesne seçiliyken Ekle > Şekiller > Küp (8 → 9 varlık); menüde teknik
    kategori yok. Kontrol: Gelişmiş kipte "Çevre Hacimleri" ve "Davranışlı Nesne (betik)" görünür.
  - **`tercihler.txt` + `tercihler_kalici.txt` (U11 B1):** Düzen > Tercihler… → "Büyük" + otomatik kaydet; küp
    eklenir ve `TULPAR_EDITOR_OTO_KAYIT_SN=2` ile elle kaydetmeden diske yazılır. İkinci açılışta menü öğesi
    boyu büyümüş olmalı (kalıcı); Görünüm > Düzeni sıfırla. Kontrol: varsayılan ayarla ilk koşum.
  - **`karsilama.txt` (U11 A3):** karşılamada yalnız Türkçe ad + "Bilardo" kartı → Oluştur; proje bilardo
    şablonundan, İngilizce adı Türkçe ad. Kontrol: ad yazılmadan "Başlamak için oyununa bir ad ver." görünür.
  - **`sade_dil.txt` (U11 A1):** Basit kipte oynat → durdur; Konsol'da `[engine_bridge]`, `: GPU`, mutlak yol ve
    "hatayla" yok, görüntüde fps / kamera hapı yok. Pozitif kontrol: Görünüm > Gelişmiş kip'e geçince hepsi görünür.
  - **`ana_betik.txt` (U10 B7):** Oynat > "Ana betiği aç (oyun.tpr)" ana dosyayı Kod panelinde açar, rolünü
    yazar ve "nesneye ekle" önermez; aynı seçimle sıradan bir betik açılınca öneri görünür (kontrol).
  - **`betik_karti.txt` (U10 B5):** bağlı olmayan projede kart "oyun başlarken ✓, her karede —, Oyuna bağlı
    değil" gösterir; "Bağla" → "Oyuna bağlı"; Kod panelinde `_guncelle` eklenip Ctrl+S → aynı kartta "her karede"
    işaretlenir. `tulpar` gerekmez.
  - **`hata_satiri.txt` (U10 B3):** bozuk betik → Oynat → "Kodu aç (satır 3)" → Kod paneli 3. satırda.
  - **`pozitif_kontrol.txt`:** olmayan öge çıkış 1 ve `HATA` satırı vermeli.

## Bilinen sınırlar

- Canlı kip (POSIX) komut kanalını **bloklamadan** okur: komut yokken editör kare çizmeye devam eder
  (boşta ~60 Hz). 2026-11-11'e kadar bloklayarak okuyordu; düşünürken geçen her saniye editörü
  durduruyor, gömülü oyun 5 s sonra "editör sessiz" diye kapanıyordu. Windows'ta canlı kip yok (FIFO).

- Metnin yeri, metnin kendisidir: üst üste çizilen iki metin (menü altında kalan araç
  çubuğu) ayırt edilmez. Öge eşleşmesi önce gelir ve görünürlük kırpmaya bakar, z-sırasına
  bakmaz.
- 3B görünümdeki nesneler öge değildir; onlara `tikla x y` ile, ekran görüntüsüne bakarak
  tıklanır.
- Windows'ta FIFO yok: canlı kip yalnız Linux/macOS'ta çalışır, betik kipi her yerde.
