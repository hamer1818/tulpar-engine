/* kb_text_shape govdesi (C, uyarilar kapali: -w). KB_TEXT_SHAPE_NO_CRT: varsayilan ayirici 0 dondurur ve stdio
   yok — kutuphane YALNIZ content/text_shape.cpp'nin verdigi (sayan) ayiriciyi kullanabilir. */
#include <string.h>
#define KB_TEXT_SHAPE_NO_CRT
#define KBTS_MEMSET memset
#define KBTS_MEMCPY memcpy
#define KB_TEXT_SHAPE_IMPLEMENTATION
#include "kb_text_shape.h"
