# Yer tutucu ses

`altin.wav` **üretilmiş bir yer tutucudur**: sinüs, zarf ve gürültüyle sentezlendi, bir ses
kütüphanesinden alınmadı. Lisansı bu depoyla aynıdır, üçüncü taraf hakkı yoktur.

| dosya | kim kullanıyor |
|---|---|
| `altin.wav` | `examples/engine_arena.tpr` (puan sesi), `tests/engine_bridge.test.tpr` (ses kapısı) |

Biçim: mono, 16 bit PCM, 22050 Hz. Dosya yoksa arena oyunu sentetik bir tona düşer.

Kaynak: TulparLang deposunda `examples/assets/sesler/` (motor ayrılmadan önceki son hâli,
86e2c4e'nin ebeveyni). Depo ayrılırken yalnız `.sahne` dosyaları taşınmıştı; bu dosya ve
örnek sahnelerin küpü (bugün `../dama_kup.gltf`) geride kalmıştı (Tuzaklar 8bz, 8ca).
