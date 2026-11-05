/* Uçüncü parti yükleyicilerin tek TU'daki gövdeleri (C, uyarılar kapalı:
   -w). Motor kodu yalnız başlıkları görür. Yalnız YÜKLEME anında malloc. */
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
/* Yalniz PNG yazimi (oyun fabrikasi ikonu, app/editor_fabrika.cpp; arac yolu, kare disi). */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
