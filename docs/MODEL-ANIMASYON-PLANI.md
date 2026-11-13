# Model, poligon ve animasyon planı

2026-11-13 — kullanıcı isteği: "dışarıdan STL / glTF / GLB desteği, poligon (mesh), animasyonları halletmek".
Bu belge **plandır, kod yok**. Bugünkü durum `docs/OZELLIK-HARITASI.md` ve kaynaktan **ölçüldü** (satır numaraları değil, adlar).

## 1. Bugün ne var (ölçülen)

| alan | durum |
|---|---|
| glTF 2.0 / **GLB** yükleme | ✅ `content/gltf.cpp` (cgltf + stb_image): mesh, malzeme (PBR + ORM), doku, **iskelet + animasyon**; meshopt + ayrık LOD. Editör "Model içe aktar…" ve sürükle-bırak `.glb/.gltf` kabul eder (`app/editor_files.cpp`) |
| Animasyon çalma | 🟡 Varlık başına **tek klip** (`clip`, `phase`, `speed`; köprüde `eng_set_anim(id, klip, hız, döngü)`, `eng_model_clip_*`). Klip = sıkıştırılmış (sabit iz eleme, 16-bit niceleme); GPU skinning var. **Yok:** klipler arası geçiş (crossfade), karışım, durum makinesi, olay, kök hareketi, editörde zaman çubuğu |
| Morph target | 🟡 `content/morph.cpp` karıştırma fonksiyonu hazır; sahneye / köprüye / editöre **bağlı değil** |
| STL, OBJ, PLY, FBX | ❌ yok (dosya uzantısı reddedilir: "yalnız .glb ve .gltf") |
| Poligon / kod ile mesh | ❌ Yalnız 8 sabit ilkel (`kPrim*`: düzlem, küp, küre, kapsül, silindir, koni, dörtgen, simit) ve arazi/su/voksel üreticileri. Köprüde `mesh_olustur` yok |
| Sınırlar | sahnede en çok **16 model kaynağı** (`kSceneMaxAssets`), **256 varlık**, model başına **32 klip** (`max_clips`), **128 eklem** ve model başına **4 iskelet** (`kModelMaxSkins`), kare başına **16384 eklem matrisi** (eskiden 4096; 2026-11-13), glTF başına 128 mesh / 32 doku |
| Fizik çarpıştırıcı | "Modellere Otomatik Collider Ekle" menüsü var; içe aktarılan mesh'ten dışbükey kabuk/üçgen mesh çarpışması **ölçülmedi** |

## 2. Hedef ve ilkeler

Hedef: bir sanatçının/arkadaşın Blender'dan, Mixamo'dan ya da 3B baskı sitesinden aldığı dosyayı **sürükle-bırak** ile sahneye koyabilmek,
karakteri **yürütüp koşturabilmek** (animasyonlar akıcı geçsin), ve kodla/editörle **kendi çokgen şeklini** yapabilmek.

