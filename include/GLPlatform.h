#pragma once

// Single include point for OpenGL. On Vita this is VitaGL; on desktop it is the
// system's legacy (compatibility profile) OpenGL, which offers the same fixed-function
// API the renderer uses.

#ifdef __vita__
#include <vitaGL.h>
#else
#include <SDL2/SDL_opengl.h>
// Desktop GL has no float variant of glOrtho.
#define glOrthof glOrtho
#endif
