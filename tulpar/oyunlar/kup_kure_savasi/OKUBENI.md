# Küpler ile Kürelerin Savaşı

Çağlar Boyu Savaş (Age of War) tarzı bir strateji oyunu — Tulpar Engine'in ilk gerçek oyunu.
Solda **Küpler** (sen), sağda **Küreler** (yapay zekâ). Altın biriktir, birlik çıkar, kule kur,
deneyim (XP) kazanıp çağ atla, düşman kalesini yık.

## Nasıl oynanır

- **Altın** zamanla gelir; düşman öldürünce daha çok gelir. **XP** öldürme ve kaleye vuruşla birikir.
- Alt çubuk: üç birlik (yakın / menzilli / ağır — fiyatı altında), **Kule**, **özel güç**, **Çağ Atla**.
  Birlikler 5'lik eğitim sırasına girer (sol altta küçük kareler) ve kalenin kapısından çıkar.
- **Kule:** düğmeye bas, kale damında parlayan yuvaya dokun. Dolu yuvaya dokunmak kuleyi yarı fiyata satar.
- **Özel güç** (50 sn bekleme): Taş: kaya yağmuru · Tunç: ok yağmuru · Demir: mancınık · Sanayi: topçu · Gelecek: lazer.
- **Çağlar:** Taş → Tunç → Demir → Sanayi → Gelecek. Her çağda birlikler, kule ve kale canı büyür.
- Savaş alanını **parmakla sürükle** (kamera kayar); üstteki **haritaya dokun** (kamera oraya atlar);
  bir birliğe dokun (adı, canı, hasarı). Sağ üstte **||** duraklatır.
- Masaüstü: fare + `1/2/3` birlik, `T` kule, `Q` özel güç, `E` çağ atla, `A/D` kamera, `ESC` duraklat.
- Zorluk yalnız yapay zekâyı değiştirir (Kolay / Normal / Zor). Rekor, en uzak çağ, oyun/zafer sayısı ve
  ayarlar (efekt sesi, müzik, parlama, çözünürlük) kalıcı kayıtta.

## Çalıştırmak

```bash
# masaüstü (oyun KENDİ dizininden çalışır: Tulpar'da import yolu çalışma dizinine göre)
cd tulpar/oyunlar/kup_kure_savasi && tulpar oyun.tpr
# penceresiz doğrulama: otopilot iki tarafı da oynar, sonda [kapi] satırları
TULPAR_ENGINE_HEADLESS=9600 DISPLAY= tulpar oyun.tpr
# Android APK (motor deposunda tools/build_bridge_android.sh bir kez; TULPAR_ANDROID_LIB_DIR =
# TulparLang runtime arşivleri)
tulpar build --target=android oyun.tpr kup_kure --apk && adb install -r kup_kure.apk
```

Telefonda uygulamanın adı **"Küpler ile Küreler"** (paket `dev.tulparlang.kupkure`), yatay ekran.

## Dosyalar

| dosya | ne |
|---|---|
| `oyun.tpr` | ekranlar, girdi, HUD, kayıt, otopilot + `[kapi]` |
| `savas.tpr` | savaş çekirdeği: birlik, yol, mermi, kule, özel güç, enkaz, yapay zekâ |
| `ses.tpr`, `kamera.tpr` | ses (dosya yoksa sentetik ton), izdüşüm (dünya ↔ ekran) |
| `davranis/*.tpr` | motorun çağırdığı kancalar: kale/kule işaretleri, mermi çarpışması, enkaz ömrü, kale tetiği |
| `assets/savas.sahne` | savaş alanı (editörde açılır; motor derlenirken `.sahneb` üretilir) |
| `assets/modeller/*.gltf` | gövdesiz süs modelleri (`tools/kup_kure_modelleri.py` üretir) |
| `assets/sesler/` | müzik ve efektler (ayrıntı oradaki OKUBENI) |
| `ikon.png` | **yok — sağlanacak** (512x512 PNG; yoksa sistem ikonu) |
