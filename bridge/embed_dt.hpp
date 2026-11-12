// L6 BRIDGE — gomulu kipte (editorun Oynat'i) kare suresini 60 Hz'e SABITLER.
//
// SORUN (olculdu 2026-11-12, Linux, RTX 5080, editorun Oynat'i, "Top topla" otopilot, 583 kare, topun kare basina yer
// degistirmesi): karelerin %16'si DONUK (0 fizik adimi), %16'si CIFT adim (2). Sebep: oyun kendi saatinde 60 Hz'e uyuyor
// (embed_frame_wait) ama gercek `dt` olculuyor; uyku ve kare isi 16-18 ms arasi oynatiyor. Fizik SABIT 1/60 adimla
// (sim::TickClock) ilerliyor: dt 16.6 ms ise o karede 0 adim, 17.0 ise 2 adim -> ayni hizda giden top bir karede durup
// ardindan iki kat gidiyor ("kasa kasa"). Kullanici bunu "top hareket ederken takiliyor" diye gordu.
//
// COZUM: dt 60 Hz bandindaysa TAM 1/60 sayilir (kare basina tam bir adim). Band disi (gercek takilma, ya da yuk altinda
// yavas kare) oldugu gibi gecer: yakalama davranisi ve sim saati bozulmaz. Tempo zaten mutlak programla (`embed_next_ns`)
// 60 Hz'e baglandigi icin ortalama sure ayni kalir; bu yalniz kare-kare dagilimi duzeltir.
#pragma once

namespace tulpar::engine::bridge {

constexpr float kEmbedDt = 1.0f / 60.0f;
inline float embed_dt_snap(float dt) {
  // [%80, %125]: ~13.3..20.8 ms. Alt/ust sinir bilerek genis degil: 25 ms'lik (40 fps) kare "60 Hz" sayilmaz.
  return (dt > kEmbedDt * 0.8f && dt < kEmbedDt * 1.25f) ? kEmbedDt : dt;
}

} // namespace tulpar::engine::bridge
