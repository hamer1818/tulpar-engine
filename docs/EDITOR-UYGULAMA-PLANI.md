# Editör → "normal uygulama" planı

> **Durum: UYGULANIYOR (başladı 2026-11-05).** Kararlar (kullanıcı): K1 Windows + Linux, K5 Basit
> varsayılan, K6 ilk Oynat'ta tek tıkla kur; K2/K3/K4/K7/K8 önerildiği gibi (Inno Setup kullanıcı
> başına, AppImage, nativefiledialog-extended, Tulpar logosu, APK menüsü U6'da).
> Aşamalar: **U0 ✅**, **U1 ✅** (aşağıda §8), **U2 ✅** (§U2 altındaki not), **U3 ✅** (a: fabrika çekirdeği C++'ta, b: arena şablonu sahneden, c: başlangıç ekranı), **U4 ✅** (sistem dosya seçicisi; K4 değişti, §U4 notu), **U5 ✅** (Basit/Gelişmiş + ilk tur; §U5 notu ve §8 düzeltmesi), **U6 ✅** (Tulpar'ı kur/güncelle penceresi, Oyun > APK / Doğrula; §U6 notu)
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

### U8 — Yardım ve destek · ~1 oturum

- **Yardım** menüsü: Başlangıç rehberi (uygulama içi kısa sayfalar: ilk oyun, sahne, betik, Oynat,
  APK), Kısayollar (komut tablosundan üretilir — elle liste yok), Belgeler (tarayıcıda), Hakkında
  (sürüm, lisanslar).
- Çökme sonrası açılışta: "editör beklenmedik şekilde kapandı — rapor: <yol>, klasörü aç".
- **Kapı:** kısayol sayfası komut tablosuyla aynı sayıda satır (tablo değişince sayfa kendiliğinden).

### U9 — (ayrı karar) Bileşen boşlukları · kapsam U0'dan sonra belirlenir

D14 yeniden ölçülür (`EDITOR-DURUM.md` matrisi 2026-09-18 tarihli). Oyun yapanın en çok ihtiyaç
duyacağı ikisi önerilir: **Ses** ve **Kamera** bileşeninin oyuna aktarılması (blob + runtime).
Parçacık/Su/Voksel panelleri sonra. Bu aşama "normal uygulama" ölçütünün parçası değil; ayrı onay.

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
