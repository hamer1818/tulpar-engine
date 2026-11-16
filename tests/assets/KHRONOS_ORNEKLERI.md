# Khronos glTF örnek modelleri (içe aktarma kapısı)

Kaynak: https://github.com/KhronosGroup/glTF-Sample-Assets (`Models/<ad>`), 2026-11-16'da indirildi, değiştirilmeden.
Kapı: `tests/test_khronos_ornekleri.cpp`. Logo/marka içeren modeller (LicenseRef-LegalMark) bilerek alınmadı.

| dosya | lisans | hak sahibi |
|---|---|---|
| khronos_Box.glb | CC-BY-4.0 | Cesium |
| khronos_RiggedSimple.glb | CC-BY-4.0 | Cesium |
| khronos_AnimatedMorphCube.glb | CC0-1.0 | Microsoft |
| khronos_Fox.glb | model CC0-1.0 (PixelMannen); rig + animasyon CC-BY-4.0 (tomkranis); glTF dönüşümü CC-BY-4.0 (@AsoboStudio, @scurest) |
| khronos_InterpolationTest.glb | CC0-1.0 | Khronos |
| khronos_TextureCoordinateTest.glb | CC0-1.0 | Ed Mackey (Analytical Graphics, Inc.) |
| khronos_SimpleSkin.gltf | CC0-1.0 | Marco Hutter (https://github.com/javagl/) |
| khronos_Fox.fbx | khronos_Fox.glb ile aynı (lisans aynı) | Blender 5.3 ile .glb -> .fbx (gömülü doku), FBX kapısı için |

## Bizim ürettiğimiz (lisans: depo)

| dosya | nasıl |
|---|---|
| insansi_mixamo.gltf / insansi_unreal.gltf / insansi_bozuk.gltf | `tools/make_test_insansi.py` (A6 yeniden hedefleme kapısı) |
| insansi_mixamo.fbx | Blender 5.3 ile insansi_mixamo.gltf -> .fbx (FBX kapısı: aynı karakter iki biçimde) |
