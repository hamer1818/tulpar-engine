# Model rehberi — dışarıdan karakter ve eşya getirmek

Bu sayfa oyun yapan kişi içindir: hangi dosyayı nereden bulursun, editöre nasıl alırsın, animasyonu nasıl çalıştırırsın.
Ölçüm ve iç ayrıntı: `docs/MODEL-ANIMASYON-PLANI.md`.

## Hangi dosya?

| Biçim | Ne zaman |
|---|---|
| **GLB / glTF** | En iyisi. İskelet, animasyon klipleri, dokular, yüz ifadeleri tek dosyada. Karakterler için bunu seç. |
| OBJ (+ .mtl) | Hareketsiz eşya (kaya, sandık, ev). Animasyon yok. |
| STL | Yalnız şekil (3B baskı modelleri). Renk ve animasyon yok. |
| FBX | Doğrudan yüklenmez → Blender'da GLB'ye çevir (aşağıda). |

## Nereden?

- **Quaternius** (quaternius.com) — CC0 (her işe serbest), düşük poligon, klipleri `Idle`, `Walk`, `Run`, `Jump` adlı: otomatik animatör
  doğrudan tanır. Paketlerin çoğu FBX/Blend gelir → GLB'ye çevir. Hazır GLB örneği: three.js deposundaki `RobotExpressive.glb`
  (Quaternius, CC0) — editörde baştan sona denendi (2026-11-16).
- **Kenney** (kenney.nl) — CC0, eşya ve ortam.
- **Poly Pizza** (poly.pizza) — düşük poligon, CC0 / CC-BY (CC-BY'de yazarın adını oyunda an).
- **Sketchfab** — "Downloadable" + lisans süzgeci, "glTF" olarak indir.
- **Mixamo** (Adobe) — ücretsiz animasyon; FBX verir ve her klibin adı `mixamo.com` olur → Blender'da klipleri `Idle` / `Walk` / `Run`
  diye yeniden adlandır (yoksa otomatik animatör tanıyamaz; elle `#0`, `#1` sıra numarasıyla da bağlanabilir).
- **Fab** (Epic) — ürün sayfasında **Formats**'a bak:
  - glTF / FBX / OBJ seçeneği varsa indir, kullan.
  - Yalnız **Unreal Engine** biçimi varsa (indirince "Unreal projesi oluştur" der): Unreal'ı kur, boş projeye ekle, modele sağ tık →
    **Asset Actions → Export** → glTF (ya da FBX → Blender → GLB). Haritalar (seviyeler) taşınmaz: içindeki modelleri tek tek dışa aktar,
    sahneyi editörde kur.
  - Lisans: ürün **"UE-Only"** / "yalnız Unreal Engine projelerinde" diyorsa başka motorda KULLANILAMAZ (dışa aktarmak teknik olarak
    mümkün olsa bile). Oyun satılacaksa her ürünün lisansını sayfasında oku.

## FBX → GLB (Blender, ücretsiz)

File → Import → FBX → (Mixamo ise Action Editor'da klipleri yeniden adlandır) → File → Export → **glTF 2.0** → Format: **glTF Binary (.glb)**.

## Editörde

1. **Kaynaklar** → **Model içe aktar…** (ya da dosyayı pencereye sürükle bırak). Dosya sahne klasörüne kopyalanır; Konsol bütçe satırı
   yazar (üçgen, eklem, klip, boyut, "mobilde ≈ N kopya sığar"). Turuncu uyarı = telefon için ağır.
2. Kaynaklar'da karta **çift tık** → sahneye eklenir. Animasyonlu modelde **Animasyon** bileşeni kendiliğinden gelir (bekleme klibi, döngü).
3. Boyu/yönü tuhafsa: karta **sağ tık → içe aktarma ayarları** (birim cm/m, yukarı eksen, ölçek, pivot; önizleme canlı).
4. Animasyon kartında **Animatör oluştur (otomatik)** → bekle/yürü/koş (+ zıpla) kurulur. Kartın **Hareketler** bölümünde her hareket
   bir satır: klibini listeden seç, yürüme/koşma hızını değiştir, **▶** ile dene. **Önizleme hızı**'nı sürükle: geçişi gör.
   **Hareket ekle** → el sallama, yumruk gibi tek seferlik bir klip ekler ve bir **tuş** önerir (1, 2, …): oyunda o tuşa basınca oynar,
   bitince yürümeye döner — kod gerekmez. **Geri al** son değişikliği geri alır.
   **Düzenle** → durum grafiği: kutular klipleri gösterir; bir duruma ya da oka tıkla, üstte düzenlenir. Durum ekle / Geçiş ekle / sağ tık
   menüleri; her değişiklik hemen kaydedilir.
5. Yürütmek için kod gerekmez: **Davranış ekle → Devriye (ileri-geri yürü)**; oyunda hız 1,5 m/s → yürür, 4+ → koşar.
   Kodla: nesneyi neyle hareket ettirirsen ettir (`sahne_konum_ver`, fizik), animatör hızı hareketten okur. Zıplama gibi anlık işler:
   `sahne_animator_tetik(sahne_bul("ad"), "zipla")`.

## Sınırlar

Sahne başına 64 model dosyası · model başına 64 klip, 128 eklem · telefonda karede toplam ≲ 50 bin iskeletli köşe (P20 Pro ölçümü).
