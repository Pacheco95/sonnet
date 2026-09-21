// The one translation unit that compiles miniaudio, and stb_vorbis for its Ogg Vorbis decoder
// (docs/audio.md). miniaudio finds the decoder through stb_vorbis's header part, which therefore
// comes first; the implementation of stb_vorbis follows miniaudio's, as miniaudio documents.
// Both are third-party single-file libraries from vcpkg's include directory, which is a system
// one, so the engine's warnings do not apply to them. MSVC's code-generation warnings are the
// exception, since /external:W0 does not reach them: stb_vorbis's seek reads a variable its
// first probe always sets, which MSVC reports as possibly uninitialized (C4701).
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <stb_vorbis.c>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
