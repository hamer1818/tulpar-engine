# Yeni oyunu Google Play'e çıkarmak — bir kerelik adımlar

Bu liste **kullanıcının** yapacağı kısmı söyler; araçlar (`play_sunucu.sh`) gerisini yapar. Düzen: MyTinyURL
(`/mnt/veri/yazilim/MyTinyURL/tool/`) — kimlik sunucuda kalır, derleme önce **dahili** kanala gider, sonra terfi.
Yeni oyun Play'e **ancak açıkça istenince** yüklenir (kullanıcı kararı, 2026-11-05).

## Kullanıcının bir kez yaptığı (Play Console)
1. Uygulamayı oluştur (paket adı = oyunun `yayin.env` içindeki `PAKET`, `tulpar.toml` `package` ile aynı).
2. Hizmet hesabına bu uygulama için yetki ver: `play-publisher@dogwood-envoy-308610.iam.gserviceaccount.com`
   (sürüm yönetimi + mağaza girişi). Anahtar **sunucuda** (`/root/.config/play-publisher/key.json`), hiçbir depoya girmez.
3. Uygulama içeriği formlarını doldur: gizlilik politikası adresi, reklam beyanı, içerik derecelendirmesi,
   hedef kitle, veri güvenliği.
4. Yükleme anahtarını **sen** üret ve şifresini kendin gir (`./anahtar_olustur.sh`); ajan anahtar/şifre üretmez, saklamaz.
5. Yeni kişisel hesapta: kapalı test (≥12 test kullanıcı, 14 gün) sonrası üretim başvurusu.

## Her sürümde (araç yapar)
```
AAB=1 SURUM_KODU=<n> SURUM_ADI=1.0.x ./apk_derle.sh          # imzalı AAB (+ semboller)
<motor>/tools/yayin/play_sunucu.sh --kuru yukle <notlar.md>   # önce kuru: ne gidecek, nereye
<motor>/tools/yayin/play_sunucu.sh yukle <notlar.md>          # dahili kanal
<motor>/tools/yayin/play_sunucu.sh kapali | terfi | tamamla   # kapalı test / üretim / aşamalı dağıtımı bitir
```
Önce `yayin.env` (oyun deposunun kökü) doğru mu: `PAKET`, `DOSYA`; mağaza dosyaları için `MAGAZA`, `EKRAN`.
Sunucu adresi `TULPAR_PLAY_SUNUCU` ile değişir (varsayılan iurl sunucusu).
