// The one stb_truetype implementation TU inside the framework library
// (third_party/README rule: the IMPL entry lives in a single wrapper TU).
// Compiled only when USE_TTF_RUNTIME is on, so targets that do not need
// runtime rasterization link none of stb_truetype (contract 2.4).
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
