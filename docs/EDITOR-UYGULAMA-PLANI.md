# Editör → "normal uygulama" planı

> **Durum: UYGULANIYOR (başladı 2026-11-05).** Kararlar (kullanıcı): K1 Windows + Linux, K5 Basit
> varsayılan, K6 ilk Oynat'ta tek tıkla kur; K2/K3/K4/K7/K8 önerildiği gibi (Inno Setup kullanıcı
> başına, AppImage, nativefiledialog-extended, Tulpar logosu, APK menüsü U6'da).
> Aşamalar: **U0 ✅**, **U1 ✅** (aşağıda §8), **U2 ✅** (§U2 altındaki not), **U3 ✅** (a: fabrika çekirdeği C++'ta, b: arena şablonu sahneden, c: başlangıç ekranı), **U4 ✅** (sistem dosya seçicisi; K4 değişti, §U4 notu), **U5 ✅** (Basit/Gelişmiş + ilk tur; §U5 notu ve §8 düzeltmesi), **U6 ✅** (Tulpar'ı kur/güncelle penceresi, Oyun > APK / Doğrula; §U6 notu), **U7a ✅** (paket kurulu düzende oyun üretir; §U7a notu), **U7b Linux ✅** (ikon, AppImage, masaüstü kaydı; §U7b notu), **U7b Windows yazıldı, CI ölçecek** (Inno Setup + başlatıcı), **U8 ✅** (Yardım: rehber, kısayollar, belgeler, çökme bildirimi; §U8 notu), **U7c ✅** (AppImage tek dosya güncelleme), **U9 ✅** (Kamera + Ses oyunda, blob v10; §U9 notu)
> Bu belge iş başlayınca adım adım işaretlenir. Önceki editör belgeleri: `EDITOR-DURUM.md` (bileşen
> matrisi, 2026-09-18), `EDITOR-PLAN.md` (E1–E5 kabuk/sahne fazları), `EDITOR-TASARIM.md`. Fabrika:
> `FABRIKA.md`.

## 0. Hedef ve bitiş ölçütü

Bugün editör güçlü bir **geliştirici aracı**; hedef, oyun yapan birinin **normal bir masaüstü
uygulaması gibi** kullanabilmesi. Bitiş ölçütü tek bir senaryo (kabul testi, §9):

> Hiç Tulpar görmemiş biri, **10 dakika içinde**: kurucuyu indirip kurar → uygulama menüsünden
> "Tulpar Editör"ü açar → açılış ekranında **Yeni oyun** der → sahnede bir nesneyi taşır →
> **Oynat** ile oyunu görür → kaydedip kapatır → yeniden açınca **Son projeler**'den geri döner.
> Terminal açmaz, dosya yolu yazmaz, hiçbir yerde "SetPass / arena / tick" görmez.

Puan hedefi (2026-11-05 değerlendirmesi 5,5/10): **8/10**.

## 1. Bugünkü durum (ölçüldü 2026-11-05, RTX 5080, `engine_editor --headless`)

| # | bulgu | nerede |
|---|---|---|
| D1 | Kurucu yok: GitHub sürümünde `tar.gz` / `zip`; aç, `engine_editor` çalıştır. Uygulama menüsü kısayolu, ikon, `.sahne` ilişkilendirmesi yok. Exe'ye gömülü ikon (`.rc`/`windres`) yok | `tools/package.sh`, `release.yml` |
| D2 | Linux'ta sistem `glfw`'su gerekir (dlopen) | `package.sh:843` |
| D3 | Argümansız açılış doğrudan `tests/assets/editor.sahne`'ye düşüyor; açılış/karşılama ekranı yok | `app/editor.cpp` |
| D4 | "Proje" kavramı yok: editör tek `.sahne` dosyası açar; oyun deposu (`tulpar.toml`) bilinmiyor | `app/editor_files.cpp` |
| D5 | Fabrika (`tools/yeni_oyun.sh`) editörden çağrılamaz; bash + Python'a dayanıyor (Windows kullanıcı makinesinde yok) | `tools/yeni_oyun.sh`, `tools/kit/ikon_uret.py` |
| D6 | Aç/Kaydet: ImGui içinde dosya adı yazılan pencere, işletim sisteminin dosya seçicisi değil | `editor_files.cpp:707` |
| D7 | Özellikler paneli 1280×720'de sayıları kesiyor (`-11.`, `1.0(`), "Odakla" taşıyor | ekran görüntüsü |
| D8 | Stats varsayılan AÇIK (`show_stats = true`, `editor_app.cpp:1906`) ve Görünüm penceresinin DIŞINA, konsolun üstüne taşıyor | ekran görüntüsü |
| D9 | 3B görünüm ekranın ~1/3'ü; varsayılan yerleşim paneller lehine | ekran görüntüsü |
| D10 | Kullanıcıya görünen teknik terimler: SetPass, Motor Arena, Sıcak Tahsis, tick, batch, Çizim Çağrısı | Stats, durum çubuğu |
| D11 | Yerleşim dosyası **proje dizinine** yazılıyor (`<sahne dizini>/editor_layout.txt`) — Küp-Küre deposunda izlenmeyen `assets/editor_layout.txt` bundan | `editor_app.cpp:1863` |
| D12 | F5 / Oynat PATH'te `tulpar` ister; yoksa açıklamalı hata (iyi) ama kurulum yolu sunulmuyor | `editor_game.cpp:67-88` |
| D13 | Yönlendirme yok: ilk açılış ipucu, örnek proje, rehber yok | — |
| D14 | Bileşen boşlukları (Ses, Kamera oyuna aktarılmıyor; Parçacık/Su/Voksel paneli yok) — **Eylül ölçümü, yeniden ölçülmeli** | `EDITOR-DURUM.md §1` |

İyi olanlar korunur: tutarlı koyu tema, Türkçe menüler, ikonlar, sahne araması, geri al/yinele,
çoklu seçim, gizmolar, durum çubuğu, konsol süzgeci, editör içi güncelleme, Oynat + geri sarma.

## 2. Senden istenecek kararlar (başlarken, tek seferde)

