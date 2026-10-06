#include "Gl.hpp"

#include <SDL3/SDL.h>

namespace gl
{
#define HS_GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
    HS_GL_FUNCTIONS(HS_GL_DEFINE)
#undef HS_GL_DEFINE

    bool Load()
    {
        bool ok = true;
#define HS_GL_LOAD(ret, name, args) \
        name = reinterpret_cast<PFN_##name>(SDL_GL_GetProcAddress("gl" #name)); \
        if (!name) { SDL_Log("OpenGL function gl%s not found", #name); ok = false; }
        HS_GL_FUNCTIONS(HS_GL_LOAD)
#undef HS_GL_LOAD
        return ok;
    }
}
