/* Resolves the console renderer's OpenGL entry points. */
#include "nack_gl.h"
#include "../nack_internal.h"

#define NACK_GL_DEFINE(ret, name, args) nack_pfn_gl##name nack_gl##name;
NACK_GL_FUNCTIONS(NACK_GL_DEFINE)
#undef NACK_GL_DEFINE

bool nack__gl_load(const char **error)
{
    const char *missing = NULL;

#define NACK_GL_RESOLVE(ret, name, args)                                     \
    nack_gl##name = (nack_pfn_gl##name)state.gl_get_proc_address("gl" #name); \
    if (!nack_gl##name && !missing)                                          \
        missing = "gl" #name;
    NACK_GL_FUNCTIONS(NACK_GL_RESOLVE)
#undef NACK_GL_RESOLVE

    if (missing) {
        if (error)
            *error = missing;
        return false;
    }
    return true;
}
