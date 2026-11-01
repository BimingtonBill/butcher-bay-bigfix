#include "eyeshine_mask.h"
#include <algorithm>
#include <cstdint>
namespace {
HMODULE module=nullptr;
template<class T> T get(const char* name) {return reinterpret_cast<T>(GetProcAddress(module,name));}
#define GL_API(X) X(glGetIntegerv) X(glPushAttrib) X(glPopAttrib) X(glEnable) X(glDisable) X(glColorMask) X(glClearColor) X(glClear) X(glStencilFunc) X(glStencilMask) X(glStencilOp) X(glMatrixMode) X(glPushMatrix) X(glPopMatrix) X(glLoadIdentity) X(glColor4ub) X(glRectf) X(glPolygonMode) X(glViewport) X(glScissor) X(wglGetProcAddress)
#define DECLARE(name) decltype(&name) call_##name=nullptr;
GL_API(DECLARE)
#undef DECLARE
GLint integer(GLenum name) {GLint v=0;call_glGetIntegerv(name,&v);return v;}
bool ready=false;
}
bool configureEyeshineMask(HMODULE gl) noexcept {
    module=gl;ready=true;
#define RESOLVE(name) call_##name=get<decltype(call_##name)>(#name);ready=ready && call_##name;
    GL_API(RESOLVE)
#undef RESOLVE
    return ready;
}
bool rebuildEyeshineMask(const EyeshineRect& r,const EyeshineConversion& c) noexcept {
    if(!ready || !c.valid || r[2]<=0 || r[3]<=0) return false;
    const auto p=call_wglGetProcAddress("glActiveTextureARB");const auto address=reinterpret_cast<std::uintptr_t>(p);
    if(address<=3 || address==~std::uintptr_t(0)) return false;
    const auto activate=reinterpret_cast<void(WINAPI*)(GLenum)>(p);
    constexpr GLenum activeTexture=0x84E0,texture0=0x84C0;
    const auto matrix=integer(GL_MATRIX_MODE),unit=integer(activeTexture);
    call_glPushAttrib(GL_ALL_ATTRIB_BITS);
    const auto units=(std::min)(8,(std::max)(1,integer(0x8871)));
    for(int n=0;n<units;++n) {
        activate(texture0+n);
        for(auto cap:{GLenum(GL_TEXTURE_1D),GLenum(GL_TEXTURE_2D),GLenum(0x806F),GLenum(0x8513)}) call_glDisable(cap);
    }
    activate(unit);
    for(auto cap:{GLenum(GL_ALPHA_TEST),GLenum(GL_BLEND),GLenum(GL_DEPTH_TEST),GLenum(GL_COLOR_LOGIC_OP),GLenum(GL_DITHER),GLenum(0x8804),GLenum(0x8620),GLenum(GL_LIGHTING),GLenum(GL_FOG),GLenum(GL_CULL_FACE)}) call_glDisable(cap);
    for(int n=0;n<6;++n) call_glDisable(GL_CLIP_PLANE0+n);
    call_glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    call_glViewport(r[0],r[1],r[2],r[3]);call_glScissor(r[0],r[1],r[2],r[3]);call_glEnable(GL_SCISSOR_TEST);
    call_glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);
    call_glDisable(GL_STENCIL_TEST);call_glClearColor(0,0,0,1-c.alpha);call_glClear(GL_COLOR_BUFFER_BIT);
    call_glEnable(GL_STENCIL_TEST);call_glStencilFunc(c.func,c.ref,c.mask);call_glStencilMask(0);call_glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
    call_glMatrixMode(GL_PROJECTION);call_glPushMatrix();call_glLoadIdentity();
    call_glMatrixMode(GL_MODELVIEW);call_glPushMatrix();call_glLoadIdentity();
    call_glColor4ub(255,255,255,c.alpha==0?0:255);call_glRectf(-1,-1,1,1);
    call_glPopMatrix();call_glMatrixMode(GL_PROJECTION);call_glPopMatrix();call_glMatrixMode(matrix);
    call_glPopAttrib();
    return true;
}
