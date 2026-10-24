// L'implementazione di miniaudio, con stb_vorbis per l'Ogg Vorbis: entrambi
// di pubblico dominio. stb_vorbis va incluso due volte, prima solo le
// dichiarazioni e dopo miniaudio il resto, come chiede miniaudio.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
