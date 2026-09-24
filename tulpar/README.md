# `tulpar/` — motorun TulparLang tarafı

Motor C++ kalır, **oyun betikleri Tulpar'da yazılır**. Bu dizin o bağlantının
TulparLang tarafındaki bütün parçalarını tutar. Daha önce bunlar TulparLang
derleyici deposunun içine dağılmıştı; derleyici artık motoru hiç tanımadığı için
(dil deposunda yalnız dilin kendi özellikleri var) hepsi buraya taşındı.

```
tulpar/
  engine.tpr              -> TulparLang'de lib/engine.tpr olarak gömülür (TR/EN sarmalayıcı)
  generated/              -> tools/gen_engine_bindings.py üretir, ELLE DÜZENLENMEZ
    engine_bindings.cpp        aot_eng_*_ptr (VMValue ABI) -> teng_* (C ABI)
    engine_builtins_table.inc  LLVM backend tablosu (ad, sembol, arite)
    engine_builtins_sigs.inc   tip çıkarımı imzaları
    engine_builtins.inc        LSP tamamlama/hover
  examples/               engine_ilk_oyun.tpr, engine_arena.tpr, engine_aksiyon.tpr,
                          engine_betik_dagitimi.tpr (betik kancalari + tetik bolgeleri),
                          engine_karakter.tpr (karakter denetleyicisi: basamak, ziplama, tetik),
                          engine_dalga.tpr (kodla uretilen dusmanlara `betik_ata`; davranislar
                          davranis/dusman.tpr + davranis/tuzak.tpr — docs/KOPRU.md 7.10)
                          (aksiyon: TulparLang P0 dil özellikleri — `enum Ekran/Durum/Hal`,
                          `yon_hesapla(): (float, float)`, düşman kaydı tek `Dusman[]`
                          — 11 paralel dizi 2026-09-21'de kalktı)
  tests/engine_bridge.test.tpr   uçtan uca köprü testi (Tulpar tarafı)
```

## Tek kaynak kuralı

199 `eng_*` builtin'in tek kaynağı `tools/gen_engine_bindings.py` içindeki `SPEC`
tablosudur. Dört dosya da tek komutla üretilir:

```bash
python3 tools/gen_engine_bindings.py            # -> tulpar/generated/
```

Bir builtin eklemek: `bridge/engine_api.h` + `bridge/engine_api.cpp` yaz, `SPEC`'e
satır ekle, betiği yeniden koştur. Elle tutulan nokta olmadığı için CLAUDE.md'nin
"5 noktada bağlama"sı bu aile için mekaniktir — bağlama noktaları birbirinden kayamaz.

## Bir TulparLang kopyasına kurmak

**Kısa yol:** `tools/motor_derleyici.sh` aşağıdaki elle bağlamanın tamamını bir `git worktree`
içinde yapar (kullanıcının kopyasına dokunmaz) ve `yapi/tulpar-motor/tulpar` üretir; editörün
"Oyunu çalıştır"ı onu kullanır. Aşağısı neyin bağlandığını anlatıyor.

Derleyici deposu motoru artık tanımıyor. Motoru bir TulparLang çalışma kopyasına
yeniden bağlamak istersen dosyaları tarihsel yerlerine kuran kip var:

```bash
python3 tools/gen_engine_bindings.py --tulpar /yol/TulparLang
```

Bu, `runtime/engine_bindings.cpp`, `src/aot/engine_builtins_table.inc`,
`src/typeinfer/engine_builtins_sigs.inc` ve `src/lsp/engine_builtins.inc`
dosyalarını yazar. Kök gerçekten TulparLang değilse (`src/vm/vm.hpp` yoksa) betik
yazmadan durur. Geri kalanı — `engine.tpr`'yi `lib/`'e koymak,
`cmake/EmbedLibraries.cmake` + `src/embedded_libs.h.in` slotu, `llvm_backend.cpp`
içindeki `.inc` dahil etme noktaları, `engine_link_flags()` ve `uses_engine`
işaretleyicisi — elle geri bağlanmayı ister; bu dizin yalnız içeriği taşır.

## ABI bağımlılığı: `TULPAR_ROOT`

`generated/engine_bindings.cpp` TulparLang'in **değer ABI'sini** görür
(`VMValue`, `Obj*`), yani `<TulparLang>/src/vm/vm.hpp` (o da yalnız
`bytecode.hpp`'yi çeker) gerekir. O başlıklar **bu depoya kopyalanmadı**: bir kopya,
derleyici ABI'yi değiştirdiğinde sessizce kayar ve uyuşmazlık ancak cihazda çökme
olarak görünür. Bunun yerine yol açıkça istenir:

```bash
cmake -S . -B build-android \
      -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
      -DTULPAR_ROOT=/yol/TulparLang
```

`TULPAR_ROOT` verilmezse `tulpar_engine_android` hedefi **kurulmaz** ve nedeni
yapılandırma çıktısına yazılır; motorun kendi Android hedefi (`tulparengine`)
bundan etkilenmez. Üretilen dosyanın `#include` satırları göreli değil **include
dizini** tabanlıdır (`"vm/vm.hpp"`, `"bridge/engine_api.h"`), böylece aynı dosya
hem bu depoda hem bir TulparLang kopyasında derlenir.
