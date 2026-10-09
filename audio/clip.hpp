// L3 AUDIO — klip yukleme: WAV/FLAC/MP3 (miniaudio decoder) -> PCM float Arena'da,
// hedef hiza/kanala donusturulmus. Sentetik sinus: test ve demo (dosya gerekmez).
#pragma once
#include "audio/mixer.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::audio {

bool clip_load(Arena &arena, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out, char *err, uint32_t err_n);
bool clip_sine(Arena &arena, float hz, float seconds, uint32_t rate, float amplitude, Clip *out);
// Sabit tampona coz (muzik yuvasi, M3b): `buf` en az cap_frames x out_channels float.
// Kaynak cap_frames'ten uzunsa HICBIR SEY yazilmaz, *too_long = true, false doner.
bool clip_load_into(float *buf, uint32_t cap_frames, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out,
                    bool *too_long, char *err, uint32_t err_n);

} // namespace tulpar::engine::audio
