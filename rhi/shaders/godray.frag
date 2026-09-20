#version 450
// GPU Gems 3 & Prosedurel Atmosferik Isik Huzmeleri (Volumetric God Rays).
// Sahne arkasinda parlak gokyuzu olmasa bile gercekci, keskin ve organik
// volumetrik gunes huzmeleri (crepuscular rays) uretir. Ayrica ekranda isimali
// parlak nesneler oldugunda gercek radyal smear biriktirmesini korur.
layout(location = 0) in vec2 v_uv;
layout(set = 0, binding = 0) uniform sampler2D u_src;  // parlak gecis (down[0])
layout(set = 0, binding = 1) uniform sampler2D u_src2; // bloom gecisi (up[0])
layout(push_constant) uniform Push {
  vec4 texel; // xy: 1/kaynak0 olcusu, zw: 1/kaynak1 olcusu
  vec4 p;     // xy: gunesin ekran UV'si, z: huzme uzunlugu (density), w: keskinlik (decay)
  vec4 q;     // x: huzme siddeti (weight), y: genel carpan (exposure), z: ornek sayisi, w: zaman (time_sec)
} pc;
layout(location = 0) out vec4 o_color;

void main() {
  vec2 sun_uv = pc.p.xy;
  float density = max(pc.p.z, 0.05);        // Huzme uzunlugu (tipik 0.5 - 2.0)
  float decay = clamp(pc.p.w, 0.01, 0.999); // Keskinlik (tipik 0.8 - 0.99)
  float weight = max(pc.q.x, 0.0);          // Huzme siddeti (tipik 0.1 - 1.0)
  float exposure = max(pc.q.y, 0.0);        // Genel carpan (kamera arkasi ve edge_fade icerir)
  int num_samples = clamp(int(pc.q.z + 0.5), 1, 64);
  float time_sec = pc.q.w;                  // Canli acisal kaydirma (zaman)

  vec2 ray_diff = v_uv - sun_uv;
  // Ekran en-boy oranini hesaba katarak dairesel/radyal huzmelerin ezilmesini onle
  float aspect_ratio = (pc.texel.y > 0.00001) ? (pc.texel.x / pc.texel.y) : 1.0;
  vec2 aspect_diff = vec2(ray_diff.x, ray_diff.y * aspect_ratio);
  float dist = length(aspect_diff);

  // 1. Prosedurel Aci ve Zaman Kaydirmasi:
  float angle = atan(aspect_diff.y, aspect_diff.x);
  float shifted_angle = angle + time_sec * 0.04;

  // 2. Cok Frekansli Huzme Modulasyonu:
  // Ana huzmeler ve dogal ince alt huzmeler
  float s1 = sin(shifted_angle * 14.0);
  float s2 = sin(shifted_angle * 28.0 + 1.3);
  float s3 = sin(shifted_angle * 56.0 + 2.7);
  float s4 = sin(shifted_angle * 7.0 - 0.8);
  float raw_shaft = s1 * 0.48 + s2 * 0.26 + s3 * 0.14 + s4 * 0.12;

  // Aradaki bosluklar tamamen SIFIRA inmeli (huzme hissi buradan gelir)
  float shaft_base = max(0.0, raw_shaft);

  // 3. Keskinlestirme (decay parametresi keskinligi kontrol eder):
  // decay yuksekken (0.95+) jilet gibi keskin, dar huzmeler; dusukken daha yumusak yayilim
  float sharpness = mix(1.5, 12.0, clamp((decay - 0.4) * 1.8, 0.0, 1.0));
  float shaft_factor = pow(shaft_base, sharpness);

  // 4. Mesafe Sonumlemesi (density parametresi huzme uzunlugunu kontrol eder):
  // Buyuk density = huzmeler daha uzaga ulasir; kucuk density = gunesin yaninda soner
  float falloff_k = 3.2 / density;
  float dist_atten = exp(-dist * falloff_k);

  // Gunes cevresi hafif tac parlamasi (corona glow)
  float corona = exp(-dist * 14.0) * 0.35;

  // Sicak atmosferik gunes rengi tonu (hafif altinsi / gunes beyazi)
  const vec3 kSunColor = vec3(1.0, 0.94, 0.84);
  vec3 proc_shafts = kSunColor * ((shaft_factor + corona) * dist_atten * (weight * 2.5));

  // 5. Orijinal Radyal Biriktirme (Isimali/parlak geometrilerin smear yapmasi icin):
  vec2 delta = ray_diff * (density * 0.8) / float(num_samples);
  float dither = fract(sin(dot(v_uv, vec2(12.9898, 78.233))) * 43758.5453);
  vec2 uv = v_uv - delta * dither;

  float illum_decay = 1.0;
  vec3 accum = vec3(0.0);
  for (int i = 0; i < num_samples; i++) {
    uv -= delta;
    vec3 s = (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999) 
               ? vec3(0.0) 
               : texture(u_src, uv).rgb;
    accum += s * (illum_decay * weight);
    illum_decay *= decay;
  }

  // 6. Nihai Birlestirme:
  // exposure: C++ tarafinda hesaplanan genel carpan (gunes kameranin arkasindaysa 0,
  // kenarlarda edge_fade ile yumusatilmis). Hem prosedurel huzmeleri hem radyal accum'u carpar.
  vec3 bloom = texture(u_src2, v_uv).rgb;
  vec3 total_godrays = (accum + proc_shafts) * exposure;
  o_color = vec4(bloom + total_godrays, 1.0);
}
