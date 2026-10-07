#!/usr/bin/env python3
"""eng_* ailesinin TEK kaynagi: SPEC tablosu -> motorun Tulpar YEREL EKLENTISI.

TulparLang derleyicisi motoru ADIYLA tanimiyor (2026-09-20'den beri) ve
2026-10-02'den (TulparLang K303) beri tanimasi da gerekmiyor: derleyicinin
genel "yerel eklenti" noktasi bir bildirim (tulpar-ext.json) okuyup her
fonksiyonu bildirilen C tipleriyle DOGRUDAN cagiriyor. Motor o bildirimi
buradan uretiyor:

  tulpar/generated/tulpar-ext.json      eklenti bildirimi: 216 fonksiyon (ad, C
                                        sembolu, parametre/donus tipi, belge),
                                        modul (engine -> engine.tpr), platforma
                                        gore link kitapliklari (linux, macos,
                                        windows, android)
  bridge/tulpar_ext_abi.inc             ABI KILIDI: her satir bir imza; derleme
                                        bridge/tulpar_abi.cpp'de onu teng_*'in
                                        GERCEK bildirimine atar — tip kayarsa
                                        motor DERLENMEZ, sembol yoksa testler
                                        LINKLENMEZ (derleyici C tarafini
                                        goremez; bu kilit gorur)

Derleme (CMake) bunlari yapi/tulpar-ext/ altinda bir PAKETE koyar: bildirim +
engine.tpr + lib/libengine_*.a. Kurulu `tulpar`:
  tulpar --ext yapi/tulpar-ext oyun.tpr      (ya da TULPAR_EXT_PATH /
                                             tulpar/tulpar.toml [ext] paths)

`--denetle`: depodaki uretilmis dosyalar bu tablonun SIMDIKI ciktisiyla bayt
bayt ayni mi (CMake on kosulu: bayat = derleme hatasi). `--oz-sinama`:
denetimin kendisinin bir farki yakaladigini gosterir (pozitif kontrol).

Eskiden (motor_derleyici.sh donemi) bu betik dort dosya uretip bir TulparLang
kopyasina kuruyordu: VMValue bindingleri + derleyici tablolari. O yol
derleyicinin ic ABI'sine (VMValue, ObjString yerlesimi) baglanmisti ve iki
gunde iki kez kirildi; K303 ile kalkti. Bkz. docs/KOPRU.md.

Parametre tipleri (SPEC): num (int ya da float -> C double), int (C int),
color (0xRRGGBBAA -> C int64_t), str (const char *), flag (bool ya da int ->
C int). Donus: void, bool (C int), int (C int), float (C double), str.
"""
import json
import os
import sys