İlkeler (depo kuralları): her özellik bir **kapı** ile gelir ve kapının **pozitif kontrolü** vardır; sayı yazılıyorsa nerede/ne zaman ölçüldüğü yazılır;
kare içinde 0 ayırma (kapasiteler init'te); STL konteyneri yok; yeni dosya formatı = bozuk girdiyle (kesik, NaN, yalan sayaç) test edilir;
sürücü/cihaz verisi ölçülür, iddia edilmez. Mobil bütçe (Mali-G72 P20 Pro) her faz sonunda ölçülür.

## 2b. Durum (2026-11-13)

| faz | durum |
|---|---|
| bütçe ölçümü (§6/4) | ✅ f1840fa — masaüstü ölçüldü, eklem matrisi tavanı 4096→16384 + `skin_dropped`; **mobil ölçülmedi** |
| A1 editörde animasyon | ✅ 6540ab7 — klip seçici, ▶ Önizle (Oynat'sız), zaman çubuğu; kapı: görüntü farkı + kontrol |
| A1b çalma kipi | ✅ — ileri-geri / **döngü** / tek (`animasyon klip faz hiz [dongu\|tek]`, blob v11, `scene_anim_time` editör+oyunda ortak); yeni animasyonlu model varsayılan döngüde; eski sahneler ileri-geri kalır |
| A2 geçiş | ⏳ sıradaki |
| M1, M2, A3, A4, P1, P2, A5 | ⏳ |

## 3. Fazlar (öncelik sırasıyla)

Boyut: **S** ≈ bir oturum, **M** ≈ 2–3, **L** ≈ 4+. Her faz kendi commit/PR'ı; sıra kullanıcının "en çok görünen" kazancına göredir.

### Faz A1 — Animasyon editörde görünsün (M) ← ilk
Bugün animasyon yalnız oyun kodundan çağrılabiliyor; editörde modeli seçince **hiçbir şey göremiyorsun**.
- Özellikler'de **Animasyon kartı**: klip listesi (ad + süre), ▶/⏸/⏮ düğmeleri, hız, döngü, **zaman çubuğu** (sürükleyerek gez), "bu klibi sahne açılışında çal" (kayıtlı `clip/phase/speed`).
- Sahne görünümünde seçili modelin animasyonu **editör önizlemesinde** oynar (oyun çalışmadan); Oynat'ta oyunla aynı.
- Kaynaklar panelinde model karosuna klip sayısı rozeti.
- **Kapı:** arayüz sürücüsü senaryosu (model ekle → klip seç → ▶ → iskelet pozu kareler arasında değişti; durdur → sabit kaldı) + pozitif kontrol (klipsiz modelde kart yok). Bit eşitlik: aynı `phase` aynı poz (replay).

### Faz A2 — Çapraz geçiş ve katman (M)
- `sim/animation`: iki pozu karıştır (eklem başına lerp + nlerp), **crossfade(klip, süre)**; çekirdek sıfır ayırma, job'lanabilir.
- Köprü: `eng_anim_gecis(id, klip, sure_s, dongu)`, `eng_anim_hiz(id, hiz)`; Tulpar yüzü Türkçe/İngilizce ikiz (`anim_gecis` / `anim_blend`). `SPEC`'e satır + `tools/gen_engine_bindings.py`.
- **Kapı:** yürü→koş geçişinde ara karelerde poz iki klip arasında (sayısal: eklem açısı monoton); süre 0 = anında; **KAPI_BOZ** pozitif kontrol (geçişi bozunca sıçrama ölçülür).
- Ölçüm: 10 karakter × 60 eklem karışımı, Mali-G72'de ms (cihaz verisi, basılır).

### Faz M1 — Model içe aktarmayı sağlamlaştır (M)
- İçe aktarma **ayarları** (proje başına `model.ayar`): birim (m / cm / mm), yukarı eksen (Y / Z), ölçek, pivot (merkez / taban), doku sıkıştırma; önbellek anahtarına girer (importer cache zaten var).
- **Önizleme:** Kaynaklar'da dönen küçük resim; üçgen/eklem/klip sayısı, **bütçe uyarısı** (mobil için üçgen sayısı).
- Sınır hataları **editörde okunur** çıksın (bugün yalnız sayaç/günlük: örn. `clips_dropped`; kullanıcı görmeyebilir — ölçülecek): 17. model, 33. klip, 129. eklem → "bu model sahneye sığmıyor: neden + ne yapmalı".
- `kSceneMaxAssets` 16 → artırma kararı (blob sürümü): önce ölçülür, çoğu oyun 4–8 kullanır; gerekirse v5.
- **Kapı:** Khronos örnek setinden 6 model (kutu, iskeletli, morph'lu, doku varyantları) ayrı test dizininde; bozuk/kesik GLB reddi (çökme yok, hata metni).

### Faz M2 — STL ve OBJ (+MTL) (M)
- `content/stl.cpp`: **ikili ve ASCII STL**, üçgen sayısı doğrulaması (ikili başlıktaki sayı ↔ dosya boyutu), NaN/Inf reddi, köşe birleştirme + **yumuşak normal** (açı eşiği ayarlı), tek malzeme (renk seçilir), birim mm→m.
- `content/obj.cpp`: `v/vt/vn/f` (çokgen yüzler üçgenlenir: kulak kesme), `.mtl` (Kd, Ks, map_Kd), grup/nesne ayrımı.
- İkisi de aynı `Model` yapısına iner → mevcut çizim/gölge/LOD/fizik yolu **değişmez**. Uzantı tablosu editörde ve sürükle-bırakta genişler; sahne biçimi `.sahne`'ye ek alan gerekmez (kaynak yolu yeterli).
- **FBX / USD / PLY:** bilerek **yok** (FBX kapalı biçim; "Blender'dan glTF olarak dışa aktar" rehberi `Yardım`'da). PLY istenirse STL ile aynı iskelet, S.
- **Kapı:** `tools/make_test_stl.py` ile ürettiğimiz küp/küre (üçgen sayısı, sınır kutusu bayt-bayt), kesik/yalan-sayaçlı STL reddi, OBJ'de dörtgen+beşgen yüz üçgenlenmesi (yüz sayısı hesabı).

### Faz A3 — Animatör: durum makinesi (L)
- **Animatör denetleyicisi** (`.animator` metin dosyası, deterministik biçim): durumlar (klip), geçişler (koşul: bool / sayı / tetik; geçiş süresi), **1B karışım ağacı** (yürü↔koş hıza göre).
- Editörde **tablo + basit grafik** görünümü (düğüm/ok); "Animatör ekle" Özellikler kartı; parametreler kodla: `anim_param(id, "hiz", 3.5)`, `anim_tetik(id, "ziplama")`.
- Çekirdek `sim/animator.cpp`: parametre → durum → karışım ağırlıkları; kare içi sıfır ayırma, replay bit eşit.
- **Kapı:** otopilot "hız arttıkça yürü→koş" (durum geçiş günlüğü), geçiş koşulu bozuk dosya reddi, KAPI_BOZ.

### Faz A4 — Olaylar ve kök hareketi (M)
- Klipte **zaman işaretçisi olayı** (ayak sesi, vuruş anı) → **olay kuyruğu** (çarpışma kuyruğu kalıbı: callback yok, `anim_olay_oku`).
- **Kök hareketi** (isteğe bağlı kutu): kökün klip içi öteleme farkı karaktere uygulanır (kayan ayak yok).
- **Kapı:** olay kare numarası klip süresiyle uyumlu (hesap yazılı), kök hareketi bir tam döngüde beklenen mesafe.

### Faz P1 — Poligon: kodla mesh (M)
- Köprü kalıbı "hesapla sonra oku" (düz skaler ABI: struct/dizi yok): `mesh_basla()`, `mesh_koseli(x,y,z,u,v)`, `mesh_ucgen(a,b,c)`, `mesh_bitir() → model` (+ tavan: 4096 köşe, doldurunca **sayılır**, geçersiz handle). Ayrıca kısayollar: `mesh_cokgen_yukselt(noktalar..., yukseklik)`.
- Prosedürel şeyler (kırılan duvar, dalga, el yapımı çokgen) oyunda üretilebilir; üretilen mesh model gibi `spawn` edilir.
- **Kapı:** köşe/indeks sayıları hesapla eşit; ters sarım (içten görünür) bozunca KAPI_BOZ kırmızı; kare içinde ayırma yok.

### Faz P2 — Editörde poligon çizimi (L)
- "Ekle > Çokgen": zeminde **tıkla-nokta** ile 2B çokgen çiz (≤ 32 nokta), Enter = kapat; **yükselt** (yükseklik alanı) ya da düz zemin; kenar/nokta **gizmo ile sürükle** (nokta düzenleme gizmosu zaten var).
- Yeni sahne bileşeni `kScenePoly` (noktalar + yükseklik + malzeme) → `.sahne` yazıcı/ayrıştırıcı + `scene_check.py` tablosu + `.sahneb` blob sürümü; çarpışma: kulak kesmeden **dışbükey parçalara** bölüp Jolt gövdesi.
- **Kapı:** yaz → oku → yaz bayt eşit (scene_check), yükseltilmiş çokgenin sınır kutusu, içbükey çokgende çarpışma parçaları dışbükey.
- Sonra (ERTELİ): yüz/kenar/köşe düzenleme, boolean, UV açma (ProBuilder düzeyi) — gerçek 3B modelleme Blender'ın işi.

### Faz A5 — Morph target + yüz/şekil animasyonu (M)
- Mevcut `morph.cpp`'yi sahneye ve köprüye bağla; glTF `weights` kanalı klip olarak çalsın; `eng_morph_agirlik(id, hedef, w)`.

### Faz A6 — Karakter yeniden hedefleme (L, **ertele**)
Mixamo'dan indirilen bir animasyonu başka karaktere uygulamak (humanoid eşleme). Değerli ama büyük; A1–A4 bitip gerçek bir karakter oyunu ihtiyacı doğunca açılır.

## 4. Sıra ve bağımlılık

```
A1 (editörde gör) → A2 (geçiş) → M1 (içe aktarma sağlamlığı) → M2 (STL/OBJ) → A3 (animatör) → A4 (olay/kök) → P1 (kodla mesh) → P2 (editörde çokgen) → A5 → [A6]
```
Gerekçe: kullanıcı animasyonları ve hareketi beğendi — **en görünür kazanç A1/A2**. M1 sınır hatalarını açar (yoksa gerçek bir model sessizce sığmaz). STL/OBJ küçük ve bağımsız.
A3, A2'ye; P2, P1'e dayanır. Paralel iş yok (kullanıcı kuralı: sıralı).

## 5. Riskler

| risk | önlem |
|---|---|
| Sanatçı varlığı yok | Khronos örnekleri (CC0/CC-BY, depoya **girmez**, test dizinine indirme betiği) + kullanıcının kendi modelleri |
| Mixamo/Sketchfab lisansı | depoya kopyalanmaz; yalnız kullanıcı kendi makinesinde kullanır (belgede yazılı) |
| Animatör kapsam şişmesi | A3 bilerek **dar**: durum + 1B karışım ağacı + geçiş koşulu; katman maskeleri/IK yok |
| `.sahneb` blob sürümü | model kaynak sınırı/`kScenePoly` blob v5 ister: önce ölçüp karar, sürüm sıçraması geriye uyumlu okuma ile |
| Mobil bütçe | her faz sonunda Mali-G72 ölçümü (eklem × karakter, üçgen sayısı) basılır, iddia edilmez |
| FBX beklentisi | belgede açık: glTF'e dönüştür; kapalı biçim desteklenmez |

## 6. Sizden karar bekleyen noktalar (varsayılanlarla ilerlenir)

1. **"Poligon"** derken P1/P2 (kendi çokgenini yapmak) mi, yoksa yalnız "düşük poligonlu model içe aktarmak" mı kastedildi? (Varsayılan: ikisi de, M2/M1 önce.)
2. **OBJ** de gelsin mi (STL'e ek)? (Varsayılan: evet, küçük.)
3. **Animatör** (A3) ne kadar derin? (Varsayılan: dar — durum + geçiş + 1B karışım.)
4. **Hedef cihaz bütçesi — ÖLÇÜLDÜ 2026-11-13 (kullanıcı "daha fazla taşır" dedi, haklı çıktı; masaüstünde):**
   `tools/karakter_olcum.sh <eklem> <üçgen> <N...>` (N iskeletli, animasyonlu karakter, her biri kendi fazında; köprü p50, taban N=1 ≈ 12,7 ms).
   RTX 5080 (Vulkan 1.4), 1280x720, gölgeli, taban üstü **fark**:

   | karakter | N=50 | N=100 | N=200 |
   |---|---|---|---|
   | 60 eklem, 5K üçgen | — | +0,3 ms | +0,7 ms |
   | 60 eklem, 15K üçgen | — | +0,7 ms | +1,7 ms (3 M iskeletli üçgen/kare) |
   | 128 eklem, 30K üçgen | +0,7 ms | +2,0 ms | (tavan: 16384 matris = 128 karakter) |

   CPU yarısı (`anim_olcum_karakter_butcesi`, tek parçacık, poz örnekleme + model + skin): 200 karakter × 60 eklem **0,47 ms**, × 128 eklem 1,0 ms.
   **Asıl bulgu — gerçek sınır GPU/CPU değil bir sabitti:** `max_skin_matrices = 4096` (kare başına eklem matrisi) → 60 eklemli karakterden yalnız **68** çizilebiliyordu,
   fazlası **sessizce kayboluyordu** (200 karakterlik sahnede çizim sayısı 68'de takıldı). 16384'e çıkarıldı (273 karakter × 60 eklem, 1 MiB/kare tamponu) ve taşma artık
   `RendererStats::skin_dropped` ile sayılır (kapı: `render_skin_matris_tavani_sayilir`, pozitif kontrol dahil).
   **Mobil henüz ÖLÇÜLMEDİ** (bağlı telefon yok): masaüstü sayısı mobil için iddia edilmez. Geçici hedef: **40 karakter × 60 eklem × 10K üçgen** (RTX'te +0,3 ms civarı), telefonda aynı betikle ölçülünce güncellenir.
