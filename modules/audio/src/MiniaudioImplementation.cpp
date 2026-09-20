// The one translation unit that compiles miniaudio, and stb_vorbis for its Ogg Vorbis decoder
// (docs/audio.md). miniaudio finds the decoder through stb_vorbis's header part, which therefore
// comes first; the implementation of stb_vorbis follows miniaudio's, as miniaudio documents.
// Both are third-party single-file libraries from vcpkg's include directory, which is a system
// one, so the engine's warnings do not apply to them.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