# (ad, donus, [(param, tip), ...], aciklama)
SPEC = [
    # yasam dongusu
    ("eng_init", "bool", [("title", "str"), ("w", "int"), ("h", "int")], "Motoru kurar: pencere (ya da TULPAR_ENGINE_HEADLESS=N ile pencersiz N kare), Vulkan, fizik. Basarisizsa false (eng_last_error)."),
    ("eng_running", "bool", [], "Oyun dongusu surmeli mi (pencere kapanmadi, eng_close cagrilmadi, headless kare siniri dolmadi)."),
    ("eng_close", "void", [], "Dongunun bir sonraki kontrolde bitmesini ister."),
    ("eng_shutdown", "void", [], "Motoru kapatir; hata varsa log halkasini doker."),
    ("eng_frame_begin", "bool", [], "Kareyi baslatir: girdi, zaman. false = pencere yok (arka plan), yine de eng_frame_end cagir."),
    ("eng_frame_end", "void", [], "Kareyi bitirir: fizik (sabit adim), cizim, sunum."),
    ("eng_dt", "float", [], "Son kare suresi (s)."),
    ("eng_time", "float", [], "Kurulumdan beri gecen sure (s)."),
    ("eng_frame", "int", [], "Kare sayaci."),
    ("eng_fps", "float", [], "Kare hizi (p50, 120 karede bir guncellenir)."),
    ("eng_physics_pause", "void", [("paused", "flag")], "Fizigi duraklat/surdur: durakliyken sim adimi yok (govde, karakter, sahne parcacigi durur; carpisma/tetik olayi gelmez). Devam edince kalinan yerden surer, sonuc duraklamasiz kosuyla bit-tam ayni. Oyun mantigi, cizim, arayuz calisir."),
    ("eng_physics_paused", "bool", [], "Fizik durakli mi."),
    ("eng_time_scale", "void", [("scale", "num")], "Sim zaman olcegi 0..4 (0.5 agir cekim, 0 donmus). Kare suresine uygulanir, adima degil: belirlenim bozulmaz."),
    ("eng_time_scale_get", "float", [], "Simdiki zaman olcegi."),
    ("eng_sim_tick", "int", [], "Atilan sabit sim adimi sayisi (durakliyken artmaz)."),
    ("eng_width", "int", [], "Gorunen genislik (piksel)."),
    ("eng_height", "int", [], "Gorunen yukseklik (piksel)."),
    ("eng_headless", "bool", [], "Pencersiz kipte mi."),
    ("eng_set_headless", "void", [("frames", "int"), ("out_ppm", "str")], "eng_init'ten once: pencersiz N kare, son kare PPM dosyasina."),
    ("eng_log", "void", [("msg", "str")], "Motor log akisina [tpr] onekiyle satir yazar (logcat/stdout + halka)."),
    ("eng_log_level", "void", [("level", "int")], "Log seviyesi: 0 sessiz, 1 bilgi, 2 ayrinti, 3 iz (her cagri)."),
    ("eng_screenshot", "bool", [("path", "str")], "Son kareyi PPM olarak yazar (yalniz headless)."),
    ("eng_gpu_name", "str", [], "Vulkan cihazinin adi."),
    ("eng_last_error", "str", [], "Son kurulum hatasi metni."),
    ("eng_error_count", "int", [], "Motorun logladigi HATA sayisi (gecersiz id, kare disi cizim, kaynak yuklenemedi...). Oyun sonunda 0 bekle."),
    ("eng_warning_count", "int", [], "UYARI sayisi (pencere yok -> headless, font bulunamadi...)."),
    # dunya / kamera
    ("eng_bloom", "void", [("enable", "flag"), ("threshold", "num"), ("intensity", "num")], "Parlama (bloom): eng_init'ten ONCE acilir (ic HDR hedefi kurulur). Esik DOGRUSAL parlaklik (1.0 = yalniz cok parlak yerler), yogunluk 0 = kapali. Kare icinde esik/yogunluk degistirilebilir."),
    ("eng_bloom_on", "bool", [], "Parlama acik mi (HDR bicimi yoksa motor kapatir; sebep logda)."),
    ("eng_render_scale", "float", [("scale", "num")], "Ic cozunurluk olcegi (dinamik cozunurluk, 0.5..1.0): sahne olcekli cizilir, birlestirme tam ekrana buyutur; arayuz tam cozunurlukte kalir. Ic hedef ister (eng_bloom ile parlama acik). Donus: uygulanan olcek (ic hedef yoksa 1.0 + UYARI). eng_init sonrasi, kare icinde ya da disinda."),
    ("eng_gravity", "void", [("gx", "num"), ("gy", "num"), ("gz", "num")], "Yercekimi (eng_init'ten once)."),
    ("eng_sun", "void", [("dx", "num"), ("dy", "num"), ("dz", "num"), ("diffuse", "num")], "Gunes yonu (normalize edilir) ve siddeti."),
    ("eng_ambient", "void", [("color", "color")], "Ortam isigi rengi (0xRRGGBBAA)."),
    ("eng_shadow_volume", "void", [("cx", "num"), ("cy", "num"), ("cz", "num"), ("radius", "num"), ("depth", "num")], "Golge haritasinin kapsadigi hacim (merkez, yaricap, derinlik)."),
    ("eng_camera", "void", [("ex", "num"), ("ey", "num"), ("ez", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Kamera: goz ve hedef."),
    ("eng_camera_orbit", "void", [("tx", "num"), ("ty", "num"), ("tz", "num"), ("yaw", "num"), ("pitch", "num"), ("radius", "num")], "Yorunge kamerasi: hedef, yaw/pitch (radyan), uzaklik."),
    ("eng_camera_x", "float", [], "Kamera gozu x."),
    ("eng_camera_y", "float", [], "Kamera gozu y."),
    ("eng_camera_z", "float", [], "Kamera gozu z."),
    # sahne blob
    ("eng_scene_load", "bool", [("path", "str")], "Derlenmis sahneyi (.sahneb, engine_sahnec) yukler: modeller, isiklar, govdeler, dunya ayarlari."),
    ("eng_scene_count", "int", [], "Sahne varligi sayisi."),
    ("eng_scene_find", "int", [("name", "str")], "Sahne varliginin dizini, yoksa -1."),
    ("eng_scene_x", "float", [("i", "int")], "Sahne varliginin x'i (dinamikse sim'den)."),
    ("eng_scene_y", "float", [("i", "int")], "Sahne varliginin y'si."),
    ("eng_scene_z", "float", [("i", "int")], "Sahne varliginin z'si."),
    ("eng_scene_name", "str", [("i", "int")], "Sahne varliginin adi."),
    ("eng_scene_script", "str", [("i", "int")],
     "Varliga editorde atanmis Tulpar betiginin (.tpr) yolu; betik bileseni yoksa bos metin. Motor bu betigi CALISTIRMAZ -- yol bir ETIKETTIR, oyun kendi dongusunde okuyup ada gore dallanir (kopru ABI'si duz skaler, callback yok)."),
    ("eng_scene_script_enabled", "bool", [("i", "int")],
     "Varligin betik bileseni ETKIN mi (editordeki 'Etkin' kutusu). Bileseni olmayan varlikta false. Ayri erisimci, cunku 'atanmamis' ile 'atanmis ama kapali' AYRI olgular."),
    # nesne ozellikleri (E4): editorde varliga verilen, betigin varsayilaninin USTUNE yazilmis degerler.
    # Ustune yazilmamis ozellik HATA DEGIL (def doner); sinir disi indeks, ad kurali ve tur uyusmazligi HATA.
    # Dogum aninda (<ad>_baslat) oku, kare icinde degil.
    ("eng_scene_prop_num", "float", [("i", "int"), ("name", "str"), ("def", "num")],
     "Sahne varliginin `sayi` ozelligi (editorde ustune yazilan deger); yazilmamissa def DEGISMEDEN doner (hata degil). Tur uyusmazligi, sinir disi indeks, 23 karakteri asan ya da a-z 0-9 _ disi ad HATA sayar ve def doner. Dogum aninda (baslat) oku."),
    ("eng_scene_prop_int", "int", [("i", "int"), ("name", "str"), ("def", "int")],
     "Sahne varliginin `tam` ozelligi (|v| <= 2^24); yazilmamissa def. Hata kurallari eng_scene_prop_num ile ayni."),
    ("eng_scene_prop_flag", "bool", [("i", "int"), ("name", "str"), ("def", "flag")],
     "Sahne varliginin `bayrak` ozelligi (evet/hayir); yazilmamissa def. Hata kurallari eng_scene_prop_num ile ayni."),
    ("eng_scene_prop_point", "bool", [("i", "int"), ("name", "str"), ("lx", "num"), ("ly", "num"), ("lz", "num")],
     "Sahne varliginin `nokta` ozelligini DUNYA konumu olarak hesaplar; sonuc eng_scene_prop_px/py/pz. (lx,ly,lz) varsayilan YEREL ofset. true = deger sahneden (ustune yazilmis), false = varsayilan; ikisi de ayni kuralla dunyaya cevrilir (varligin yazar konumu + donusu, olcek yok), yani ayni ofset ayni noktayi verir. Sinir disi indekste (lx,ly,lz) oldugu gibi."),
    ("eng_scene_prop_px", "float", [], "Son eng_scene_prop_point sonucunun x'i (dunya)."),
    ("eng_scene_prop_py", "float", [], "Son eng_scene_prop_point sonucunun y'si (dunya)."),
    ("eng_scene_prop_pz", "float", [], "Son eng_scene_prop_point sonucunun z'si (dunya)."),
    ("eng_scene_prop_has", "bool", [("i", "int"), ("name", "str")],
     "Sahne varliginda bu ozellik editorde ustune yazilmis mi (herhangi tur). Yoksa false (hata degil)."),
    ("eng_scene_unload", "bool", [], "Sahneyi bosaltir: govdeler fizikten cikar, cizim durur. Sonra eng_scene_load yeniden cagrilabilir (bolum gecisi)."),
    ("eng_scene_loaded", "bool", [], "Su an yuklu bir sahne var mi."),
    ("eng_scene_vx", "float", [("i", "int")], "Sahne varliginin hizi x (govdesizse 0)."),
    ("eng_scene_vy", "float", [("i", "int")], "Sahne varliginin hizi y."),
    ("eng_scene_vz", "float", [("i", "int")], "Sahne varliginin hizi z."),
    ("eng_scene_is_dynamic", "bool", [("i", "int")], "Sahne varliginin govdesi dinamik mi (kuvvet uygulanabilir mi)."),
    ("eng_scene_set_velocity", "void", [("i", "int"), ("vx", "num"), ("vy", "num"), ("vz", "num")], "Sahne govdesinin hizi (dinamik degilse hata loglanir, cagri yok sayilir)."),
    ("eng_scene_impulse", "void", [("i", "int"), ("ix", "num"), ("iy", "num"), ("iz", "num")], "Sahne govdesinin hizina ekler (dinamik degilse hata loglanir)."),
    ("eng_scene_is_character", "bool", [("i", "int")], "Sahne varligi karakter mi (Karakter Kontrolcusu bileseni + dogdu)."),
    ("eng_scene_character_move", "void", [("i", "int"), ("vx", "num"), ("vz", "num"), ("jump", "flag")], "Sahne karakterine yatay hiz istegi (KALICI; durmak icin 0,0) + zipla (kenar-tetikli)."),
    ("eng_scene_character_grounded", "bool", [("i", "int")], "Sahne karakteri zeminde mi."),
    ("eng_scene_character_set_jump", "void", [("i", "int"), ("speed", "num")], "Sahne karakterinin ziplama hizi (m/s)."),
    # varliklar
    ("eng_spawn_box", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("hx", "num"), ("hy", "num"), ("hz", "num"), ("dynamic", "flag"), ("color", "color")], "Kutu (yarim kenarlar) + fizik govdesi. Donus: varlik id (0 hata)."),
    ("eng_spawn_sphere", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("dynamic", "flag"), ("color", "color")], "Kure + fizik govdesi. Donus: varlik id."),
    ("eng_spawn_ground", "int", [("half_size", "num"), ("color", "color")], "Dama dokulu zemin (ust yuz y=0) + sabit govde. Donus: varlik id."),
    ("eng_spawn_trigger_box", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("hx", "num"), ("hy", "num"), ("hz", "num")], "Tetik (bolge) kutusu: GORUNMEZ, carpisma tepkisi yok; icine giren/cikan govdeler eng_trigger_* kuyrugunda. eng_set_pos tasir. Donus: varlik id."),
    ("eng_spawn_trigger_sphere", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num")], "Tetik (bolge) kuresi. Donus: varlik id."),
    ("eng_spawn_character", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("height", "num"), ("color", "color")], "Karakter denetleyicisi (sanal kapsul): rampada kaymaz, basamak cikar, zemine yapisir. Konum AYAK tabani; boy > 2*yaricap. Donus: varlik id."),
    ("eng_character_move", "void", [("id", "int"), ("vx", "num"), ("vz", "num"), ("jump", "flag")], "Karaktere yatay hiz istegi (KALICI; durmak icin 0,0) + zipla (kenar-tetikli). Dikey hiz motorun."),
    ("eng_character_grounded", "bool", [("id", "int")], "Karakter zeminde mi (yurunebilir yuzeyde)."),
    ("eng_character_ground_state", "int", [("id", "int")], "Zemin durumu: 0 zeminde, 1 dik yamac (kayar), 2 desteksiz, 3 havada."),
    ("eng_character_set_jump", "void", [("id", "int"), ("speed", "num")], "Ziplama hizi (m/s, varsayilan 4.0)."),
    ("eng_load_model", "int", [("path", "str")], "glTF modeli yukler. Donus: model tutamaci, -1 hata."),
    ("eng_spawn_model", "int", [("asset", "int"), ("x", "num"), ("y", "num"), ("z", "num"), ("scale", "num"), ("tint", "color")], "Model varligi (govdesiz). Donus: varlik id."),
    ("eng_spawn_light", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("color", "color"), ("intensity", "num"), ("radius", "num")], "Nokta isik. Donus: varlik id."),
    ("eng_despawn", "void", [("id", "int")], "Varligi (ve govdesini) siler."),
    ("eng_alive", "bool", [("id", "int")], "Id hala canli mi (nesil dahil)."),
    ("eng_count", "int", [], "Canli varlik sayisi."),
    ("eng_x", "float", [("id", "int")], "Varlik x (dinamikse sim'den)."),
    ("eng_y", "float", [("id", "int")], "Varlik y."),
    ("eng_z", "float", [("id", "int")], "Varlik z."),
    ("eng_set_pos", "void", [("id", "int"), ("x", "num"), ("y", "num"), ("z", "num")], "Isinlar; dinamik govde yeniden kurulur (hiz sifir)."),
    ("eng_set_color", "void", [("id", "int"), ("color", "color")], "Varlik rengi."),
    ("eng_set_scale", "void", [("id", "int"), ("scale", "num")], "Gorsel olcek (fizik govdesi degismez)."),
    ("eng_set_yaw", "void", [("id", "int"), ("yaw_deg", "num")], "Y ekseni donusu (derece); dinamik govdede sim ezer."),
    ("eng_vx", "float", [("id", "int")], "Hiz x."),
    ("eng_vy", "float", [("id", "int")], "Hiz y."),
    ("eng_vz", "float", [("id", "int")], "Hiz z."),
    ("eng_set_velocity", "void", [("id", "int"), ("vx", "num"), ("vy", "num"), ("vz", "num")], "Dinamik govdenin hizi."),
    ("eng_impulse", "void", [("id", "int"), ("ix", "num"), ("iy", "num"), ("iz", "num")], "Hiza ekler (ziplama, itme)."),
    ("eng_is_dynamic", "bool", [("id", "int")], "Dinamik govde mi."),
    ("eng_awake", "bool", [("id", "int")], "Govde uyanik mi (hareket ediyor)."),
    # kodla uretilen varliga betik baglama (sahnedeki `betik "x.tpr"` atamasinin kod ikizi)
    ("eng_script_attach", "bool", [("id", "int"), ("name", "str")],
     "Kopru varligina davranis betigi BAGLAR: \"davranis/dusman.tpr\" ya da \"dusman\" -> <taban>_baslat(id) hemen, <taban>_guncelle(id, dt) her kare (yuva sirasi, fizikten sonra), _carpisma(id, diger, olay, x, y, z, hiz, diger_sahne), _tetik_girdi/_cikti(id, diger, diger_sahne), _bolge_girdi/_cikti(id, bolge, bolge_sahne), _bitir(id). `id` ve `diger` KOPRU id'si (0 = kopru varligi degil), son arguman SAHNE dizini (-1). Ikinci baglama oncekinin yerini alir (once onun _bitir'i). Hic kancasi bulunamayan ad, olu id ya da dolu havuz: HATA + false, onceki baglanti degismez."),
    ("eng_script_detach", "bool", [("id", "int")], "Bagli betigi cozer: once baglanti kalkar, sonra <taban>_bitir(id) (varlik hala canli). false = bagli betik yoktu (hata degil). eng_despawn ve kapanis da _bitir'i kendisi cagirir."),
    ("eng_script_name", "str", [("id", "int")], "Varliga bagli betigin taban adi (\"dusman\"); betik yoksa ya da id 0 ise bos metin (hata degil; kanca icinde betik_adi(diger) guvenli). Olu id HATA."),
    ("eng_script_count", "int", [], "Betik bagli kopru varligi sayisi (havuz tavani 512)."),
    # model animasyonu
    ("eng_model_clip_count", "int", [("asset", "int")], "Modelin animasyon klip sayisi (glTF animations)."),
    ("eng_model_clip_duration", "float", [("asset", "int"), ("clip", "int")], "Klibin suresi (s)."),
    ("eng_model_clip_name", "str", [("asset", "int"), ("clip", "int")], "Klibin adi."),
    ("eng_set_anim", "void", [("id", "int"), ("clip", "int"), ("speed", "num"), ("loop", "flag")], "Model varligina klip atar; her kare poz degerlendirilir. clip < 0: animasyonu kapat (statik)."),
    ("eng_anim_time", "float", [("id", "int")], "Varligin klip icindeki ani (s)."),
    ("eng_anim_done", "bool", [("id", "int")], "Dongusuz klip bitti mi."),
    # girdi
    ("eng_key_down", "bool", [("name", "str")], "Tus basili mi: \"W\",\"A\",\"SPACE\",\"UP\",\"ESC\",\"ENTER\",\"SHIFT\",\"0\"..\"9\"."),
    ("eng_key_pressed", "bool", [("name", "str")], "Tus bu karede basildi mi."),
    ("eng_touch_count", "int", [], "Ekrandaki parmak sayisi (masaustunde 0; TULPAR_ENGINE_DOKUNMATIK=1 ile fare = parmak 0)."),
    ("eng_touch_x", "float", [("i", "int")], "i. parmagin x'i (piksel)."),
    ("eng_touch_y", "float", [("i", "int")], "i. parmagin y'si (piksel)."),
    ("eng_stick_x", "float", [], "Sanal joystick x (-1..1; sol yarim ekran surukleme)."),
    ("eng_stick_y", "float", [], "Sanal joystick y (-1..1; ileri = +)."),
    ("eng_stick_action", "bool", [], "Sag yarim ekrana kisa dokunus (bu karede)."),
    ("eng_look_dx", "float", [], "Bakis, piksel/kare: sag fare tusuyla surukleme + (dokunmatik) sag yarim surukleme."),
    ("eng_mouse_x", "float", [], "Fare x (masaustu)."),
    ("eng_mouse_y", "float", [], "Fare y (masaustu)."),
    ("eng_mouse_down", "bool", [("button", "int")], "Fare tusu basili mi (0 sol, 1 sag, 2 orta)."),
    # HUD
    ("eng_text", "void", [("s", "str"), ("x", "num"), ("y", "num"), ("scale", "num"), ("color", "color")], "Ekrana metin (kare icinde cagir; font yoksa sessizce atlanir, logda uyari)."),
    ("eng_rect", "void", [("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("color", "color")], "Ekrana duz dikdortgen (kare icinde cagir)."),
    ("eng_text_width", "float", [("s", "str"), ("scale", "num")], "Metnin piksel genisligi."),
    # ses
    ("eng_audio_open", "bool", [("rate", "int"), ("channels", "int")], "Ses cihazini acar (0,0 = varsayilan 48 kHz stereo). Acilamazsa false: oyun sessiz devam eder, hata loglanir."),
    ("eng_audio_close", "void", [], "Ses cihazini kapatir (calan sesler durur)."),
    ("eng_audio_ok", "bool", [], "Ses cihazi acik mi."),
    ("eng_audio_backend", "str", [], "Ses arka ucu ve cihaz adi (\"pulseaudio 'Speakers' 48000 Hz ...\"); kapaliysa bos."),
    ("eng_audio_load", "int", [("path", "str")], "Ses dosyasi yukler (WAV/FLAC/MP3) -> klip tutamaci, -1 hata. Ayni yol tek kez yuklenir."),
    ("eng_audio_tone", "int", [("hz", "num"), ("seconds", "num")], "Sentetik sinus klibi uretir -> klip tutamaci. Ayni (frekans, sure) ayni tutamaci verir."),
    ("eng_audio_play", "int", [("clip", "int"), ("gain", "num"), ("loop", "flag")], "Klibi calar. Donus: ses id'si (0 hata). Ses seviyesi ana seviyeyle carpilir."),
    ("eng_audio_beep", "int", [("hz", "num"), ("seconds", "num"), ("gain", "num")], "Ton uret + cal (tek cagri). Donus: ses id'si."),
    ("eng_audio_stop", "void", [("voice", "int")], "Calan sesi durdurur."),
    ("eng_audio_stop_all", "void", [], "Butun sesleri durdurur."),
    ("eng_audio_master", "void", [("gain", "num")], "Ana ses seviyesi (0..4); calan sesler de guncellenir."),
    ("eng_audio_playing", "int", [], "Su an calan ses sayisi (cihaz thread'inin olctugu deger)."),
    ("eng_audio_peak", "float", [], "Son ses blogundaki en buyuk ornek (0 = sessiz); ses gercekten uretiliyor mu olcusu."),
    # sorgular: isin testi + yakinlik (savas/yapay zeka; callback FFI olmadigi icin OLAY degil SORGU)
    ("eng_raycast", "float", [("ox", "num"), ("oy", "num"), ("oz", "num"), ("dx", "num"), ("dy", "num"), ("dz", "num"), ("max_dist", "num"), ("skip_id", "int")], "Isin testi: (ox,oy,oz)'dan (dx,dy,dz) yonune en cok max_dist. Donus: carpma mesafesi, -1 iska. skip_id kendi govdesini atlar (0 = atlama yok). Govde eklendikten sonra en az bir kare gerekir (genis faz)."),
    ("eng_ray_x", "float", [], "Son isin testinin carpma noktasi x."),
    ("eng_ray_y", "float", [], "Son isin testinin carpma noktasi y."),
    ("eng_ray_z", "float", [], "Son isin testinin carpma noktasi z."),
    ("eng_ray_nx", "float", [], "Son carpmanin yuzey normali x (duvar boyunca kayma)."),
    ("eng_ray_ny", "float", [], "Son carpmanin yuzey normali y."),
    ("eng_ray_nz", "float", [], "Son carpmanin yuzey normali z."),
    ("eng_ray_id", "int", [], "Son isinin carptigi KOPRU varliginin id'si (0 = kopru varligi degil)."),
    ("eng_ray_scene", "int", [], "Son isinin carptigi SAHNE varliginin dizini (-1 = sahne govdesi degil)."),
    ("eng_overlap", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("skip_id", "int")], "Kure sorgusu: merkeze radius icindeki kopru varliklari, YAKINDAN UZAGA sirali. Donus: sonuc sayisi (eng_overlap_id/eng_overlap_dist ile okunur). Zemin ve isik disarida."),
    ("eng_overlap_id", "int", [("i", "int")], "Son kure sorgusunun i. sonucunun varlik id'si."),
    ("eng_overlap_dist", "float", [("i", "int")], "Son kure sorgusunun i. sonucunun merkeze uzakligi."),
    ("eng_nearest", "int", [("x", "num"), ("y", "num"), ("z", "num"), ("radius", "num"), ("skip_id", "int")], "Merkeze radius icindeki EN YAKIN kopru varligi (0 = yok). eng_overlap ile ayni olcut."),
    # --- Carpisma olaylari: callback FFI yok, o yuzden KUYRUK -------------
    ("eng_collision_count", "int", [], "Bu karede olusan carpisma olayi sayisi. Halka her kare basinda temizlenir; bir karede birden cok fizik adimi atilsa da olaylar birikir."),
    ("eng_collision_dropped", "int", [], "Halkaya sigmayip DUSEN olay sayisi. 0 degilse kapasite yetmiyor: olaylar sessizce kirpilmiyor, bu sayac goruyor."),
    ("eng_collision_a", "int", [("i", "int")], "i. carpismanin A tarafi: kopru varlik id'si (0 = kopru varligi degil, ornegin zemin ya da sahne govdesi)."),
    ("eng_collision_b", "int", [("i", "int")], "i. carpismanin B tarafi."),
    ("eng_collision_scene_a", "int", [("i", "int")], "i. carpismanin A tarafinin SAHNE varlik dizini (-1 = sahne govdesi degil)."),
    ("eng_collision_scene_b", "int", [("i", "int")], "i. carpismanin B tarafinin sahne varlik dizini."),
    ("eng_collision_x", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya x)."),
    ("eng_collision_y", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya y)."),
    ("eng_collision_z", "float", [("i", "int")], "i. carpismanin temas noktasi (dunya z)."),
    ("eng_collision_nx", "float", [("i", "int")], "i. carpismanin yuzey normali (A'dan B'ye) x."),
    ("eng_collision_ny", "float", [("i", "int")], "i. carpismanin yuzey normali y."),
    ("eng_collision_nz", "float", [("i", "int")], "i. carpismanin yuzey normali z."),
    ("eng_collision_speed", "float", [("i", "int")], "i. carpismanin SIDDETI: temas noktasinda normal boyu goreli hiz (m/s). Sert/yumusak carpma ayrimi bununla yapilir."),
    # tetik olaylari (kuyruk; belirlenimli sira)
    ("eng_trigger_count", "int", [], "Bu karenin tetik giris/cikis olayi sayisi (kopru + sahne tetikleri). Sira belirlenimli."),
    ("eng_trigger_dropped", "int", [], "Halkaya sigmayip DUSEN tetik olayi. 0 degilse olay kaybolmus."),
    ("eng_trigger_zone", "int", [("i", "int")], "i. olayin BOLGESI: kopru varlik id'si (0 = sahne tetigi)."),
    ("eng_trigger_zone_scene", "int", [("i", "int")], "i. olayin bolgesinin SAHNE dizini (-1 = kopru tetigi)."),
    ("eng_trigger_other", "int", [("i", "int")], "i. olayda giren/cikan: kopru varlik id'si (0 = kopru varligi degil)."),
    ("eng_trigger_other_scene", "int", [("i", "int")], "i. olayda giren/cikanin sahne dizini (-1 = sahne varligi degil)."),
    ("eng_trigger_entered", "bool", [("i", "int")], "i. olay giris mi (false = cikis)."),
    # navmesh: bake engine_sahnec'te (sahnedeki sabit kutu govdeler), runtime yalniz sorgular
    ("eng_nav_ok", "bool", [], "Yuklu sahnede bake edilmis navmesh var mi (yoksa oyun duz yola duser)."),
    ("eng_nav_polys", "int", [], "Navmesh poligon sayisi."),
    ("eng_nav_path", "int", [("fx", "num"), ("fy", "num"), ("fz", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Navmesh uzerinde duz yol: nokta sayisi (0 = yol yok). Noktalar eng_nav_x/y/z ile okunur."),
    ("eng_nav_partial", "bool", [], "Son yol kismi mi (hedefe ulasilamadi, en yakin noktaya kadar)."),
    ("eng_nav_x", "float", [("i", "int")], "Son yolun i. noktasinin x'i."),
    ("eng_nav_y", "float", [("i", "int")], "Son yolun i. noktasinin y'si."),
    ("eng_nav_z", "float", [("i", "int")], "Son yolun i. noktasinin z'si."),
    ("eng_nav_nearest", "bool", [("x", "num"), ("y", "num"), ("z", "num")], "Navmesh'e en yakin noktayi bulur (konum mesh'in disindaysa yol aramadan once buna yapistir). Sonuc eng_nav_near_x/y/z."),
    ("eng_nav_near_x", "float", [], "Son eng_nav_nearest sonucunun x'i."),
    ("eng_nav_near_y", "float", [], "Son eng_nav_nearest sonucunun y'si."),
    ("eng_nav_near_z", "float", [], "Son eng_nav_nearest sonucunun z'si."),
    ("eng_nav_raycast", "bool", [("fx", "num"), ("fy", "num"), ("fz", "num"), ("tx", "num"), ("ty", "num"), ("tz", "num")], "Navmesh uzerinde dogru gorus: true = ENGEL VAR (yol ara), false = dumduz gidilebilir. Yol aramadan cok daha ucuz."),
    ("eng_nav_ray_t", "float", [], "Son eng_nav_raycast'in carpma parametresi (0..1; engel yoksa 1)."),
    # anlik-kip (immediate mode) arayuz: menu / ayar / duraklat. Motorda yeni cizim
    # yolu yok: eng_rect + eng_text uzerine kurulu. Deger BETIKTE yasar (Tulpar'da
    # cikti parametresi yok): onay kutusu/kaydirici YENI degeri dondurur.
    ("eng_ui_begin", "void", [], "Arayuz karesini baslatir: isaretciyi (enjeksiyon > dokunmatik > fare) ornekler, basma/birakma kenarlarini hesaplar. Kare icinde, widget'lardan ONCE."),
    ("eng_ui_end", "void", [], "Arayuz karesini bitirir: isaretci kalktiysa surukleme birakilir."),
    ("eng_ui_theme", "void", [("panel", "color"), ("text", "color"), ("accent", "color")], "Arayuz renkleri: panel zemini, yazi, vurgu. Dugme/iz tonlari panelden turetilir."),
    ("eng_ui_enable", "void", [("on", "flag")], "Bundan sonraki widget'lar etkin mi. Kapaliyken widget SOLUK cizilir ve degeri DEGISMEZ (tiklama yutulur)."),
    ("eng_ui_panel", "void", [("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("color", "color")], "Menu/HUD zemini (renk 0 = tema paneli). eng_ui_begin gerektirmez."),
    ("eng_ui_label", "void", [("s", "str"), ("x", "num"), ("y", "num"), ("scale", "num"), ("color", "color")], "Arayuz metni (renk 0 = tema yazi rengi). eng_ui_begin gerektirmez."),
    ("eng_ui_button", "bool", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num")], "Dugme: bu karede tiklandiysa true (basma VE birakma dugmenin icinde). Kimlik etiketten turer."),
    ("eng_ui_checkbox", "bool", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("value", "flag")], "Onay kutusu. Donus YENI degerdir (degisen yoksa gelen deger) — betik geri yazar."),
    ("eng_ui_slider", "float", [("label", "str"), ("x", "num"), ("y", "num"), ("w", "num"), ("h", "num"), ("value", "num"), ("min_v", "num"), ("max_v", "num")], "Kaydirici. Donus YENI degerdir (surukleme sirasinda her kare gunceller); aralik gecersizse hata + gelen deger."),
    ("eng_ui_active", "bool", [], "Bir widget basili/surukleniyor mu (menu acikken oyun girdisini bastirmak icin)."),
    ("eng_ui_clicks", "int", [], "Toplam etkinlestirme sayisi: dugme tiklamasi + onay kutusu degisimi (kapi olcumu)."),
    ("eng_ui_test_click", "void", [("x", "num"), ("y", "num")], "Pencersiz dogrulama: (x,y) noktasina tek karelik bas+birak enjekte eder; sonraki eng_ui_begin tuketir. Widget disina tiklama tetiklemez."),
    # kalici kayit: ayarlar + en yuksek skor (motor kurulmadan da calisir)
    ("eng_save_open", "bool", [("path", "str")], "Kayit deposunu bu dosyaya baglar ve okur. true = dosya okundu, false = dosya yok (yeni kayit; hata degil). Varsayilan yol: TULPAR_ENGINE_SAVE ya da calisma dizininde tulpar_kayit.txt."),
    ("eng_save_path", "str", [], "Kayit dosyasinin yolu."),
    ("eng_save_set", "void", [("key", "str"), ("value", "num")], "Sayisal deger yazar (bellekte; diske eng_save_write ya da kapanis yazar)."),
    ("eng_save_get", "float", [("key", "str"), ("def", "num")], "Sayisal deger okur. Anahtar yoksa varsayilan doner (hata degil); deger sayisal degilse HATA loglanir ve varsayilan doner."),
    ("eng_save_set_str", "void", [("key", "str"), ("value", "str")], "Metin deger yazar (satir sonu ve '=' yasak)."),
    ("eng_save_get_str", "str", [("key", "str"), ("def", "str")], "Metin deger okur; anahtar yoksa varsayilan."),
    ("eng_save_has", "bool", [("key", "str")], "Anahtar var mi."),
    ("eng_save_write", "bool", [], "Kaydi diske yazar (gecici dosya + rename). Kapanista kirli kayit kendiliginden yazilir."),
    ("eng_save_clear", "void", [], "Butun anahtarlari dusurur (diske yazmak icin eng_save_write)."),
    ("eng_save_count", "int", [], "Kayittaki anahtar sayisi."),
    # sahne sicak yeniden yukleme: editorde "Derle" -> oyun kendini yeniler
    ("eng_scene_watch", "void", [("on", "flag")], "Yuklu .sahneb dosyasini izle: degisirse sahne bosaltilip yeniden yuklenir. Degisim gorulunce bir kontrol daha beklenir (yarim yazilmis blob yuklenmesin)."),
    ("eng_scene_reloaded", "bool", [], "Son okumadan beri sahne sicak yuklendi mi. BIR KEZ true doner (tuketilir): olay olarak kullanilir."),
    ("eng_scene_reload_count", "int", [], "Toplam sicak yukleme sayisi."),
    ("eng_scene_path", "str", [], "Yuklu (ya da izlenen) sahne dosyasinin yolu."),
    ("eng_file_copy", "bool", [("src", "str"), ("dst", "str")], "Ikili dosya kopyalar. .sahneb gibi NUL iceren bloblar Tulpar string'ine sigmaz; arac/test betikleri icin."),
    ("eng_file_mtime", "float", [("path", "str")], "Dosyanin degisim zamani (saniye, epoch); dosya yoksa 0."),
    # olcum
    ("eng_draw_count", "int", [], "Son karede cizim sayisi."),
    ("eng_body_count", "int", [], "Fizikteki govde sayisi."),
    ("eng_light_count", "int", [], "Son karede nokta isik sayisi."),
    ("eng_frame_ms", "float", [], "Son 120 karenin p50 suresi (ms)."),
    ("eng_rss_kb", "int", [], "Surecin yerlesik bellegi (RSS), KB: Linux/Android /proc/self/statm, macOS task_info, Windows GetProcessMemoryInfo. Olculemezse 0. Motor kurulmadan da calisir, kare icinde cagrilabilir (ayirma yok)."),
    ("eng_virtual_mb", "int", [], "Surecin sanal boyutu (MB). Motor kurulmadan da calisir; olculemezse 0. Oturum ac/kapa kacak kapisinin aleti (Tuzaklar 8ct)."),
    ("eng_thread_count", "int", [], "Surecin thread sayisi. Motor kurulmadan da calisir; olculemezse 0."),
    ("eng_vk_live", "int", [], "Canli havuzsuz Vulkan nesnesi (kurma - birakma; komut tamponu/descriptor set haric). Kurulu motorda anlik; kapanistan sonra son kapanista cihaz yikilmadan hemen onceki deger (oturumdan oturuma degismemeli). Olculmediyse -1."),
]

# SPEC tipi -> bildirim (tulpar-ext.json) tipi. Bildirim tipleri C'yi soyler:
# i32 = int, i64 = int64_t, f64 = double, bool = int (0/1), str = const char *.
EXT_PARAM = {"num": "f64", "int": "i32", "color": "i64", "str": "str", "flag": "bool"}
EXT_RET = {"void": "void", "bool": "bool", "int": "i32", "float": "f64", "str": "str"}
# Bildirim tipi -> C tipi (ABI kilidi bununla yazilir; teng_*'in gercek
# bildirimine atanir).
C_TYPE = {"i32": "int", "i64": "int64_t", "f64": "double", "bool": "int", "str": "const char *", "void": "void"}

# Tulpar'dan cagrilan sembol. Tek istisna eng_init: once betik VM'ini kuran
# yapistirici (bridge/tulpar_kopru.cpp) — kurulum eng_init'in ICINDE ki oyun
# onu unutamasin (unutulan kurulum = sessizce calismayan betikler).
SYMBOL_OVERRIDE = {"eng_init": "teng_tulpar_init"}

# Motor arsivleri: GNU ld tek gecis -> grup (arsivler birbirine capraz bagli).
# engine_tulpar (yapistirici) teng_*'i ve TulparLang runtime'inin tulpar_ext_*
# ABI'sini cagirir; derleyici -ltulpar_runtime'i eklentiden SONRA koyar.
DESKTOP_LIBS = ["engine_tulpar", "engine_bridge", "engine_content", "engine_renderer", "engine_sim",
                "engine_rhi", "engine_audio", "engine_core", "engine_platform", "engine_jolt",
                "engine_recast", "engine_meshopt", "engine_astcenc"]
# Windows (MSYS2 MINGW64): ayni arsivler, ayni grup (TulparLang'in Windows
# linki de GNU ld; surucu MINGW64 clang++, -static). Sistem kitapliklari
# CMake'in hedeflerine yazdiklarinin AYNISI: psapi (engine_platform PUBLIC,
# os_resident_bytes) + Threads::Threads. Gerisi (kernel32, user32, advapi32,
# shell32, msvcrt) suruculerin varsayilan listesinde. Vulkan ve GLFW link
# DEGIL, calisma zamaninda LoadLibrary (rhi/vk_api.cpp, platform/window.cpp);
# miniaudio da WASAPI/ole32'yi kendisi yukluyor. Olculdu (2026-10-02, CI
# MINGW64 GCC): motor ikililerinin ithalat tablosunda KERNEL32 + msvcrt +
# MinGW calisma zamani DLL'lerinden baska sistem DLL'i yok (engine_demo.exe).
# Ikisi de olculunce GEREKMEDI (CI 2026-10-02, kosum 37026937971: -lpsapi,
# -lpthread ve engine_rhi birlikte cikarildi, tanimsizlarin hepsi rhi::*):
# K32GetProcessMemoryInfo kernel32'de, winpthread -static zincirinde. CMake ile
# ayni kalsin ve PSAPI_VERSION=1 basliklarinda da linklensin diye duruyorlar.
WINDOWS_FLAGS = ["-lpsapi", "-lpthread"]
# Android: yapistirici + NativeActivity kabugu + kopru tek arsivde
# (tulpar_engine_android, CMakeLists.txt); tools/build_bridge_android.sh
# paketin android/<abi>/ dizinine koyar.
ANDROID_LIBS = ["tulpar_engine_android", "engine_content", "engine_renderer", "engine_sim", "engine_rhi",
                "engine_audio", "engine_core", "engine_platform", "engine_jolt", "engine_recast",
                "engine_meshopt", "engine_astcenc"]


def manifest_text():
    fns = []
    for name, ret, params, doc in SPEC:
        assert len(params) <= 16, name
        f = {"name": name}
        sym = SYMBOL_OVERRIDE.get(name, "t" + name)
        if sym != name:
            f["symbol"] = sym
        if params:
            f["params"] = [f"{p}: {EXT_PARAM[t]}" for p, t in params]
        if ret != "void":
            f["returns"] = EXT_RET[ret]
        f["doc"] = doc
        fns.append(f)
    # Elle okunur: ust duzey girintili, fonksiyon basina TEK satir (fark
    # okunsun diye).
    lines = ["{",
             '  "tulpar_ext": 1,',
             '  "name": "engine",',
             '  "description": "Tulpar Engine — mobil oncelikli C++ oyun motoru (teng_* duz skaler C ABI). '
             'URETILMIS: tools/gen_engine_bindings.py (SPEC); elle duzenleme.",',
             '  "modules": {"engine": "engine.tpr"},',
             '  "link": {',
             '    "linux": ' + json.dumps({"lib_dirs": ["lib"], "libs": DESKTOP_LIBS, "group": True,
                                            "flags": ["-lpthread"]}, ensure_ascii=False) + ",",
             '    "macos": ' + json.dumps({"lib_dirs": ["lib"], "libs": DESKTOP_LIBS, "flags": ["-lpthread"]},
                                           ensure_ascii=False) + ",",
             '    "windows": ' + json.dumps({"lib_dirs": ["lib"], "libs": DESKTOP_LIBS, "group": True,
                                              "flags": WINDOWS_FLAGS}, ensure_ascii=False) + ",",
             '    "android": ' + json.dumps({"lib_dirs": ["android/{abi}"], "libs": ANDROID_LIBS, "group": True},
                                             ensure_ascii=False),
             "  },",
             '  "functions": [']
    for i, f in enumerate(fns):
        lines.append("    " + json.dumps(f, ensure_ascii=False) + ("," if i + 1 < len(fns) else ""))
    lines.append("  ]")
    lines.append("}")
    return "\n".join(lines) + "\n"


def abi_text():
    out = ["// URETILMIS DOSYA — tools/gen_engine_bindings.py (SPEC). Elle duzenleme.",
           "//",
           "// ABI KILIDI: tulpar-ext.json'daki her fonksiyonun C imzasi. Satir bicimi:",
           "//   TENG_ABI(donus, tulpar_adi, c_sembolu, (parametre tipleri))",
           "// bridge/tulpar_abi.cpp her satiri teng_*'in GERCEK bildirimine (engine_api.h)",
           "// tipli bir isaretci olarak atar: bildirim ile C kaydiginda motor DERLENMEZ.",
           "// eng_init'in Tulpar sembolu yapistiricidir (teng_tulpar_init, ayni imza);",
           "// kilit alttaki teng_init'i baglar, yapistirici kendi basliginda kilitli.",
           f"#define TENG_ABI_COUNT {len(SPEC)}"]
    for name, ret, params, _doc in SPEC:
        ps = ", ".join(C_TYPE[EXT_PARAM[t]] for _p, t in params) or "void"
        out.append(f"TENG_ABI({C_TYPE[EXT_RET[ret]]}, {name}, t{name}, ({ps}))")
    return "\n".join(out) + "\n"


def outputs(root):
    gen = os.path.join(root, "tulpar", "generated")
    return [(os.path.join(gen, "tulpar-ext.json"), manifest_text()),
            (os.path.join(root, "bridge", "tulpar_ext_abi.inc"), abi_text())]


def check(outs):
    """Bayat dosyalarin listesi (bos = taze)."""
    stale = []
    for path, text in outs:
        try:
            with open(path, encoding="utf-8", newline="") as f:
                cur = f.read()
        except OSError:
            cur = None
        if cur != text:
            stale.append(path)
    return stale


def main():
    argv = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    mode = "yaz"
    rest = []
    for a in argv:
        if a in ("-h", "--help"):
            print(__doc__)
            return 0
        if a == "--denetle":
            mode = "denetle"
        elif a == "--oz-sinama":
            mode = "oz"
        elif a.startswith("-"):
            print(f"hata: bilinmeyen secenek {a} (--help)", file=sys.stderr)
            return 2
        else:
            rest.append(a)
    if rest:
        root = os.path.abspath(rest[0])

    names = [s[0] for s in SPEC]
    assert len(names) == len(set(names)), "yinelenen ad"
    outs = outputs(root)

    if mode == "oz":
        # Pozitif kontrol: dosyalarin dogru hali TAZE, tek bayti degismis hali
        # BAYAT gorunmeli. Ikisinden biri tutmazsa --denetle bir sey olcmuyor.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            fake = [(os.path.join(tmp, os.path.basename(p)), t) for p, t in outs]
            for p, t in fake:
                with open(p, "w", encoding="utf-8", newline="") as f:
                    f.write(t)
            if check(fake):
                print("baglama oz-sinama DUSTU: dogru dosyalar bayat goruldu", file=sys.stderr)
                return 1
            with open(fake[0][0], "w", encoding="utf-8", newline="") as f:
                f.write(fake[0][1].replace("eng_init", "eng_inix", 1))
            if not check(fake):
                print("baglama oz-sinama DUSTU: degistirilmis bildirim TAZE goruldu", file=sys.stderr)
                return 1
        print(f"baglama oz-sinama: denetim degisikligi yakaliyor ({len(SPEC)} fonksiyon)")
        return 0

    if mode == "denetle":
        stale = check(outs)
        if stale:
            for p in stale:
                print(f"BAYAT: {os.path.relpath(p, root)} — SPEC'in ciktisiyla ayni degil", file=sys.stderr)
            print("Duzeltme: python3 tools/gen_engine_bindings.py (uretilmis dosyalar depoya girer)", file=sys.stderr)
            return 1
        print(f"baglama denetimi: {len(outs)} uretilmis dosya SPEC ile ayni ({len(SPEC)} fonksiyon)")
        return 0

    for path, text in outs:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        print("yazildi:", os.path.relpath(path, root), f"({len(SPEC)} fonksiyon)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
