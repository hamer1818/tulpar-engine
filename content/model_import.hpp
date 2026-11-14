// L6 CONTENT — Model içe aktarma ayarları (M1, docs/MODEL-ANIMASYON-PLANI.md): birim, yukarı eksen, ölçek, sığdırma, pivot.
//
// Ayar MODELİN YANINDA durur: `kale.glb` -> `kale.glb.ayar` (Unity'nin .meta'sı gibi; model kopyalanınca/paketlenince ayarı da
// yanında gider). Dosya yoksa biçimin varsayılanı geçerlidir (glTF/OBJ: dokunma; STL: Z-yukarı, 1 m'ye sığdır, taban pivotu — M2'deki
// davranış). Okuyan TEK yer `gltf_load`: editör, sahne çalışma zamanı, köprü ve engine_sahnec aynı sonucu görür.
//
// Uygulama KÖŞELERE DEĞİL örnek (instance) matrislerine: C = Ölçek · Öteleme(-pivot) · Eksen, her `ModelInstance::world`'ün önüne
// çarpılır ve sınır kutusu yeniden hesaplanır. Böylece iskeletli/animasyonlu glTF de aynı yoldan geçer (skin matrisi C'den önce),
// meshopt/LOD/morph verisi değişmez ve editör ayarı GPU'ya yeniden yüklemeden değiştirebilir (`model_import_reapply`).
// Kök hareketi (A4, model_clip_root_xz) C'nin doğrusal kısmından geçer: ölçekli modelde yürüme mesafesi de ölçeklenir.
//
// Biçim (deterministik metin; yazıcı kanonik, okuyucu katı — bozuk ayar modeli YÜKLETMEZ, satır numaralı hata verir):
//   tulpar-model-ayar 1
//   birim mm          # m | cm | mm | inc   (dosya birimi -> metre)
//   yukari z          # y | z
//   olcek 1.5         # birimin üstüne çarpan (> 0)
//   sigdir 0          # > 0: en büyük boyut bu kadar METRE olur (birim/olcek yerine)
//   pivot taban       # koken | taban | merkez
#pragma once
#include <cstddef>
#include <cstdint>

#include "content/model.hpp"

namespace tulpar::engine::content {

enum class ModelUnit : uint8_t { M = 0, Cm = 1, Mm = 2, Inch = 3 };
enum class ModelPivot : uint8_t { Origin = 0, Base = 1, Center = 2 };

struct ModelImportSettings {
  ModelUnit unit = ModelUnit::M;
  bool z_up = false;
  float scale = 1.0f;
  float fit = 0.0f;
  ModelPivot pivot = ModelPivot::Origin;
};
bool model_import_equal(const ModelImportSettings &a, const ModelImportSettings &b);
float model_unit_meters(ModelUnit u);

// Uzantıya göre varsayılan (ayar dosyası YOKKEN geçerli olan): .stl -> Z-yukarı + 1 m sığdır + taban; diğerleri dokunulmaz.
ModelImportSettings model_import_defaults(const char *model_path);
// `<model_path>.ayar` yolunu yazar. false: tampon küçük.
bool model_import_path(const char *model_path, char *out, size_t cap);
// Metin -> ayar. false: err'e satır numaralı Türkçe sebep. Bilinmeyen anahtar / tekrar / aralık dışı REDDEDİLİR.
bool model_import_parse(const char *text, size_t len, ModelImportSettings *out, char *err, size_t err_cap);
// Kanonik metin; dönüş yazılan bayt (NUL hariç), 0 = tampon küçük.
size_t model_import_write(const ModelImportSettings &s, char *buf, size_t cap);
// Dosyadan: yoksa *found = false ve *out = varsayılan (true döner). Varsa ve bozuksa false (err dolu).
bool model_import_load(const char *model_path, ModelImportSettings *out, bool *found, char *err, size_t err_cap);
// Dosyaya yazar (varsayılana eşitse dosyayı SİLER: ayarsız model ile varsayılan ayarlı model aynı şey, iki doğruluk olmasın).
bool model_import_save(const char *model_path, const ModelImportSettings &s);
// Ayar dosyasının içerik özeti (önbellek anahtarına karışır; dosya yoksa 0).
uint64_t model_import_file_hash(const char *model_path);

// Ham sınır kutusu (instance uzayı) -> C ve yeni sınır. Sığdırma boyutu sıfır/geçersizse birim·ölçek kullanılır.
Mat4 model_import_matrix(const ModelImportSettings &s, Vec3 raw_lo, Vec3 raw_hi, Vec3 *out_lo, Vec3 *out_hi);
// Yeni yüklenmiş (C'siz) modele uygular: instance'lar C ile çarpılır, sınır güncellenir, Model::import_* doldurulur.
void model_import_apply(Model &m, const ModelImportSettings &s);
// Zaten uygulanmış modelde ayarı değiştirir (editör): eski C geri alınıp yenisi uygulanır; GPU verisi aynı kalır.
void model_import_reapply(Model &m, const ModelImportSettings &s);

} // namespace tulpar::engine::content