| # | karar | seçenekler | önerim |
|---|---|---|---|
| K1 | İlk platform | Windows / Linux / ikisi birden | **İkisi**, ama kurucu önce **Windows** (oyun yapanların çoğu); Linux AppImage aynı turda |
| K2 | Windows kurucu biçimi | Inno Setup / NSIS / MSIX / yalnız zip + kısayol | **Inno Setup**, kullanıcı başına kurulum (yönetici şifresi istemez; `%LOCALAPPDATA%\Programs\Tulpar Editor`) — güncelleyici kendi dizinine yazabilsin diye |
| K3 | Linux biçimi | AppImage / .deb / Flatpak / tar + `.desktop` | **AppImage** (glfw içinde, tek dosya) + istenirse `~/.local/share/applications` kısayolu |
| K4 | Dosya seçici kütüphanesi | nativefiledialog-extended (zlib; Linux'ta xdg-portal/GTK) / portable-file-dialogs (WTFPL; Linux'ta zenity/kdialog süreci) / kendi Win32+portal kodumuz | **nativefiledialog-extended**, portal arka ucu; bulunamazsa bugünkü ImGui penceresi GÖRÜNÜR yedek |
| K5 | Kip | yalnız Basit / Basit + Gelişmiş / bugünkü gibi | **Basit (varsayılan) + Gelişmiş** (bugünkü her şey Gelişmiş'te kalır) |
| K6 | Kurucu `tulpar` derleyicisini de getirsin mi | evet (gömülü) / hayır (ilk Oynat'ta indir) / hayır (yönlendir) | **İlk Oynat'ta tek tıkla kur** (TulparLang kurucusunu çağırır); kurucu küçük kalır, sürüm uyumu `tulpar update` ile |
| K7 | Uygulama adı / ikon | "Tulpar Editör" + mevcut Tulpar logosu / yeni ikon | Tulpar logosu (`/mnt/veri/yazilim/Tulpar/installer/assets/tulpar-logo.png`, kullanıcının varlığı) |
| K8 | Android'i editörden derleme | şimdi / sonra | **U6'da APK menüsü**, Android SDK yoksa açık mesaj. Play yok (kullanıcı kararı) |

## 3. Aşamalar

Sıra bağımlılığa göre; her aşama kendi kapısıyla kapanır, kapının **pozitif kontrolü** olur
(depo kuralı). Süreler kaba tahmin (oturum = birkaç saatlik çalışma), ölçülmedi.

### U0 — UX ölçüm tabanı (önce ölç) · ~1 oturum

Düzeltmeden önce "kesik metin", "taşan pencere", "görünüm oranı" sayıları **ölçülür** ki U1'in
kapısı bir sayıya dayansın.

- `engine_editor --headless N --ux-rapor <dosya>`: her kare ImGui çizim listesinden
  - **kesik metin**: hücresine sığmayan etiket/değer (ImGui `CalcTextSize` > öğe genişliği) — ad ve
    pencereyle sayılır,
  - **taşan pencere**: kayan pencerenin (Stats, açılır menü) dikdörtgeni ait olduğu panelin dışına
    çıkıyor mu,
  - **3B görünüm oranı**: görünüm alanı / pencere alanı,
  - **görünen terimler**: çizilen bütün metin (U5'in yasak terim kapısı için).
- Çözünürlükler: 1280×720, 1366×768 (en yaygın dizüstü), 1920×1080, 2560×1440, DPI 1.5.
- Çıktı: tablo (`docs/EDITOR-UYGULAMA-PLANI.md §8`'e taban satırları).
- **Pozitif kontrol:** sabit genişlikli bir test penceresine bilerek uzun etiket koy → kesik=1.

### U1 — Yerleşim ve okunurluk · ~2 oturum

- **Dönüşüm alanları (D7):** genişlik eşiğin altındaysa X/Y/Z alt alta; değer biçimi genişliğe göre
  (`-11.24` → sığmıyorsa `-11.2`, asla kesik `-11.`); "Çoğalt / Sil / Odakla" sığmazsa yalnız ikon +
  ipucu.
- **Stats (D8):** varsayılan KAPALI; açılınca Görünüm dikdörtgeninin İÇİNE kenetlenir; Basit kipte
  kısa sürüm (§U5).
- **Varsayılan yerleşim (D9):** 3B görünüm ≥ %55 alan (1920×1080'de); alt panel (Kaynaklar/Konsol)
  varsayılan daraltılmış; "Yerleşimi sıfırla" menüsü.
- **Kapı:** U0 ölçümüyle — 1280×720 ve 1366×768'de kesik metin **0**, taşan pencere **0**, görünüm
  oranı ≥ eşik. Pozitif kontrol: eski davranış bayrağı (`TULPAR_EDITOR_UX_ESKI=1`) → sayılar U0
  tabanına döner.

### U2 — Kullanıcı ayarları kullanıcı dizininde · ~1 oturum

- `editor_layout.txt`, son dosyalar/projeler, kip tercihi, dil → kullanıcı ayar dizini:
  Linux `$XDG_CONFIG_HOME/tulpar-editor`, Windows `%APPDATA%\Tulpar\Editor`, macOS
  `~/Library/Application Support/Tulpar Editor`. (D11)
- Eski konumdaki dosya bir kez taşınır (göç), proje dizinine bir daha yazılmaz.
- **Kapı:** editör testinde bir proje açılıp kapatılır → proje dizinine yazılan dosya **0** (dizin
  listesi önce/sonra). Pozitif kontrol: eski yolu zorla → 1 dosya yazıldığı görülür.
- **✅ Yapıldı (2026-11-05):** `platform::user_config_dir` (`TULPAR_EDITOR_AYAR_DIZINI` > Windows
  `%APPDATA%\Tulpar\Editor` > macOS `~/Library/Application Support/Tulpar Editor` > `$XDG_CONFIG_HOME`
  / `~/.config/tulpar-editor`, mkdir -p); panel düzeni, son dosyalar (`son_sahneler`) ve güncelleme
  tercihi (`guncelleme`) orada. Eski dosya bir kez KOPYALANIR, eskiye dokunulmaz
  (`app::editor_ayar_yolu`). Penceresiz (otomasyon) koşum kullanıcının gerçek ayarlarına dokunmaz:
  ayar dizini verilmediyse süreçe özgü geçici dizin, kapanışta silinir. Kapı: birim testi
  `editor_ayar_yolu_kullanici_dizinine_bir_kez_tasir` (taşıma bir kez, üzerine yazmaz; POZİTİF
  KONTROL kurulamayan dizin → eski yola düşer, `goc=2`) + `tools/editor_ux.sh --kapi`'de panel
  düzeni yolu proje dizini dışında. Not: penceresiz kip düzeni zaten yazmıyordu; "proje dizinine 0
  yazım" asıl pencereli kapanışta — o yol elle kontrol listesinde (§9).

### U3 — Açılış ekranı + proje kavramı + fabrika editörde · ~3 oturum

- **Proje = oyun deposu**: içinde `tulpar.toml` olan dizin. Proje açınca: sahneler listesi
  (`assets/*.sahne`), ana betik (`oyun.tpr`), varlıklar. Tek `.sahne` açmak da sürer.
- **Açılış ekranı** (argümansız açılış ve "Dosya > Kapat"):
  - **Yeni oyun**: form — oyun adı TR/EN, kimlik ve önek **adlardan otomatik** (değiştirilebilir,
    canlı doğrulama), şablon kartları (bugün `arena`), konum (dosya seçici), yön (yatay/dikey).
  - **Aç…** (proje dizini ya da `.sahne`), **Son projeler** (ad, yol, son açılış; dizin yoksa soluk +
    "listeden kaldır"), **Örnek oyunu aç** (pakete gömülü arena örneği).
- **Fabrikanın C++ çekirdeği (D5):** şablon kopyalama + yer tutucu doldurma + doğrulama C++'a taşınır
  (`app/editor_fabrika.cpp`); `tools/yeni_oyun.sh` aynı çekirdeği `engine_editor --komut yeni_oyun`
  ile çağırır → **tek kaynak**. İkon üretimi stb_truetype ile C++'ta (Python bağımlılığı kalkar).
  Paket `tulpar/sablonlar/` ve `kit/varlik/`'i taşır (`package.sh` + manifest).
- **Kapı:** `engine_editor --komut yeni_oyun ...` ile `yeni_oyun.sh` çıktısı **dosya dosya aynı**
  (ikon hariç; ikon için piksel karşılaştırma eşiği); üretilen proje kendi `tools/dogrula.sh`'inden
  geçer. Açılış ekranı headless'ta `ui_test` tıklamasıyla "Yeni oyun" akışını uçtan uca koşar.
  Pozitif kontrol: geçersiz kimlik (`Çılgın Oyun` → otomatik `cilgin_oyun`, elle `Ç` → form hatası),
  dolu hedef dizin → reddedilir.
- **Not (Tuzaklar 8dq):** kimlik/önek doğrulaması yerel ayardan bağımsız (Türkçe `İ/ı` → ASCII slug).
- **✅ Yapıldı (2026-11-05):** U3a `app/editor_fabrika.cpp` (+ `engine_editor --yeni-oyun`,
  `--kimlik-oner`; `yeni_oyun.sh` artık onu çağırır, ikon stb ile — Python kalktı); U3b şablonun arenası
  `assets/arena.sahne`'den (oyun `arena.sahneb`'yi yükler → F5 oyunu bulur; `ortam.sh` sahneyi derler);
  U3c `app/editor_karsilama.cpp`: argümansız pencereli açılışta başlangıç ekranı (Yeni oyun: ad TR/EN,
  kimlik/önek/konum otomatik, şablon, yön, gelişmiş alanlar; Son projeler; Aç; Örnek sahne; Boş editör;
  Esc), arkası karartılmış; Oluştur → fabrika + sahneyi açar + derler. Dosya menüsünde "Başlangıç
  ekranı". Kapı (`tools/editor_ux.sh --kapi`, yerel CI): karşılama 1280×720 / 1366×768 kesik 0;
  `--karsilama-yeni <dizin>` Oluştur işleyicisi uçtan uca (proje + sahne + `.sahneb`); POZİTİF KONTROL
  dolu dizine Oluştur reddedilir. Penceresiz karşılama koşumunda yerleşik kapılar koşmaz (ekran girdiyi
  alır); bunu yaparken sonlandırma koşulunu da `kapilar`a çevirmiştim → koşum hiç bitmedi (yakalandı,
  düzeltildi). Açık: "Konum" için klasör seçici U4'te (bugün metin alanı).

### U4 — İşletim sisteminin dosya seçicisi · ~1 oturum

- K4'teki kütüphane `third_party/`'ye vendor edilir; `tools/layer_check.py` `VENDORED` tablosuna
  izinli katmanla (`app` = L6) eklenir; lisansı `lisanslar/`'a.
- Aç / Farklı kaydet / Varlık ekle / Proje konumu seç → yerli seçici; seçici yoksa (Linux'ta portal
  yok) bugünkü ImGui penceresi **görünür yedek** ("sistem dosya seçicisi bulunamadı" satırı).
- **Kapı:** headless'ta yerli seçici çağrılamaz → yedek yolun birim testi + seçici soyutlamasının
  sahte (mock) arka uçla testi; yerli seçici **elle kontrol listesi** (§9) — iddia değil, ölçüm.
- **✅ Yapıldı (2026-11-05) — K4 DEĞİŞTİ:** nativefiledialog-extended yerine `platform/dosya_secici`
  (L0) ALT SÜREÇ arka uçları: Linux kdialog (KDE'de önce) / zenity, Windows PowerShell +
  System.Windows.Forms, macOS osascript. Sebep: NFD Linux'ta GTK/dbus BAĞLANTI bağımlılığı getiriyordu;
  motor sürücüsüz/kütüphanesiz makinede derlenir (Vulkan, GLFW dlopen). Bedel: Windows'ta açılış ~1 s.
  `file_dialog_open` önce sistem seçicisini dener; yoksa ImGui penceresi ve sebebi pencerede soluk
  satır. Başlangıç ekranında Konum'un "Seç..." düğmesi klasör seçici. Penceresiz kip ve testler
  `TULPAR_DOSYA_SECICI=yok` (kipli pencere bloklardı). Testler: argv (Türkçe/boşluk/tırnak yolu tek
  argüman, PowerShell `'`→`''`), "yok" → görünür yedek, sahte `zenity` betiğiyle süreç + çıktı okuma
  uçtan uca (seçildi → Kaydet uzantı ekler; vazgeç → Vazgecildi). **Ölçülmedi:** gerçek kdialog/zenity
  penceresi ve Windows/macOS yolları (Windows derleyicisi yerelde yok; GitHub CI derler) — elle kontrol.

### U5 — Basit / Gelişmiş kip + sade dil + ilk deneyim · ~2 oturum

- **Kip:** Basit varsayılan. Basit'te gizlenenler: Stats ayrıntısı, render kipleri (Aydınlatmalı
  dışı), gölge/küme ayarları, konsolun motor satırları (yalnız uyarı/hata), teknik menüler.
- **Terim sözlüğü** (Basit kip):

  | bugün | Basit |
  |---|---|
  | SetPass / Malzeme Geçişi | (gizli) |
  | Çizim Çağrısı / batch | (gizli) |
  | Motor Arena / Sıcak Tahsis | Bellek |
  | tick | (gizli) |
  | Üçgen/piksel | (gizli) |
  | FPS | Akıcılık (FPS) |

- **Kısa Stats:** FPS + kare süresi grafiği + bellek (MB) + nesne sayısı.
- **İlk açılış turu:** 4 adım (Sahne paneli → Görünüm → Özellikler → Oynat), atlanabilir, bir kez.
- **İpuçları:** her araç çubuğu düğmesinde ad + kısayol + tek cümle (komut tablosu zaten taşıyor).
- **Kapı:** U0'ın metin yakalamasıyla Basit kipte çizilen metinde yasak terim listesi **0**;
  Gelişmiş'te bugünkü metinler aynen (gerileme yok). Pozitif kontrol: Basit kipte Stats'i ayrıntılı
  zorla → yasak terim > 0.
- **✅ Yapıldı (2026-11-05):** `editor_gelismis()` tek bayrak (kullanıcı ayar dizininde `kip`; pencereli
  varsayılan Basit, penceresiz Gelişmiş — yerleşik kapılar bugünkü ekranı ölçer; `--kip` ile seçilir).
  Basit: Stats üç satır (Bellek, Sahnede N nesne, Model ayrıntısı) + "Ayrıntılar: Görünüm > Gelişmiş
  kip"; görünüm kipi açılırı ve Görünüm/Oynat menülerindeki teknik alt menüler gizli; durum çubuğunda
  kare/tick ve kamera konumu yok. Görünüm menüsünde "Gelişmiş kip". İlk açılış turu
  (`app/editor_tur.cpp`): Sahne → Görünüm → Özellikler → Oynat, hedef vurgulu, İleri/Atla/Esc, bir kez
  (`tur` dosyası); Yardım > "İlk turu yeniden göster"; `--tur-adim N` görüntü için. Kapı
  (`editor_ux.sh --kapi`): Basit kipte teknik terim 0, Gelişmiş'te 8 (pozitif kontrol: ölçüm Stats'i
  görüyor). Durum çubuğu ImGui metni değil (doğrudan çizim) — kanca onu görmez, gizleme kodla.
  Konsolun Basit kipte yalnız uyarı/hata göstermesi YAPILMADI (yeni oyun gibi bilgi satırları
  kaybolurdu; ayrı karar).

### U6 — Oynat / derle akışı kullanıcıya göre · ~1–2 oturum

- `tulpar` yoksa (D12): mesaj + **"Tulpar'ı kur"** düğmesi (TulparLang kurucusu / `tulpar update`),
  kurulumdan sonra yeniden sonda. Sürüm eski ise "güncelle".
- **Oyun > Android APK oluştur**: projenin `apk_derle.sh` mantığı (C++'tan; Windows'ta bash yok →
  U3 gibi çekirdek C++'a ya da `tulpar build`'i doğrudan çağır); Android SDK/araçları yoksa ne
  kurulacağını söyleyen tek ekran. **Play'e yükleme YOK** (kullanıcı kararı 2026-11-05).
- **Oyun > Doğrula** (projenin kapıları, sonuç konsolda yeşil/kırmızı).
- **Kapı:** `PATH` boşken Oynat → açıklayıcı ekran (pozitif kontrol: `TULPAR_MOTOR_DERLEYICI`
  geçersiz yol → aynı ekran); APK menüsü emülatörde (varsa) üretilen projede `[kapi] TAMAM`.

**U6 notu (2026-11-06, yapıldı):**
- **Derleyici arama:** `game_find_compiler` PATH'te bulamazsa kurulumcuların standart yerine bakar
  (`~/.local/bin/tulpar`, `%LOCALAPPDATA%\Programs\Tulpar\tulpar.exe`). Menüden açılan editör,
  install betiğinin kabuk profiline yazdığı PATH'i görmeyebilir. Bulamazsa nedeni döner
  (`DerleyiciEksik::KuruluDegil` / `Eski`).
- **"Tulpar gerekli" penceresi:** F5 ya da Ctrl+F5 bu nedeni görünce açılır.
  - Tek tıkla `curl … install.sh | bash` (Windows'ta `iwr … install.ps1 | iex`) ya da `tulpar update` çalışır.
  - Çıktı Konsol'a akar; iş bitince derleyici yeniden aranır.
- **Planın pozitif kontrolü değişti:** "TULPAR_MOTOR_DERLEYICI geçersiz → aynı ekran" artık geçerli değil.
  Açıkça seçilmiş derleyici yanlışsa kurulum ÖNERİLMEZ, çünkü bu kullanıcının kendi seçimi. Pozitif
  kontrol şimdi şu: derleyici varken pencere açılmaz ve oyun 0 ile biter.
- **Oyun > Android APK oluştur / Projeyi doğrula:** projenin kendi `apk_derle.sh` ve `tools/dogrula.sh`
  betiklerini alt süreçte çalıştırır. Editör bildiği `EXT` ve `TULPAR` değerlerini verir; çıktı
  Konsol'a gider.
- **Plandan sapma:** APK mantığı C++'a TAŞINMADI. Betik Android NDK + bash istiyor, Windows'ta menü
  açık bir "henüz yok" mesajı verir. Fabrika projesi olmayan yerde menü reddeder.
- **Güvenlik kuralı:** penceresiz editör, `TULPAR_ENGINE_HEADLESS` verilmeden pencereli oyun açmaz.
  Bir `--kapisiz` denemesi oyunu kullanıcının masaüstünde açmıştı.
- **Kapı** (`tools/editor_ux.sh --kapi`, ikinci dalga):
  - derleyici yok → pencere açılır;
  - derleyici var → pencere açılmaz, oyun 0 ile biter;
  - `TULPAR_ENGINE_HEADLESS` yok → oyun reddedilir;
  - `--arac dogrula` → Oluştur'un ürettiği projenin kapıları geçer;
  - `--arac apk` fabrika dışı yerde → reddedilir.
- **Elle doğrulandı:** `--arac apk` üretilen projede 5,7 MB arm64 APK üretti ve APK doğrulaması TAMAM.
  Bu süre (~1 dk) yüzünden kapıda yok; yerel_ci "Fabrika" aşaması APK'yı zaten derliyor.
- **U7'ye kalan:** şablonun `tools/ortam.sh`'inde motor yolu için `/mnt/veri/...` varsayılanı var.
  Kurulu editörde motor deposu olmayacak, bu yüzden bu varsayılan editörün verdiği yola dönmeli.

### U7 — Kurulum ve dağıtım · ~3 oturum

- **Ortak:** uygulama ikonu (K7) — Windows `.ico` + `windres` ile exe'ye gömülü, Linux PNG (hicolor
  boyutları), pencere ikonu (glfw `glfwSetWindowIcon`).
- **Windows (K2):** Inno Setup betiği `tools/kurulum/windows.iss`; kullanıcı başına kurulum,
  Başlat menüsü + masaüstü kısayolu (isteğe bağlı), `.sahne` ilişkilendirmesi (çift tık → editör),
  kaldırıcı. Editör içi güncelleyici kurulum dizinine yazabilir (kullanıcı başına olduğu için).
  CI Windows ayağı kurucuyu üretir, **sessiz kurar** (`/VERYSILENT`), kurulu yoldan
  `engine_editor --headless 30` koşar, kaldırır.
- **Linux (K3):** AppImage (`appimagetool`; glfw ve gerekli `.so`'lar içinde → D2 kalkar),
  `.desktop` + MIME (`application/x-tulpar-sahne`). Güncelleyici AppImage'ı yerinde değiştirir ya
  da açıkça "yeni AppImage'ı indir" der (hangisi: U7 başında ölçülür). CI: AppImage
  `--appimage-extract-and-run --headless 30` (FUSE'suz runner).
- **macOS:** `.app` paketi ERTELE (iOS/macOS rafta; CI ayağı zaten var, ilk istek gelince).
- **Kapı:** üç platformun paket kapısı (bugünkü `package.sh --denetle` + kurulu yoldan koşum).
  Pozitif kontrol: kurucuya bir dosyayı eksik koy → kurulu koşum düşer.

**U7a notu (2026-11-06, yapıldı) — paket kurulu editörde oyun üretebiliyor:**
- **Paket şablonsuzdu.** Paket `tulpar/sablonlar` ile `tulpar/kit/varlik`'i taşımıyordu. Kurulu
  editörde "Yeni oyun", "Şablon dosyası bulunamadı" hatası verirdi. Derleme makinesinde bu hiç
  görünmedi, çünkü `asset_path` ENGINE_SOURCE_DIR üzerinden kaynak ağacına düşüyordu.
- **Paketleme U3a'dan beri duruyordu.** Varlık türetmesi fabrika dizgilerinde ("%s/assets/%s")
  "çözülemeyen yol" hatası veriyordu. `package.sh` yerel_ci'de yoktu.
- **Düzeltme:**
  - `package.sh` fabrika dosyalarını dizinlerden türeterek pakete koyar.
  - `PROJE_ICI` tablosu: `editor_fabrika.cpp`'deki proje-göreli `assets/` yolları motor varlığı
    sayılmaz.
  - Proje sahnesi bulma `fabrika_proje_sahnesi`'ne taşındı.
- **Kapı:** `package.sh` içindeki `fabrika_sinamasi`.
  - `TULPAR_ENGINE_KAYNAKSIZ=1` verilir; `asset_path` bu durumda kaynak ağacına düşmez. Bunun
    üzerine paketlenmiş editör boş bir dizinde oyun üretir.
  - Üretilen projenin `ortam.sh`'i sahneyi paketin `engine_sahnec`'iyle derler.
  - Pozitif kontrol: `oyun.tpr` şablonu silinmiş paket kopyası üretimi reddeder.
  - `package.sh` artık yerel_ci'de de koşuyor (5d).
- **Şablonun motor bağı.** `ortam.sh`'te `/mnt/veri` varsayılanı kalktı. `.motor` artık motorun
  İKİLİ dizinine bağlanıyor: kaynak ağacında `yapi/`, kurulu editörde kurulum dizini.
  `tulpar.toml` `[ext]` yolu `.motor/tulpar-ext`, iki düzende de aynı. Editörün Oyun menüsü
  `TULPAR_ENGINE_DIR`'i kendi dizini olarak verir.
- **Elle doğrulandı, paketten ve kaynaksız:** `--karsilama-yeni` → `--arac dogrula` 3/3 geçti;
  projede doğrudan `tulpar oyun.tpr` 30 kare koştu.
- **Sırada (U7b):** ikon (K7), Linux AppImage + .desktop/MIME, Windows Inno Setup.
  - AppImage salt okunur: güncelleyici dosya dosya değiştiremez. Karar: AppImage dosyası bütün
    olarak yenisiyle değiştirilir (`$APPIMAGE`).
  - Windows kurucusu yerelde derlenemez (MinGW/ISCC yok), CI'da ölçülür.

**U7b notu (2026-11-06, Linux tarafı yapıldı):**
- **İkon (K7):** TulparLang logosu `assets/ikon/tulpar_{16..512}.png` ve `tulpar.ico` olarak eklendi.
  - Pencere ikonu `glfwSetWindowIcon` ile verilir (X11/Windows).
  - Wayland'de ikon `app_id = dev.tulparlang.TulparEditor` üzerinden `.desktop`'tan gelir.
- **Masaüstü kaydı** (`app/editor_masaustu`):
  - `~/.local/share` altına `.desktop` dosyası yazılır; Exec, AppImage'ın kendi yoludur.
  - Ayrıca hicolor ikonlar ve `application/x-tulpar-sahne` (`*.sahne`) dosya türü yazılır.
  - AppImage ilk açılışta, ya da taşındıysa, kendini kaydeder. Elle kayıt: Yardım menüsü ya da
    `--masaustu-kaydet`.
- **AppImage** (`tools/kurulum/appimage.sh`):
  - Paket dizini + AppRun ile 12 MB'lık tek dosya üretir.
  - appimagetool 1.9.1 ve type2-runtime 20251108 sürüm ve SHA-256 ile sabitlendi; GitHub
    yayın özetiyle karşılaştırıldı.
  - Kapı: AppImage'ın içi SALT OKUNUR açılır ve kaynaksız koşar; masaüstü kaydı denenir.
    Pozitif kontrol: sahnesiz AppImage düşer. Kapı yerel_ci'de 5e aşaması.
- **Salt okunur kurulumun bulduğu hatalar:**
  - Penceresiz düzen kapısı `tests/assets`'e yazıyordu; artık ayar dizinine yazıyor.
  - `oyun.log`, `tulpar_eklenti_sonda.log` ve `engine_hata.log` ikili dizinine yazılıyordu;
    artık ayar dizinine (ya da yedek olarak oraya) yazılıyor.
- **AppImage'ta motor dizini:** AppImage her açılışta farklı bir geçici yerde açılır
  (`/tmp/.mount_*`). Proje `.motor` bağlantısı oraya bağlanırsa editör kapanınca kopardı. Bu yüzden
  `tulpar-ext` ve `engine_sahnec` `~/.local/share/tulpar-editor/motor-<sürüm>`'e kopyalanır;
  Oyun menüsü bu kalıcı yolu verir.
- **Güncelleyici:** önce AppImage'ta devre dışıydı. **U7c (2026-11-06):** artık AppImage DOSYASI
  bütün olarak yenisiyle değiştiriliyor: indir → SHA-256 → yedeğe al → yerine koy, düşerse geri al
  (docs/GUNCELLEME.md "AppImage kipi").
- **Bilinen sınır:** AppImage'ta "Örnek sahne" salt okunur yerde açılır, kaydetmek başarısız olur. **Kalktı (2026-11-10):** örnek artık oynanabilir bilardo; kullanıcının oyun klasörüne üretilip oradan açılır.
  Doğrusu örneği kullanıcı dizinine kopyalamak; U8'de ele alınacak.
- **Windows (yazıldı, ÖLÇÜLMEDİ):** bu kısım yalnız GitHub CI'da ölçülebilir. Yerelde MinGW yok;
  Inno Setup'ı wine altında kurma denemesine izin verilmedi.
  - `TulparEditor.exe`: GUI alt sistemli başlatıcı (`app/baslatici_win.cpp`). engine_editor'u
    konsol penceresi açmadan, aynı argümanlarla başlatır. engine_editor konsol uygulaması kalır,
    çünkü kapılar ve testler stdout'unu okur.
  - exe ikonu `windres` ile gömülür (`tools/kurulum/editor_ikon.rc.in`).
  - `tools/kurulum/windows.iss`:
    - kullanıcı başına kurulum (`%LOCALAPPDATA%\Programs\Tulpar Editor`);
    - Başlat menüsü kısayolu, isteğe bağlı masaüstü kısayolu;
    - HKCU'da `.sahne` → `TulparEditor.exe "%1"` ilişkisi;
    - kaldırınca `.guncelleme` dizini de silinir.
  - Kapı `tools/kurulum/windows_kurucu.sh`:
    - derle → sessiz kur → kurulu yoldan kaynaksız koşum ve Yeni oyun → kaldır (dizin ve kayıt
      temizlendi mi);
    - pozitif kontrol: sahnesiz paketin kurulumu düşer.
  - `ci.yml`: Linux işine AppImage, Windows işine kurucu adımı eklendi.
  - `release.yml`: iki dosya da sürüme eklenir (SHA256SUMS dahil).
  - İlk "gönder"de kırmızı dönerse buradan düzeltilir.

### U8 — Yardım ve destek · ~1 oturum

- **Yardım** menüsü: Başlangıç rehberi (uygulama içi kısa sayfalar: ilk oyun, sahne, betik, Oynat,
  APK), Kısayollar (komut tablosundan üretilir — elle liste yok), Belgeler (tarayıcıda), Hakkında
  (sürüm, lisanslar).
- Çökme sonrası açılışta: "editör beklenmedik şekilde kapandı — rapor: <yol>, klasörü aç".
- **Kapı:** kısayol sayfası komut tablosuyla aynı sayıda satır (tablo değişince sayfa kendiliğinden).

**U8 notu (2026-11-06, yapıldı):**
- **Yardım menüsü** (`app/editor_yardim`):
  - Başlangıç rehberi: 5 kısa sayfa (ilk oyun, sahne, betik, oynat, Android).
  - Kısayollar: ÇALIŞAN komut tablosundan üretilir; her komut bir satır, aramalı.
  - Belgeler: GitHub `docs/`, tarayıcıda açılır.
  - Hakkında zaten vardı.
- **Çökme bildirimi:**
  - Pencereli editör ayar dizinine `oturum.acik` işareti yazar, temiz kapanışta siler.
  - İşaret durursa açılışta "beklenmedik şekilde kapandı" penceresi çıkar. En yeni çökme raporu
    gösterilir; "Klasörü aç" ve "Yolu kopyala" düğmeleri vardır.
  - Editöre `platform/crash` artık kuruluyor; raporlar `<ayar>/cokme` altına yazılıyor. Daha önce
    editörde hiç kurulu değildi.
  - İşaret, güncelleme sonrası yeniden başlatmadan ÖNCE silinir; yoksa yeni sürüm eski oturumu
    "çökmüş" sanardı.
- **Örnek sahne salt okunur yerdeyse** (AppImage, Program Files) açılışta söylenir: "Farklı kaydet
  ile kendi klasörünüze kaydedin". U7b'nin bilinen sınırı buydu.
- **Yan bulgu:** Stats kaplaması görünüm oturunca (kare 1–2) ilk kez açılıyor ve ImGui onu öndeki
  pencerelerin üstüne koyuyordu. Açılışta açılan çökme penceresi onun ALTINDA kalıyordu. Yardım
  pencereleri artık kare 4'ten sonra çiziliyor.
- **Kapı** (`editor_ux.sh --kapi`):
  - rehber, kısayollar ve çökme 1280x720'de kesik metin 0;
  - kısayol sayfası 29 satır = tablo 29 komut.
  - C++ testleri: tabloda kısayol değişince sayfa da değişiyor (pozitif kontrol); oturum işareti
    çökmeyi yakalıyor, temiz kapanışı yakalamıyor.

### U9 — (ayrı karar) Bileşen boşlukları · kapsam U0'dan sonra belirlenir

D14 yeniden ölçülür (`EDITOR-DURUM.md` matrisi 2026-09-18 tarihli). Oyun yapanın en çok ihtiyaç
duyacağı ikisi önerilir: **Ses** ve **Kamera** bileşeninin oyuna aktarılması (blob + runtime).
Parçacık/Su/Voksel panelleri sonra. Bu aşama "normal uygulama" ölçütünün parçası değil; ayrı onay.

**U9 notu (2026-11-06, kullanıcı onayladı ve yapıldı) — Kamera ve Ses oyunda:**
- **Blob v10:** `SceneBlobCamera` (fov derece, yakın, uzak) ve `SceneBlobAudio` (klip yolu metin
  tablosunda; düzey; döngü ve 3B bayrakları).
  - Başlığın sonuna dört u32 eklendi. Tablolar en sonda, yani kamerasız/sessiz sahnenin bölüm
    ofsetleri değişmedi.
  - `scene_blob_open` her kaydı ölçer: varlık sınırı, klip metin sınırı, bilinmeyen bayrak, ayrılmış
    alan 0, geçerli kamera.
  - `.sahne` ayrıştırıcısı geçersiz kamerayı satır numarasıyla reddeder. Editörün "Uzak kırpma"sı
    yakından küçük olamaz.
- **Okuyan taraf (köprü), v7 kuralına göre aynı değişiklikte:**
  - Sahne yüklenince ilk kamera uygulanır (göz, −Z bakış, açı, yakın/uzak kırpma). Yakın/uzak
    artık sabit değil, köprü alanı.
  - Ses açıksa klipler yüklenir ve döngülüler başlar. 3B olanlar her kare varlığın konumunu izler.
  - Boşaltma, sahnenin döngülü seslerini durdurur.
  - Yeni dil yüzü: `sahne_kamera_kullan(i)` / `scene_camera_use`, `sahne_ses_cal(i)` /
    `scene_sound_play`.
- **Perde (`audio_pitch`) TAŞINMIYOR:** mikserde çalma hızı yok. Bu, `scene_check` muafiyeti
  olarak gerekçesiyle yazıldı; kapının ilk koşusu yakaladı. Editörde "Perde (oyunda henüz yok)"
  yazıyor.
- **Sürüm geçişi:** eski (v9) `.sahneb` açılmaz, "yeniden derleyin" der. Fabrika şablonunun ve
  Küp-Küre'nin `ortam.sh`'i artık sahnec blob'dan yeniyse de yeniden derliyor; yalnız mtime'a
  bakılsaydı v9 blob kalırdı.
- **Testler:**
  - `scene_blob_v10_camera_and_audio`: değerler bit-tam; 5 bozuk kayıt reddedilir.
  - `bridge_scene_camera_and_audio_components`, kontrollerle birlikte: ses kapalıyken çalmaz;
    kamerasız sahne kamerayı değiştirmez; kamerasız varlıkta `camera_use` hata verir.
  - Eski sürüm reddi 1..9.

### U10 — Betikler herkes için (kullanıcı isteği, 2026-11-11)

**Kullanıcının sorusu:** "Betikler tam olarak neye bağlı, kendi kendine çalışabiliyor mu? Betiğe
basınca seçili nesneye ekleniyor ama içinde ne var göremiyorum. Hiç bilmeyen biri için bile kolay
olmalı; Unity/Unreal'in öğrenme süresi uzun olduğu için bunu kullanıyoruz."

**Bugün nasıl çalışıyor (koddan ölçüldü):**
- Betik bir `.tpr` dosyası. Adından türeyen işlevleri motor **adıyla** çağırır: `dondur.tpr` →
  `dondur_baslat(id)` bir kez, `dondur_guncelle(id, dt)` her kare, `dondur_carpisma(...)`,
  `dondur_bitir(id)`. Bu Unity'nin `Start` / `Update` / `OnCollision`'ı.
- Betik **kendi başına çalışmaz**. Oyunun ana dosyası `oyun.tpr` (motoru açar, sahneyi yükler,
  kare döngüsü) betiği **import etmek zorunda**; etmezse motor "betik kancası YOK" hatası basar
  ve betik sessizce hiçbir şey yapmaz. Editör bu import satırını hiçbir yere yazmıyor.
  - Fabrika projelerinde (asıl kullanım) "Yeni betik" import yolunu bile hesaplayamıyor; iskelete
    `<oyunun import kokunden bu dosyanin yolu>` yazıyor.
- Kaynaklar'da `.tpr`'ye çift tıklamak onu seçili nesneye **atıyor**. Unity'de ise çift tıklama
  dosyayı **açar**. Betiğin içi editörde hiçbir yerde görünmüyor; tek yol Betik kartındaki
  "Dış editörde aç" (VS Code / sistem uygulaması).
- Betik kartındaki açıklama ASCII ve teknik ("AOT", "import kokunden", "docs/KOPRU.md 7.9").
- Denendi (2026-11-11, fabrika projesi): `oyun.tpr` → `import "assets/betikler";`, editörün
  yazdığı `assets/betikler.tpr` → `import "assets/betikler/dondur";` zinciri çalışıyor
  (`dondur_baslat` duvarda koştu). Dosya boşken de oyun hatasız derleniyor.

**Yapılacaklar (sırayla; her madde ayrı commit, kapısı ve pozitif kontrolüyle):**

| # | ne | neden | durum |
|---|---|---|---|
| B1 | **Otomatik bağlama:** editör, sahnedeki betikleri `assets/betikler.tpr` toplayıcısına yazar (Kaydet / Derle / Oynat'ta); şablonlar `import "assets/betikler";` taşır. Eski projede satır yoksa editör **bir kez sorar** ve ekler. | Kullanıcı `import` kavramını hiç görmesin; atanan betik her zaman çalışsın. | ✅ 2026-11-11 (`app/editor_betik.cpp`; kapı `tests/arayuz/betik_{yeni,eski}.txt`) |
| B2 | **Betik editörün içinde açılır:** Kaynaklar'da çift tıklama ve Betik kartındaki "Düzenle" yerleşik **Kod** paneli açar: satır numaralı, Kaydet (Ctrl+S), kaydedilmemiş işareti, "Dış editörde aç" seçeneği. Atama sürükle-bırakla ya da sağ tık "Seçili nesneye ekle" ile. | "İçinde ne var göremiyorum." Unity'deki gibi çift tık = aç. | ✅ 2026-11-11 (kapı `tests/arayuz/kod.txt`; pozitif kontrol: geri al koruması kalkınca sahne geri alınıyor → kırmızı) |
| B3 | **Hata satıra gider:** "Oyun başlatılamadı" penceresindeki "Kodu aç (satır N)" önce Kod panelinde o satırı açar ve vurgular. | Hata → düzelt → F5 döngüsü editörden çıkmadan. | ✅ 2026-11-11 (kapı `tests/arayuz/hata_satiri.txt`) |
| B4 | **Yeni betik sade:** dosya diyaloğu yerine tek alan ("Betiğin adı"), dosya `assets/betikler/<ad>.tpr` olur; hazır davranış seçilir: Boş / Döndür / Yukarı-aşağı salla / Oyuncu kontrolü (ok tuşları) / Dokununca yok ol. İskelet sade Türkçe yorumlu; oluşturulunca Kod panelinde açılır. | Boş sayfa korkusu; ilk betik 10 saniyede çalışsın. | ✅ 2026-11-11: Boş / Ok tuşlarıyla hareket / Boşlukla zıpla / Dokununca yok ol (gerçek oyunda denendi). **B4b (2026-11-11):** Döndür ve Yukarı-aşağı salla eklendi; köprüye `sahne_konum_ver` / `sahne_donus_ver` / `sahne_dondur` / `sahne_ilk_*` (KOPRU.md). Kapı `tests/arayuz/hazirlar.txt`: oyunda dönen duvarın eski ucu boşalıyor, sallanan duvar oynuyor; pozitif kontrol hız/yükseklik 0 → yerinde. Betiksiz nesnede "Davranış ekle (betik)" düğmesi. Kapı `tests/arayuz/yeni_betik.txt` |
| B5 | **Betik kartı anlaşılır:** "Bu nesne ne yapsın?" başlığı; betik adı + Düzenle; dosyada bulunan olaylar işaretli liste (✓ başlarken, ✓ her karede, — çarpışınca); "oyuna bağlı ✓ / bağlı değil" satırı; ipuçları Türkçe harfli ve teknik terimsiz. | Kullanıcı neyin ne zaman çalıştığını kartta görsün. | ✅ 2026-11-11: kart adı "Davranış (betik)"; "Ne zaman çalışır?" listesi dosyadan okunur (değişince kendiliğinden tazelenir); "Oyuna bağlı ✓ / değil + Bağla"; elle yol, dış editör, ayrı pencere ve kod örneği yalnız Gelişmiş'te. Kapı `tests/arayuz/betik_karti.txt` (pozitif kontrol: aynı satır önce çizgili, `_guncelle` eklenince işaretli) |
| B6 | **Rehber:** Yardım > Başlangıç rehberi "Betik" sayfası bu modele göre yeniden yazılır (Unity karşılıkları tablosuyla: Start = `_baslat`, Update = `_guncelle`). | "Bu olayı anlamadım." | ✅ 2026-11-11: "Betik (davranış)" sayfası: ilk betik 4 adımda, olay adları + Unity karşılıkları, Kod paneli, hata satırı, ana dosya. Kapı `editor_rehber_betik_sayfasi_kodla_uyumlu_with_control`: sayfadaki kanca adları `betik_kancalari` ile tanınmalı, andığı komut tabloda olmalı (pozitif kontrol: `zipla_update` yazılınca kırmızı, ölçüldü) |
| B7 | **Ana betik görünür:** Oyun menüsünde "Ana betiği aç (oyun.tpr)" — oyunun kendisi de Kod panelinde. | Ana dosyanın varlığı ve rolü gizli kalmasın. | ✅ 2026-11-11: menü adı "Oynat" (Oyun menüsü yok); Kod paneli ana dosyada rolünü yazar ve "nesneye ekle" önermez; projede `oyun.tpr` yoksa (motor örnekleri) soluk. Kapı `tests/arayuz/ana_betik.txt` (kontrol: sıradan betikte "nesneye ekle" görünür) |

**Sonradan yapıldı (2026-11-12, kullanıcı isteği "hepsi önemli"):** Kod panelinde Tulpar sözdizimi — renklendirme
(TulparLang lexer'ının Türkçe + İngilizce sözcükleri), Enter girintisi / Tab 4 boşluk / `}` geri girinti, parantez
kapatma ve eşleştirme, öneri listesi (motorun `engine.tpr` + `kit/*.tpr` işlevleri imzalarıyla, dosyanın kendi
`func`'ları; Tab/Enter/Yukarı/Aşağı/Esc/Ctrl+Boşluk/fare), parametre ipucu balonu ve canlı söz dizimi denetimi
(`tulpar typecheck` + tek seferlik `tulpar --lsp`; ayrışma, tip, bilinmeyen işlev). Kapılar `tests/arayuz/kod_*.txt`.
**Kod sekmesi ve bağımsız pencere (2026-11-12):** Kod paneli "Görünüm"ün yanında sekme olarak açılır; "Ayrı pencerede aç" onu
**ayrı bir süreçte** (`engine_editor --kod <dosya> [--proje ..] [--kutuphane ..]`, `process_start_detached`) kendi işletim
sistemi penceresine ayırır: editör kapansa da açık kalır (aynı süreçte bir ImGui viewport editörle birlikte ölürdü). Pencere
yalnız Kod panelini tam ekran gösterir; kullanıcının tema/yazı boyutu tercihini okur; kaydedilmemiş değişiklikte pencere kapatma
iptal edilip "Kaydet / Kaydetmeden kapat / Vazgeç" sorulur (`Window::cancel_close`). Kapılar: `kod_sekme.txt`, `kod_ayir.txt`
(ana editör kapandıktan SONRA çocuk sürecin yaşadığı ölçülür), `kod_pencere.txt`, `editor_kod_penceresi_argv_*`. **Gerçek
işletim sistemi penceresi yolu (swapchain) bu oturumda çalıştırılmadı** (penceresiz yol ve sürücü doğrulandı).
**Geri birleştirme:** bağımsız pencerede "Editöre geri al": ayar dizinine `kod_istekleri/istek_<pid>_<n>.txt` (unix saniyesi + yol)
bırakılır; çalışan editör ~1/3 s'de bir yoklar, dosyayı sekme olarak açar ve isteği siler, pencere kapanır. Editör yoksa (ya da
kaydedilmemiş başka dosya açma izin vermiyorsa) pencere ~3 s sonra isteği geri alır, uyarır ve AÇIK KALIR; 60 sn'den eski istek atılır.
Kapılar: `kod_geri_al.txt`, `kod_geri_al_yok.txt`, `editor_kod_istegi_yaz_ve_al_*`.
**Canlı kod (2026-11-12):** oyun editörde çalışırken `oyun.tpr` ya da `assets/betikler/*.tpr` değişirse (Kod paneli, ayrı pencere, dış
editör: dosya parmak izi `betik_proje_damgasi`, otomatik üretilen `betikler.tpr` hariç) oyun kendiliğinden durdurulup yeniden başlar.
Tulpar AOT derlendiği için çalışan koda yama yapılmaz; derleme önbelleği yeniden başlatmayı kısa tutar. İki ardışık yoklamada aynı
yeni parmak izi görülünce tetiklenir (yarım yazılmış dosya). Derleme hatasında oturum AÇIK kalır: düzeltip kaydedince tekrar dener;
açık Durdur ya da oyunun kendi kapanışı oturumu bitirir. Tercihler > OYNATMA > "Kod değişince oyunu yeniden başlat" (varsayılan açık).
Kapılar: `kod_canli.txt` (2 başlatma) + `kod_canli_kapali.txt` (kontrol: tercih kapalı, 1), `editor_betik_proje_damgasi_*`.
**Canlı sahne:** oyun editörde çalışırken sahne değişirse (parmak izi, iki ardışık yoklama) editör `.sahneb`'yi kendiliğinden derler;
şablonlar `sahne_izle(true)` ile izler ve `sahne_degisti()` ile oyunu yeniden başlatmadan günceller (arena: parıltılar sahnedeki konum ve
sayıyla yeniden kurulur, oyuncu ışınlanır, skor/süre kalır; bilardo: masa baştan okunur, oyun sürüyorsa tur yeni dizilimle başlar).
Editörde çalışırken yapılan sahne düzenlemeleri Durdur'da geri alınır (mevcut kural). Tercih tek: "Değişiklikleri çalışan oyuna uygula".
Kapılar: `kod_sahne.txt` (+ kapalı kontrol), `kod_sahne_bilardo.txt`.
**Tanıma git / yeniden adlandır (2026-11-12):** Kod panelinde **F12** ya da **Ctrl+tık**: imlecin altındaki işlevin tanımına git (aynı dosya: tek seferlik
`tulpar --lsp`; kütüphane/başka dosya: LSP yalnız aynı dosyayı bilir, bu yüzden `func <ad>(` satırı proje + motor kütüphanesi dosyalarında
aranır; kütüphane dosyaları YALNIZ OKUNUR açılır), **Alt+Sol** geri (konum ve sütun korunur), **F2** yeniden adlandır (LSP `rename`; proje
dosyaları diske yazılır, açık dosya tampona ve kaydedilmemiş sayılır). LSP `t"{...}"` içindeki kullanımları görmez: bunlar ayrıca tam sözcük
eşleşmesiyle adlandırılır. Reddedilen adlar: dil sözcüğü, geçersiz karakter, **betik kancası** (`<dosya>_baslat` vb.: motor adı dosya adından
arar). Yalnız işlev adları (LSP global değişkenler için null döner). Kapılar: `kod_tanim.txt`, `editor_kod_tanim_*`, `editor_kod_sablon_*`.
**Geçmiş paneli (2026-11-12):** sağ sütunda (Özellikler / Dünya / **Geçmiş**; alt sol sütunda üçüncü sekme "Kayna…" diye kısalıyordu) kullanıcı eylemleri
listelenir (`OpGroups`: bir eylem = günlükte N işlem; "Taşı: duvar", "Sil: …", "Ekle: …", "… ve N nesne daha"). Bir satıra tıklamak o âna
döndürür (editörün kendi `do_undo`/`do_redo`'su: gruplar, fizik, kaynak bakımı aynı); yinelenebilir eylemler soluk. Kapı: `gecmis.txt`
(9 / 10 / 12 varlık), `editor_gecmis_listesi_*`.
**Hâlâ kapsam dışı:** görsel betik, tanıma git / yeniden adlandır (LSP destekliyor, editöre bağlı değil).
(Betik sıcak yükleme köprüde var, editöre bağlanması ayrı iş.)

### U11 — UX mükemmelliği (kullanıcı kararı, 2026-11-11)

**Kullanıcı:** "Arayüzü mükemmel hale getirmeye devam edeceğiz. UX'i abartacağız." "Bu turların hepsini
yapacağız." Fabrika aşama 3–5 (Play, reklam, ilk oyun) bu iş bitene kadar ertelendi.

**Ölçüm (2026-11-11, arayüz sürücüsü, Basit kip, 1600×900 + 1280×720):**
- Gezilen akış: karşılama → yeni oyun → F5 → durdur → seç/ekle → yeniden adlandır → gizmo → geri al →
  kaydet → menüler.
- Puan **7,2/10**; çıta "mükemmel" (Unity/Godot cilası). Önceki ~7,5 bir tahmindi.
- Alanlar: betik 8,5 · sahne düzenleme 8 · özellikler 7,5 · oynat 7,5 · yardım 7,5 · açılış 7 · görsel cila 7 ·
  küçük ekran 7 · **dil/geri bildirim 5,5** · **ayarlar 4**.
- Her turdan sonra aynı gezinti tekrarlanır ve yeniden puanlanır.

**Tur A — en çok kazanç**

| # | ne | bulgu | durum |
|---|---|---|---|
| A1 | **Basit kipte sade dil:** Konsol'da ham motor satırları (`[engine_bridge] k421 bilgi …`) Basit'te gizli (Gelişmiş'te ve süzgeçle görünür); durum çubuğu ve Konsol'da yollar kısaltılır (proje köküne göre); Türkçe harfsiz metinler düzelir ("geri alindi"); Basit'te fps / çizim / kamera koordinatı katmanları gizli | Kullanıcı motorun iç günlüğünü okuyor | ✅ 2026-11-11: Konsol girdisinde **teknik** bayrağı; Basit'te teknik BİLGİ gizli (uyarı/hata her kipte), rozet sayaçları görünenleri sayar. Oyun satırları `[engine_bridge] kN bilgi [tpr]` önekinden arınır; `logla_ayrinti` (engine.tpr, `[ayrinti] ` öneki) teknik sayılır, şablonların GPU/kapanış satırları ona geçti. Yollar kayıt anında kısalır (proje kökü silinir, ev `~`; Konsol + durum çubuğu). ~35 ASCII Türkçe ileti düzeldi ("geri alındı", kaydedilmemiş değişiklik penceresi, ipuçları). Basit'te fps/çizim/kamera hapı ve durum çubuğu ms'i yok. Oyun sekmesi "Oyun hazırlanıyor… N sn" + ilerleme şeridi; "çıkış kodu 0" yerine "oyun bitti"/"durduruldu". Kapılar: `console_shortens_paths_with_control`, `console_basit_hides_technical_info_with_control`, overlay testine Basit karşı ölçümü (4 hap, kamera yok), `tests/arayuz/sade_dil.txt` (pozitif kontrol: Gelişmiş'te motor satırı + fps + kamera görünür). Bulunan hata: kullanıcı durdurunca "oyun hatayla bitti (kod 0)" diyordu (koşul), düzeldi ve senaryoda denetleniyor |
| A2 | **Temiz Durdur:** gömülü oyun durdurulunca "editör 5 s sessiz → kapanıyor" UYARISI çıkmaz; editör kapanış sinyali verir | Her oynatmadan sonra sahte ▲1 | ✅ 2026-11-11: ürün hatası DEĞİLDİ — kullanıcı yolunda Durdur temiz (`gomulu: editor durdurdu`, betik kipinde ölçüldü). Uyarıyı canlı sürücü üretiyordu: FIFO'yu bloklayarak okuyor, komut beklerken editör kare çizmiyordu → oyun 5 s sonra kapanıyordu. Canlı kip artık bloklamasız (O_RDWR\|O_NONBLOCK, boşta 16 ms uyku); ölçüm: 9 s komutsuz bekleme sonrası oyun 546. karede editörün Durdur'uyla kapandı, uyarı yok |
| A3 | **Açılış formu:** şablon kartları (ad, tek cümle açıklama, ne oynanır); İngilizce ad isteğe bağlı (Türkçeden önerilir); başlıkta "(kaynak derlemesi)" yok; arka plan karartılır; konum kısa gösterilir | Şablon "arena" açıklamasız, İngilizce ad zorunlu | ✅ 2026-11-11: şablon **kartları** (`tulpar/sablonlar/<tür>/sablon.txt`: ad, açıklama, yön, ikon; kart seçilince ekran yönü şablonun önerisine geçer; sürücü kartı adıyla bulur); İngilizce ad isteğe bağlı (ipucu "boş: <Türkçe ad>", boşsa Türkçe ad kullanılır); "(kaynak derlemesi)" yalnız Gelişmiş'te; konum `~/TulparOyunlari/…` + "Değiştir…", tam yol Gelişmiş'te. **Karartma gerçekten çalışmıyordu:** ayrı pencere panellerin altında kalıyordu; ön plan listesine taşındı ve alfa 175 → 225 (çerçeve tamponu sRGB, karışım doğrusal: 175 algıda ~%35 karartıyordu; ölçüldü yazı 230 → 144, 225 ile 95). Kapılar: `fabrika_sablon_kartlari_with_control` (her şablonun kartı dolu), `tests/arayuz/karsilama.txt` (Bilardo kartı + boş İngilizce ad → proje `// Kart Deneme — Kart Deneme`; kontrol: ad yokken uyarı satırı) |

**Tur B**

| # | ne | bulgu | durum |
|---|---|---|---|
| B1 | **Tercihler penceresi:** yazı boyu, tema, otomatik kayıt, Basit/Gelişmiş, dil; **Görünüm > Düzeni sıfırla** | Ayarlar penceresi yok (puan 4) | ✅ 2026-11-11: **Düzen > Tercihler… (Ctrl+,)** — yazı boyu %90/%100/%115/%130 (tüm arayüz ölçeği, `set_ui_scale`), Basit/Gelişmiş, otomatik kaydet (kapalı/1/2/5/10 dk; oynatırken, adsız sahnede ve Kod paneline yazarken kaydetmez), güncelleme denetimi, panel düzenini sıfırla. `tercihler.txt` (anahtar = değer; bilinmeyen atlanır, geçersiz değer en yakına). **Görünüm > Düzeni sıfırla** komut tablosunda (eskiden "⋮" menüsüne gömülüydü). Tema ve dil YOK: açık tema ayrı iş (C6), editörün İngilizce arayüzü yok. Kapılar: `tercih_dosyasi_gidis_donus_with_control`, `tests/arayuz/tercihler{,_kalici}.txt` (yazı boyu kalıcı: menü öğesi 22 → 26 px; küp elle kaydetmeden diske; kontrol: varsayılan boy + aralık dolmadan "otomatik kaydedildi" yok) |
| B2 | **Sade "+" menüsü:** Basit'te az ve anlaşılır kategori, teknik adlar Gelişmiş'te ("Model (glTF)", "Çevre Hacimleri"); "Tulpar Betik Nesnesi" yeni adıyla; **Hızlı ekle** seçim varken de erişilir | 14 kategori; seçim varken hızlı ekle kayboluyor | ✅ 2026-11-11: Basit kipte ayrı ağaç (9 üst / 24 yaprak, günlük dil: "Şekiller", "Fizikli nesneler: Düşen kutu, Yuvarlanan top, Duvar / engel, Karakter", "Işık", "Doğa", "Efektler", "Kamera", "Ses kaynağı", "3B model (dosyadan)", "Boş nesne"); tam ağaç (14 / ~60) Gelişmiş'te, "Tulpar Betik Nesnesi" → "Davranışlı Nesne (betik)". Ana menü çubuğunda **Ekle** (Seçim'den sonra; Unity'nin GameObject menüsü): seçim varken de ekler, aynı tablo. Menü çubuğu kancasına `after` (tablodan gelmeyen üst menü). Kapılar: `create_menu_basit_subset_with_control` (Basit ⊂ tam, teknik ad yok), `tests/arayuz/ekle_menu.txt` (seçim varken Küp eklenir 8 → 9; kontrol: Gelişmiş'te "Çevre Hacimleri") |
| B3 | **Okunur yeni nesne adları:** "Küp", "Küp 2" (Türkçe harfli, boşluklu) | `kup_6` | ✅ 2026-11-11: `editor_benzersiz_ad` — okunur Türkçe kök ("Küp", "Top", "Lamba", "Duvar", "Ses kaynağı"…; tablo do_add kodlarıyla) + sahnede ilk boş numara ("Küp 2"); çoğaltma ve **yapıştırma** da aynı kural (yapıştırma eskiden aynı adı kopyalıyordu → betiklerin `sahne_bul`'u belirsizdi); sondaki " N" atılır ("Küp 2" → "Küp 3"), 32 bayta UTF-8 harf bölmeden sığar. Kapılar: `editor_benzersiz_ad_with_control`, `ekle_menu.txt` (Küp → Ctrl+D → "Küp 2"; kontrol: önce "Küp 2" yok) |
| B4 | **Yeni projede "ilk adımlar" kartı:** ne oynanır, F5, ilk nesne, ilk davranış; şablonun kodda doğan oyuncu/parıltıları editörde işaretli ya da açıklanmış | Yeni proje boş kutu gibi | ✅ 2026-11-11: Basit kipte (fabrika projesi, seçim yok) **İlk adımlar** kartı: "Bu oyunda: …" (`sablon.txt` açıklama) + "koddan doğar" notu (yeni `kodda` anahtarı) + dört adım (Oyunu dene · Bir nesne ekle · Bir nesneye davranış ver · Kaydet) — durum değişikliklerinden kendiliğinden işaretlenir, yapılmamış adım tıklanınca işi başlatır/yolu gösterir; bitince "Başlangıç rehberi". İlerleme ve gizleme proje başına KULLANICI ayar dizininde (`ilk_adimlar.txt`; proje klasörüne yazılmaz, 64 proje tavanı). Sahne kökü ipucundaki "Müfettiş" → "Özellikler". Kapılar: `ilk_adim_dosyasi_with_control`, `tests/arayuz/ilk_adimlar{,_gizli}.txt` (ekle + kaydet işaretlenir, gizle kalıcı; dosyada bitler 26) |
| B5 | **Boş Kaynaklar:** teknik "Bu dizinde .gltf/.glb yok" yerine ne konacağı + **İçe aktar** düğmesi; yol `…//assets` çift eğik çizgisiz | Boş durum eylemsiz | ✅ 2026-11-11: boş durum "Burada henüz 3B model ya da betik yok" + **Model içe aktar…** + **Yeni betik**; dar panelde metin sarılır, düğmeler alt alta. Başlıkta kalıcı "+" (içe aktar). `editor_model_ice_aktar`: .glb / .gltf proje klasörüne kopya, .gltf'nin göreli `uri` bağımlılıkları (alt klasör dahil) gelir, `data:` ve `../` atlanır, aynı adlı model ezilmez (reddedilir), Windows `\` ayracı. Yol kısa ("assets"; Konsol'la aynı kural, "//" tek). Dosya seçicide kapı arka ucu `TULPAR_DOSYA_SECICI=sabit:<yol>`. Kapılar: `editor_model_ice_aktar_with_control`, `tests/arayuz/kaynaklar.txt` (kontrol: içe aktarmadan önce karo yok) |

**Tur C — cila**

| # | ne | bulgu | durum |
|---|---|---|---|
| C1 | İkon tutarlılığı (Kaydet indirme oku değil disket), menülerde eksik ikonlar | | ✅ 2026-11-11: araç çubuğu ve görünüm çubuğu TEK aileden (Material): Oynat/Durdur, Taşı/Döndür/Ölçekle, Yakala (ızgara), Gizmolar (göz; eskiden güneş), Kaydet (disket; eskiden indirme oku), Derle (anahtar), Izgara açık/kapalı (+ sabit `###izgara` kimliği: etiket değişince düğme yeni öğe sayılıyordu). İkon kapısı 0 eksik glif |
| C2 | Sayı biçimi: `-0.0°` yok; ölçü birimleri tutarlı | | ✅ 2026-11-11: `gosterim_sifirla` — biçimin basamağında sıfıra yuvarlanan değer (−0.0f, gizmodan kalan −0.00001) GÖSTERİMDE işaretsiz 0; veri yalnız kullanıcı değiştirirse yazılır (sessiz düzeltme yok). `prop_vec3` / `prop_float` hepsi. Kapı `gosterim_sifirla_eksi_sifir_with_control` (kontrol: −0.06 @ %.1f korunur); canlı: gizmo sürüklemesi sonrası dönüş "0.0°" (eskiden "-0.0°") |
| C3 | 1280×720'de kesilen etiketler (Dünya: "Kamera gör…", araç çubuğu sağ ucu) + bu çözünürlük için UX kapısı | Kapı yalnız büyük çözünürlükte | ✅ 2026-11-11: **ölçümün kör noktası kapandı** — UX ölçümü yalnız taşan metni sayıyordu, `editor_ellipsize`'ın "…" ile kısalttığı etiketleri görmüyordu: yeni **kısaltma** kaydı (pencere + kare sayısı). Kapı: `tests/arayuz/kesik_gezinti.txt` (Dünya, bileşenli nesneler, Tercihler, Konsol, Ekle) 1280×720'de iki kipte kalıcı kısaltma/kesik 0 (`editor_ux.sh --kapi`; pozitif kontrol: Konsol satırlarında kısaltma > 0, `editor_ux_kisaltma_olculur_with_control`). Düzeltmeler: onay kutusu satırında etiket değer sütununa taşar (kutu sağda); **uyarlanır etiket sütunu** (önceki karenin en uzun etiketi, panelin %50'sine kadar); varsayılan düzende Özellikler alt sınırı 300 px (1920 görünüm oranı 0.486 korundu); varlık seçici önizlemesi dosya adı (klasör/uzantı yok, ipucunda tam yol; kullanıcı verisi ölçümde sayılmaz); "Hüzme Yoğunluğu" → "Yoğunluk" (bölüm zaten Hüzmeler); betik kartı uyarısı sarılır |
| C4 | Varsayılan ışık ve gökyüzü: yeni sahne düz görünmesin | | ✅ 2026-11-11: **"Izgara" düğmesi hiçbir şey yapmıyordu** (`show_grid` yalnız değiştiriliyor, hiçbir yerde okunmuyordu) — artık editörün 3B geçişinde derinlik testli zemin ızgarası (100 m, 1 m ince ara / 10 m ana çizgi, kendi dokusu, mipmap ile uzakta soluk, gölge düşürmez); boş yeni sahne artık boşlukta değil. Arena şablonu: yatık güneş (0.55 0.75 0.45, 1.05), biraz aydınlık ortam, koyu zemin + açık duvar (görünüm parlaklık sapması 34.1 → 39.7; oyunun yedek kodu aynı değerlerle; şablon doğrulaması 3/3). Kapı `tests/arayuz/izgara.txt` (açık/kapalı sapma 12.9 / 1.5; iki görüntü birbirinin kontrolü) |
| C5 | Seçim yokken Özellikler'de Dünya ayarlarının tekrarı yerine sahne özeti; gizmo görünürlük ayarları Görünüm'e | Aynı ayar iki yerde | ✅ 2026-11-11: seçimsiz Özellikler = başlık + hızlı ekle + ilk adımlar/ipucu + **SAHNE** özeti ("8 nesne · 1 ışık · 2 fizikli · 1 davranışlı", "Gün saati 14:00 · sis kapalı") + "Dünya ayarları" (Dünya sekmesini öne getirir). Gizmo görünürlükleri dünya ayarlarından **Görünüm > Gizmolar (görünüm)** alt menüsüne (sahnenin değil görünümün ayarı). Sahne kökü ipucu güncel. Kapı `tests/arayuz/sahne_ozeti.txt` (kontrol: ayarlar Dünya sekmesinde) |
| C7 | Basit kipte araç çubuğu sade: "Derle" (F5 zaten derler), "Yakala", "Gizmolar" Gelişmiş'te | Tur B gezintisi (2026-11-11) | ✅ 2026-11-11: Basit'te araç çubuğu Oynat · Taşı/Döndür/Ölçekle · Kaydet; üçü menü ve kısayolda durur. Kapı `editor_chrome_toolbar_basit_sade_with_control` (Basit: yakala/gizmolar/derle çizilmez; kontrol: Gelişmiş'te üçü de çizilir) |
| C8 | Bilardo şablonu kopyalanan topları görmüyordu (kullanıcı, 2026-11-11: "kopyalanan topların içinden geçiyor") | Betik sabit `top_0..top_15` arıyordu | ✅ 2026-11-11: toplar sahneden adla toplanır — `top_0` beyaz, adı "top" ile başlayan her nesne bir top (kopya "top_3 2", yeni "top_ekstra"); sahnede top yoksa 16'lık yedek. Ölçüldü: kopyalı sahnede yeni betik 17, eski 16 top; şablon doğrulaması 3/3 |
| C6 | **Açık tema** (Tercihler'de Tema seçeneği): palet `constexpr`, ~34 yerde elle `IM_COL32` — hepsi tona bağlanır, iki palet, ekran görüntüsü kapısı | B1'de işlevsiz seçenek göstermemek için ayrıldı | ✅ 2026-11-11: palet bir YAPI (`Palet`; koyu = eski sabitlerin bayt aynısı — eski sürümle penceresiz kare piksel piksel aynı, 0 fark), **açık tema** (girdi beyaz, düğme panelden bir ton koyu, vurgu/durum renkleri açık zeminde koyulaştırılmış); `theme_colors` içindeki elle yazılmış durum renkleri palete girdi. Tercihler > Tema Koyu / Açık (`tema = 0/1`, kalıcı). Renkli zemin üstündeki koyu mürekkep temadan bağımsız (`editor_koyu_murekkep`: renk örneği yazısı açık temada beyaz üstüne beyaz kalıyordu; eksen harfleri, ✚ ekle düğmesi, OYNATILIYOR hapı). Kapılar: `editor_tema_karsitligi_with_control` (WCAG: metin/panel koyu 15.3 açık 14.5; ikincil 6.2 / 7.1; vurgu ≥ 3; kontrol: aynı renk 1.0), tercih kapısı panel zemini 29 → 233 (kalıcı; kontrol: varsayılan koşu koyu) |

## 4. Sıra ve bağımlılık

```
U0 ──► U1 ──► U5
 │             ▲
 └──► U2 ──► U3 ──► U6
              │
              └──► U4      U7 (U3'ten sonra; pakete şablon/örnek girer)      U8 (en son)
```

Önerilen sıra: **U0 → U1 → U2 → U3 → U4 → U5 → U6 → U7 → U8** (~15–17 oturum). En erken görünür
kazanç U1 (bir oturumda puan ~+1). Kurucu (U7) U3'ten sonra, çünkü pakete şablon ve örnek proje
girmeli.

## 5. Mimari notlar (başlamadan bilinmesi gerekenler)

- **Katman kuralı:** açılış ekranı, fabrika çekirdeği, dosya seçici `app/` (L6); fabrika çekirdeği
  dosya işlemleri için yalnız `platform/` (L0) kullanır. STL yasağı sürer (`StaticString`, `Array`).
- **Tek kaynak:** fabrika mantığı C++'a taşınınca `tools/yeni_oyun.sh` ince sarmalayıcı olur
  (`engine_editor --komut yeni_oyun`); yerel CI'ın Fabrika aşaması ikisini karşılaştırır.
- **Basit/Gelişmiş** kip yeni bir arayüz değil, bugünkü pencerelerde görünürlük bayrakları + Stats'in
  iki sürümü — Gelişmiş bugünkü ekranla bayt aynı kalmalı (headless ekran görüntüsü kapısı).
- **Headless ölçülebilirlik** korunur: her yeni ekranın `--headless --komut` ile sürülebilen ve
  `[kapi]` satırı basan bir yolu olur; pencereli yol yalnız kullanıcı denemesinde.
- **Güncelleyici** (`app/updater.hpp`) kurulum dizinine yazar: kullanıcı başına kurulum şart (K2);
  AppImage'ta davranış U7'de ölçülerek seçilir.

## 6. Riskler

| risk | etki | önlem |
|---|---|---|
| Linux'ta dosya seçici portalı olmayan masaüstleri | seçici açılmaz | görünür ImGui yedeği (U4) |
| Windows'ta bash/Python yok | fabrika ve APK menüsü çalışmaz | U3/U6 çekirdeği C++'ta |
| Inno Setup yalnız Windows'ta derlenir | Linux'tan kurucu üretilemez | kurucu CI Windows ayağında; yerelde ölçülemez → CI'ın kapısı |
| AppImage + güncelleyici | yerinde güncelleme yetkisi | U7 başında ölçüm, gerekirse "indir" yönlendirmesi |
| Basit kip gerçek işi gizler | ileri kullanıcı kaybolur | tek tık Gelişmiş, tercih kalıcı (U2) |
| Ekran görüntüsü kapıları sürücüye duyarlı | sahte kırmızı | ölçüm metinden/dikdörtgenden (U0), piksel değil |

## 7. Kapsam dışı

Play'e yükleme (kullanıcı kararı), macOS `.app`, iOS, eklenti mağazası, çok kullanıcılı/bulut proje,
editör içi kod düzenleyici (dış editör açılmaya devam eder), YZ ile tasarımdan oyun üretimi.

## 8. Ölçüm tablosu (U0'da doldurulacak)

Araç: `tools/editor_ux.sh` (dört çözünürlük paralel, `engine_editor --ux-rapor`; `--kapi` eşikli).
Ölçüm `app/editor_ux.cpp` + ImGui metin kancası (`third_party/imgui` TULPAR YAMASI, imconfig.h);
yamanın canlı olduğunun pozitif kontrolü `editor_ux_imgui_kancasi_kesik_metni_gorur` (dar pencerede
uzun etiket → kesik 1; geniş pencerede 0; kanca kapalıyken sayım değişmez).

**Taban (U0, 2026-11-05, RTX 5080, 60 headless kare, `tests/assets/editor.sahne`):**

| çözünürlük | kesik metin | Stats taşan kare | 3B görünüm oranı | görülen metin |
|---|---|---|---|---|
| 1280×720 | **21** | **41 / 60** (96 px) | 0.300 | 86 |
| 1366×768 | 18 | 41 / 60 (60 px) | 0.310 | 88 |
| 1920×1080 | 5 | 0 | 0.349 | 96 |
| 2560×1440 | 4 | 0 | 0.374 | 125 |

Kesiklerin dağılımı (1280×720): Özellikler 16 (Dönüşüm X/Y/Z değerleri `-11.212` → 26 px alanda,
`+ Zemin Sisi`), Dünya 2 (`0.50`, `1.00`), Görünüm 1 (`Serbest En-Boy`), dock sekmesi 1 (`Ko…`
= Konsol). 1920×1080'de bile 5 (Dünya 2, sekme, en-boy, `-10.068`).

Yan bulgu: headless editörün yerleşik "düzen kapısı" sabit `.duzen_kapisi.duzen` adını kullanıyordu;
dört çözünürlük paralel koşunca birbirinin dosyasını silip kapıyı düşürdü (Tuzaklar 8dr sınıfı). Ad
artık süreç kimliğiyle.

## 9. Kabul testi (her platformda kullanıcı yapar; ajan headless kapılarla ön doğrular)

1. Kurucuyu indir, kur (yönetici şifresi istenmemeli). Süre: ___
2. Başlat menüsü / uygulama menüsünden aç; açılış ekranı geldi mi?
3. Yeni oyun → ad "Deneme Oyunu" → Oluştur. Sahne açıldı mı? Süre: ___
4. Bir nesneyi taşı, Ctrl+Z, tekrar taşı. Oynat. Oyun açıldı mı (tulpar yoksa kurulum ekranı)?
5. Kaydet, kapat, yeniden aç → Son projeler'de var mı, değişiklik duruyor mu?
6. Dosya > Aç: işletim sisteminin dosya seçicisi mi geldi?
7. Ekranda anlamadığın bir terim gördün mü? (liste)
8. Toplam süre ≤ 10 dk mı? Puan (1–10): ___

**Ajanın ön doğrulaması — arayüz sürücüsü (2026-11-06, `docs/ARAYUZ-SURUCUSU.md`):** penceresiz
editör komutla sürülüyor (tıkla / yaz / tuş / ekran görüntüsü; hedef etiketle). Adım 3–4, karşılama
ekranından gerçek tıklamalarla koşturuldu: Yeni oyun → ad + İngilizce ad + konum → Oluştur → sahne
açıldı → F5 → oyun Oyun sekmesinde çalıştı ("Toplanan 1"). Bulunan ve düzeltilenler:

- **F5 yeni projede düşüyordu:** derleyici "eklenti bulunamadı: .motor/tulpar-ext (tulpar.toml)"
  dedi. `.motor` bağını yalnız `tools/ortam.sh` kuruyordu, yani yalnız Oyun menüsü araçları.
  Artık oyun başlamadan editör kuruyor: `game_motor_bagla`. Windows'ta bu bir sembolik bağ, yetki
  yoksa junction. Test: `game_motor_bagi_kurulur_yenilenir_ezmez`.
- **Menüler yarı saydamdı:** `PopupBg` alfası 0.98'di; açık menünün altından araç çubuğu yazıları
  hayalet gibi okunuyordu (~%7 sızma ölçüldü). Artık opak.
- **Pasif "Oluştur" düğmesi etkin gibi görünüyordu:** pasifken (34,80,188), hazırken (38,99,235)
  ölçüldü; fark gözle seçilmiyordu. Vurgu rengi artık yalnız hazırken var.
- **Rehberde yanlış varsayılan klasör:** "Belgeler/TulparOyunlari" yazıyordu; Linux'ta varsayılan
  `~/TulparOyunlari`.

Kalan adımlar pencereli/kurulu yolda kullanıcıda: 1, 2, 5'in kapat/yeniden aç kısmı ve 6 (işletim
sisteminin seçicisi).

**U1 sonrası (2026-11-05, aynı ölçüm, `tools/editor_ux.sh --kapi` YEŞİL, yerel CI'da):**

| çözünürlük | kesik metin | Stats taşan kare | 3B görünüm oranı |
|---|---|---|---|
| 1280×720 | 21 → **0** | 41 → **0** | 0.300 → **0.431** |
| 1366×768 | 18 → **0** | 41 → **0** | 0.310 → **0.442** |
| 1920×1080 | 5 → **0** | 0 | 0.349 → **0.486** |
| 2560×1440 | 4 → **0** | 0 | 0.374 → **0.514** |

Yapılanlar: Dönüşüm X/Y/Z harfi düşürmek de yetmiyorsa **alt alta** (her eksen tam genişlik; sayı
kesilmez); hızlı ekleme ızgarası sütun sayısını en geniş etikete göre seçer; En-Boy açılırı en uzun
seçeneğe göre genişler; Stats varsayılan kapalı ve görünümün içine kenetli (sığmayan kısmı
kaydırılır); varsayılan yerleşimde alt şerit kalktı — Kaynaklar + Konsol sol sütunun altında, Dünya
Özellikler'in yanında sekme; Konsol dar panelde araç düğmelerini alt satıra alır, kare/etiket
sütunlarını gizler (ipucunda). Düzen kapısının mutantı iki ölçüyü de değiştirir (yeni ağaçta ilk yaprak
Y bölmesinin çocuğu; yalnız genişlik değişince kontrol "farklı HAYIR" diyordu).

Eşik: görünüm ≥ 0.45 (plandaki 0.55 tahmini tutmadı: Görünüm panelinin kendi araç satırları ~95 px).
Kapının duyarlılığı: 1024×600'de "Kaynaklar" sekmesi kesik=1 yakalandı. **Ölçülmeyen sınıf:**
pencerenin tamamen dışında kalan öğe (ImGui hiç çizmez, kanca görmez) ve üst üste binen öğeler —
Konsol'daki "Temizle"nin arama kutusunun altında kalması ekran görüntüsünde görüldü, sayıda değil.

**DÜZELTME (U5, 2026-11-05):** U0/U1 ölçümü ImGui'nin `Text/TextColored/TextDisabled` yolunu
(`RenderTextWrapped`) GÖRMÜYORDU — kanca yalnız `RenderText` ve `RenderTextClippedEx`'teydi; Stats'in
bütün satırları ve düz etiketler ölçüm dışıydı (Basit kip terim kapısı "Gelişmiş'te de 0" deyince
yakalandı). Üçüncü kanca eklendi; ölçüm hemen 2–3 gerçek kesik buldu (Stats p50 satırı 309 px / 289;
Özellikler'de "Kaynak: Sahne Güneşi (Gökyüzü)" ve 1366×768'de "ATMOSFERİK SİS LABORATUVARI (16 SİS
TÜRÜ)") — düzeltildi (satır kısaltıldı, sarma, bölüm başlıkları "…" + ipucu), dört çözünürlükte yine
**0**. Ayrıca canlı sayaçlı metinler (kare sayısı) her karede yeni "kesik" üretiyordu: özetlemede
ardışık rakamlar tek sınıf (test `editor_ux_rakamlar_tek_sinif`). U1 tablosundaki "0"lar bu yüzden o
gün için EKSİK ölçümdü.
