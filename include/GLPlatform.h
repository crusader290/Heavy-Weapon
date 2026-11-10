#pragma once

// Single include point for OpenGL: the system's legacy (compatibility profile) OpenGL,
// whose fixed-function API the renderer uses.

#include <SDL3/SDL_opengl.h>
// Desktop GL has no float variant of glOrtho.
#define glOrthof glOrtho
