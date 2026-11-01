#pragma once
#include "eyeshine_fix.h"
struct EyeshineConversion {
    bool valid=false;
    GLenum func=0;
    GLint ref=0;
    GLuint mask=0;
    GLfloat alpha=0;
};
bool configureEyeshineMask(HMODULE gl) noexcept;
// Replay an observed constant-alpha stencil conversion, preserving RGB,
// stencil, matrices, samplers, and every server attribute.
bool rebuildEyeshineMask(const EyeshineRect& region,const EyeshineConversion& conversion) noexcept;
