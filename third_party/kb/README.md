# kb_text_shape (vendored)

`kb_text_shape.h` — Unicode metin bölütleme (bidi, betik, satır, sözcük) + OpenType shaping (GSUB/GPOS; Arapça,
Hint, Güneydoğu Asya betikleri, bitişikler). Zlib (LICENSE). Kaynak: github.com/JimmyLefevre/kb, **etiket yok**,
commit `4eec43b4c753a42b64aa8d7fd55dbce16a70343c` (2026-11-14), sha256
`de2619d986753a643c8243855592e91e30613816c3944e08f8104868d7e789fd`. Değiştirilmeden.

Gövde `kb_text_shape_impl.c`'de, `KB_TEXT_SHAPE_NO_CRT` ile: varsayılan ayırıcı yok, stdio yok. Kullanan tek yer
`content/text_shape.cpp` (katman: content). Güncelleme: aynı dosyayı yeni commit'ten al, bu satırları güncelle,
`./yapi/engine_tests shaping` koştur.
