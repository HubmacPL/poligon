// The single translation unit that compiles the stb_dxt implementation (block compression
// of textures: BC1, BC3, BC4, BC5).
#include <cstring>  // stb_dxt uses memcpy without including it
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
