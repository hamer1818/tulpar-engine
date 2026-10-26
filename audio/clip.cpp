#include "audio/clip.hpp"

#include <cmath>
#include <cstdio>

#include <miniaudio.h>

namespace tulpar::engine::audio {

// MONO KAYNAK MONO KALIR (M2, 2026-10-09): eskiden her klip karistiricinin cikis
// kanal sayisina (stereo) acilirdi; mono WAV efekti arenada iki kat yer tutuyordu
// (oyunun 15 SFX'i ~8.7 MB'in yarisi bosa). Karistirici mono klibi zaten calar
// (mixer.cpp: cl.channels == 1 -> her kanala ayni ornek), tepe ve sinyal ayni.
// Kural: kaynak 1 kanalsa 1 kanal; degilse eskisi gibi cikis kanal sayisi.
namespace {
ma_format ma_fmt(SampleFormat f) { return f == SampleFormat::S16 ? ma_format_s16 : ma_format_f32; }
void set_samples(Clip *c, void *buf, SampleFormat f) {
  if (f == SampleFormat::S16) c->samples16 = (const int16_t *)buf; else c->samples = (const float *)buf;
}
} // namespace

bool clip_load(Arena &arena, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out, char *err, uint32_t err_n,
               SampleFormat fmt) {
  *out = Clip{};
  ma_decoder dec;
  // Once kaynagin KENDI kanal sayisiyla ac (0 = dogal); mono degilse cikisa gore yeniden ac.
  ma_decoder_config dc0 = ma_decoder_config_init(ma_fmt(fmt), 0, out_rate);
  ma_result r = ma_decoder_init_file(path, &dc0, &dec);
  if (r != MA_SUCCESS) { if (err) std::snprintf(err, err_n, "decoder: %s (%s)", ma_result_description(r), path); return false; }
  if (dec.outputChannels != 1 && dec.outputChannels != out_channels) {
    ma_decoder_uninit(&dec);
    ma_decoder_config dc = ma_decoder_config_init(ma_fmt(fmt), out_channels, out_rate);
    r = ma_decoder_init_file(path, &dc, &dec);
    if (r != MA_SUCCESS) { if (err) std::snprintf(err, err_n, "decoder: %s (%s)", ma_result_description(r), path); return false; }
  }
  out_channels = dec.outputChannels;
  ma_uint64 len = 0;
  if (ma_decoder_get_length_in_pcm_frames(&dec, &len) != MA_SUCCESS || len == 0) {
    ma_decoder_uninit(&dec);
    if (err) std::snprintf(err, err_n, "uzunluk bilinmiyor (akis kaynaklari sonraki dilim): %s", path);
    return false;
  }
  const uint32_t n = (uint32_t)(len * out_channels);
  void *buf = fmt == SampleFormat::S16 ? (void *)arena.alloc_array<int16_t>(n) : (void *)arena.alloc_array<float>(n);
  if (!buf) { ma_decoder_uninit(&dec); if (err) std::snprintf(err, err_n, "arena dolu"); return false; }
  ma_uint64 got = 0;
  r = ma_decoder_read_pcm_frames(&dec, buf, len, &got);
  ma_decoder_uninit(&dec);
  if (r != MA_SUCCESS && r != MA_AT_END) { if (err) std::snprintf(err, err_n, "okuma: %s", ma_result_description(r)); return false; }
  set_samples(out, buf, fmt); out->frames = (uint32_t)got; out->channels = out_channels; out->rate = out_rate;
  return true;
}

bool clip_load_into(void *buf, uint32_t cap_frames, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out,
                    bool *too_long, char *err, uint32_t err_n, SampleFormat fmt) {
  *out = Clip{};
  if (too_long) *too_long = false;
  ma_decoder_config dc = ma_decoder_config_init(ma_fmt(fmt), out_channels, out_rate);
  ma_decoder dec;
  ma_result r = ma_decoder_init_file(path, &dc, &dec);
  if (r != MA_SUCCESS) { if (err) std::snprintf(err, err_n, "decoder: %s (%s)", ma_result_description(r), path); return false; }
  ma_uint64 len = 0;
  if (ma_decoder_get_length_in_pcm_frames(&dec, &len) != MA_SUCCESS || len == 0) {
    ma_decoder_uninit(&dec);
    if (err) std::snprintf(err, err_n, "uzunluk bilinmiyor: %s", path);
    return false;
  }
  if (len > cap_frames) {
    ma_decoder_uninit(&dec);
    if (too_long) *too_long = true;
    if (err) std::snprintf(err, err_n, "%.1f s, yuva en cok %.1f s", (double)len / out_rate, (double)cap_frames / out_rate);
    return false;
  }
  ma_uint64 got = 0;
  r = ma_decoder_read_pcm_frames(&dec, buf, len, &got);
  ma_decoder_uninit(&dec);
  if (r != MA_SUCCESS && r != MA_AT_END) { if (err) std::snprintf(err, err_n, "okuma: %s", ma_result_description(r)); return false; }
  set_samples(out, buf, fmt); out->frames = (uint32_t)got; out->channels = out_channels; out->rate = out_rate;
  return true;
}

bool clip_sine(Arena &arena, float hz, float seconds, uint32_t rate, float amplitude, Clip *out) {
  *out = Clip{};
  const uint32_t n = (uint32_t)(seconds * (float)rate);
  float *buf = arena.alloc_array<float>(n ? n : 1);
  if (!buf) return false;
  const float w = 2.0f * 3.14159265358979f * hz / (float)rate;
  for (uint32_t i = 0; i < n; i++) buf[i] = amplitude * std::sin(w * (float)i);
  out->samples = buf; out->frames = n; out->channels = 1; out->rate = rate;
  return true;
}

} // namespace tulpar::engine::audio
