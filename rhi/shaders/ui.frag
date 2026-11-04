#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in float v_encode;
layout(set = 1, binding = 0) uniform sampler2D u_atlas; // glif atlasi (beyaz + alfa); duz kutu icin beyaz texel
layout(location = 0) out vec4 o_color;
// UI rengi sRGB algisal verilir; hedef dogrusal (SRGB bicim donanimda kodlar).
// Kaplama (atlas) UNORM'dur, oldugu gibi carpilir; karisim dogrusal uzayda.
vec3 srgb_to_linear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 linear_to_srgb(vec3 c) {
  c = clamp(c, 0.0, 1.0);
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
// v_encode = kodla (0/1) + 2 * titret (E1c, 2026-11-04; Renderer::set_ui_dither). Titretme alfayi +-0.5/255
// oynatir (Jimenez 2014 IGN, ekran pikseline bagli): harmanin sonucu en cok yarim nicem adimi kayar, genis
// dusuk alfali gecislerde 8 bitlik hedefin halkalari kirilir. Alfa 0 olan piksele dokunmaz (bos alan bos kalir).
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
  vec4 c = vec4(srgb_to_linear(v_color.rgb), v_color.a) * texture(u_atlas, v_uv);
  if (v_encode > 1.5 && c.a > 0.0) c.a = clamp(c.a + (ign(gl_FragCoord.xy) - 0.5) / 255.0, 0.0, 1.0);
  if (mod(v_encode, 2.0) > 0.5) c.rgb = linear_to_srgb(c.rgb);
  o_color = c;
}
