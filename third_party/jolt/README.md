# Jolt Physics (vendored)

**v5.7.0** (2026-11-23; v5.3.0 idi), MIT (LICENSE). Yalnız `Jolt/` kaynak ağacı; upstream `Jolt.cmake` çıkarıldı — derleme `engine/CMakeLists.txt`'te
(`engine_jolt`), glob ile. `ObjectStream/` olduğu gibi derlenir (SerializableObject.cpp'deki `GetRTTIOfType` her yerden
çağrılıyor) ama `JPH_OBJECT_STREAM` tanımlı olmadığı için metin serializer'ın kendisi dosya içi
`#ifdef` ile dışarıda kalır.

Bayraklar (plan Faz 2 / REV 8): `JPH_CROSS_PLATFORM_DETERMINISTIC` (FMA kapalı, platformlar arası bit
eşitliği iddiası — CI Linux x86_64 ↔ macOS arm64 altın özetle **sınanır**); x86_64'te `-msse4.2` (AVX yok:
düşük segment ve belirlenimlilik), arm64'te NEON; `JPH_OBJECT_STREAM`, `JPH_DEBUG_RENDERER`,
`JPH_PROFILE_ENABLED` kapalı; exception/RTTI kapalı (Jolt kendi RTTI'sini kullanır).

Neden vendor: raylib/sqlite deseni; derleme makinesinde paket gerekmez. Güncelleme: aynı etiketle
tarball indir, `Jolt/` kopyala, ObjectStream'i ve Jolt.cmake'i sil, bu satırı güncelle.

5.7 notu (2026-11-23): `Compute/` (VK/DX12/MTL/CPU) ve `Shaders/` + `Physics/Hair/` geldi. Arka uçlar `JPH_USE_VK` /
`JPH_USE_DX12` / `JPH_USE_MTL` / `JPH_USE_CPU_COMPUTE` ile korunuyor ve hiçbirini tanımlamıyoruz: GPU hesap yolu derlenmez, Jolt motorun
Vulkan'ına dokunmaz. `JobSystem::CreateJobs` 5.7'de saf sanal oldu → `sim/physics.cpp` `FiberJoltJobs`. Altın fizik özeti
her Jolt güncellemesinde değişir (CMakeLists `ENGINE_PHYSICS_GOLDEN_HASH`).
