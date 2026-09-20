// L6 CONTENT — prosedurel ilkel geometri tablosu.
//
// NEDEN VAR: PR #331 editore "Kapsul / Silindir / Koni / Dortgen / Simit"
// ekleme menusunu getirdi ve bu basligi DORT yerden include etti
// (content/scene_runtime.{hpp,cpp}, app/editor_app.cpp), ama dosyayi eklemeyi
// unuttu. Baslik bir HPP'den include edildigi icin hata bulasiciydi: onu
// dolayli goren her ceviri birimi de dusuyordu.
//
// Fikir: bu geometriler bir glTF kaynagindan GELMEZ, motorun kendi
// ureteclerinden (renderer::Renderer::sphere/capsule/...) uretilir. O yuzden
// varlik bir SceneEntity'de `asset = -1` ve `primitive = <yuva>` olarak durur.
//
// TEK TABLO, IKI CAGIRAN: SceneRuntime yukleme aninda, editor varlik
// duzenlenince buradan kurar. Tabloyu kopyalamak iki ayri dogruluk kaynagi
// yaratirdi (birinde yuva eklenir otekinde unutulur), bu yuzden hem editor hem
// runtime AYNI fonksiyonu cagirir.
#pragma once
#include <cstdint>

#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

// YUVA NUMARALARI EDITOR MENU KODLARIYLA AYNI. Bu bilerek secildi
// (app/editor_widgets.cpp `kCreate3D` tablosu ve app/editor_app.cpp'deki
// `case 20..24: e.primitive = kind;` dali): menude tiklanan kod dogrudan
// yuva indeksi oluyor, arada bir esleme tablosu tutulmuyor.
enum : int32_t {
  kPrimParticle = 0,  // SceneRuntime parcaciklari prims_[0] ile ciziyor
  kPrimPlane    = 8,
  kPrimCube     = 10,
  kPrimSphere   = 11,
  kPrimCapsule  = 20,
  kPrimCylinder = 21,
  kPrimCone     = 22,
  kPrimQuad     = 23,
  kPrimTorus    = 24,
};

// AYNI yuvalarin isaretsiz esanlamlilari. Cagiran ham sayi yazmasin diye:
// parcacik cizimi bir zamanlar prims_[0]'i ham 0 olarak soruyordu ve slot 0 hic
// doldurulmadigi icin parcaciklar hic cizilmiyordu — sessizce, cunku 0 da
// gecerli bir indeks. Iki yazim da ayni sabiti gosterir; yeni kod hangisini
// kullanirsa kullansin derlenir (iki dalin cagiranlari birlesti).
constexpr uint32_t kPrimitivePlane    = (uint32_t)kPrimPlane;
constexpr uint32_t kPrimitiveCube     = (uint32_t)kPrimCube;
constexpr uint32_t kPrimitiveSphere   = (uint32_t)kPrimSphere;
constexpr uint32_t kPrimitiveCapsule  = (uint32_t)kPrimCapsule;
constexpr uint32_t kPrimitiveCylinder = (uint32_t)kPrimCylinder;
constexpr uint32_t kPrimitiveCone     = (uint32_t)kPrimCone;
constexpr uint32_t kPrimitiveQuad     = (uint32_t)kPrimQuad;
constexpr uint32_t kPrimitiveTorus    = (uint32_t)kPrimTorus;

// Tablo boyu = en buyuk yuva + 1. Cagiranlar `primitive` alanini bu sinira
// gore denetliyor (scene_runtime.cpp, editor_app.cpp), o yuzden
// yeni bir ilkel eklenirse burasi da buyumeli.
constexpr uint32_t kPrimitiveSlotCount = 25;

// Tabloyu (out[kPrimitiveSlotCount]) doldurur; YUKLEME ANINDA bir kez
// cagrilir, kare icinde degil. Dolu yuvalar:
//   0 Parcacik (= Kup mesh'i) · 8 Duzlem · 10 Kup · 11 Kure · 20 Kapsul
//   21 Silindir · 22 Koni · 23 Dortgen · 24 Simit
// Kullanilmayan yuvalar gecersiz MeshHandle olarak kalir — cagiranlar zaten
// `valid()` ile bakiyor, yani bos yuva cizilmez.
//
// Bir uretec gecici tampon tavanini asarsa o yuva KURULMAZ (bozuk mesh
// cizmektense eksik ciz). Donus: kurulan mesh sayisi.
uint32_t build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out);

} // namespace tulpar::engine::content
