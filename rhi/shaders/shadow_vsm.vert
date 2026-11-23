#version 450
// Golge haritasi, VSM SAYFASI (Faz 9, 2026-11-23): yalniz derinlik. Sayfa matrisi CPU'da modelle carpilir
// (renderer::vsm_page_view_proj * model) ve push sabitinin model alanina konur — kare UBO'su ve kademe
// indeksi YOK, cunku her sayfanin kendi ortografik izdusumu var ve bir karede yuzlerce sayfa cizilebilir.
// Push duzeni shadow.vert ile AYNI (renderer'in boru hatti duzeniyle uyumlu kalsin diye).
layout(location = 0) in vec3 in_pos;
layout(push_constant) uniform Push { mat4 model; vec4 color; uvec4 skin; } pc;
void main() { gl_Position = pc.model * vec4(in_pos, 1.0); }
