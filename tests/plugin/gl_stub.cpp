#include <windows.h>
#include <gl/GL.h>
#include <cstring>
#include <string>

namespace {
thread_local ULONG_PTR context = 1;
GLuint query = 0, attribute = 0;
GLuint boundProgram = 0;
bool recreated = false;
BOOL swapResult = TRUE;
GLint viewport[4]{0, 0, 320, 200};
std::string lastProgram;
volatile unsigned traceCalls = 0;
bool insideBegin = false;
bool aliasColor = false;
GLfloat currentColor[4]{1, 1, 1, 1}, currentAttrib3[4]{0, 0, 0, 1};
unsigned illegalQueries = 0;
bool shadowFixture = false;
GLenum activeTexture = 0x84C2;
GLuint textureBindings[32]{};
unsigned copies = 0;
GLint depthMinFilter = GL_LINEAR;
GLint maskMinFilter = GL_LINEAR;
unsigned filterWrites = 0;
void WINAPI activeTextureProc(GLenum unit) { if(insideBegin) ++illegalQueries; activeTexture=unit; }
void WINAPI getProgramProc(GLenum,GLenum,GLint* id) { if(insideBegin) ++illegalQueries; *id=boundProgram; }
void WINAPI getEnvProc(GLenum,GLuint index,GLfloat* v) {
    if(insideBegin) ++illegalQueries;
    if(shadowFixture) { v[0]=1.0f/2048;v[1]=1.0f/1024;v[2]=v[3]=0; }
    else { v[0]=float(index);v[1]=2;v[2]=3;v[3]=4; }
}
void WINAPI program(GLenum, GLenum, GLsizei length, const void* source) { lastProgram.assign(static_cast<const char*>(source), length); }
void WINAPI beginQuery(GLenum target, GLuint id) { query = target == 0x8914 ? id : 0; }
void WINAPI beginQueryOtherContext(GLenum target, GLuint id) { query = target == 0x8914 ? id + 100 : 0; }
void WINAPI endQuery(GLenum target) { if (target == 0x8914) query = 0; }
void WINAPI genQueries(GLsizei n, GLuint* ids) { for (GLsizei i = 0; i < n; ++i) ids[i] = 17 + i; }
void WINAPI deleteQueries(GLsizei n, const GLuint* ids) { if (n == 1) query = ids[0]; }
GLboolean WINAPI isQuery(GLuint id) { return id == 17 ? GL_TRUE : GL_FALSE; }
void WINAPI queryIv(GLuint id, GLenum name, GLint* out) { *out = id == 7 && name == 0x8866 ? 19 : -1; }
void WINAPI queryUiv(GLuint id, GLenum name, GLuint* out) { *out = id == 8 && name == 0x8867 ? 23 : 0; }
void WINAPI vertexAttrib(GLuint index, GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    attribute = index;
    if (index == 3) {
        const GLfloat value[]{r, g, b, a};
        std::memcpy(currentAttrib3, value, sizeof(value));
        if (aliasColor) std::memcpy(currentColor, value, sizeof(value));
    }
}
void WINAPI bindProgram(GLenum, GLuint id) { boundProgram=id; }
void WINAPI bindProgramOtherContext(GLenum, GLuint id) { boundProgram=id+100; }
}
extern "C" {
__declspec(dllexport) void bigfixAliasColor(BOOL alias) { aliasColor = alias != FALSE; }
__declspec(dllexport) void bigfixAttrib3(GLfloat* value) { std::memcpy(value, currentAttrib3, sizeof(currentAttrib3)); }
__declspec(dllexport) void bigfixShadowFixture() {
    shadowFixture=true;activeTexture=0x84C3;
    for(unsigned i=0;i<32;++i) textureBindings[i]=100+i;
}
__declspec(dllexport) unsigned bigfixCopyCalls() { return copies; }
__declspec(dllexport) GLint bigfixDepthMinFilter() { return depthMinFilter; }
__declspec(dllexport) unsigned bigfixFilterWrites() { return filterWrites; }
void WINAPI stubTexParameteri(GLenum target,GLenum name,GLint value) {
    if (insideBegin) ++illegalQueries;
    if (target==GL_TEXTURE_2D && name==GL_TEXTURE_MIN_FILTER) {
        const auto texture=textureBindings[activeTexture-0x84C0];
        if (texture==5133) depthMinFilter=value;
        if (texture==5134) maskMinFilter=value;
        ++filterWrites;
    }
}
__declspec(dllexport) GLenum bigfixActiveTexture() { return activeTexture; }
HDC WINAPI stubGetCurrentDC() { return nullptr; }
void WINAPI stubGetTexLevelParameteriv(GLenum,GLint,GLenum n,GLint* v) { if(insideBegin) ++illegalQueries; *v=n==GL_TEXTURE_INTERNAL_FORMAT?GL_RGBA8:shadowFixture?(n==GL_TEXTURE_WIDTH?2048:1024):256; }
void WINAPI stubGetTexParameteriv(GLenum,GLenum name,GLint* v) {
    if(insideBegin) ++illegalQueries;
    const auto texture=textureBindings[activeTexture-0x84C0];
    *v=name==GL_TEXTURE_MIN_FILTER?(texture==5133?depthMinFilter:texture==5134?maskMinFilter:GL_LINEAR):
        name==GL_TEXTURE_MAG_FILTER?(texture==5134?GL_LINEAR:GL_NEAREST):0x812F;
}
__declspec(noinline) void WINAPI stubCopyTexSubImage2D(GLenum,GLint,GLint,GLint,GLint,GLint,GLsizei,GLsizei) { ++copies;traceCalls+=25; }
__declspec(noinline) void WINAPI stubCopyTexImage2D(GLenum,GLint,GLenum,GLint,GLint,GLsizei,GLsizei,GLint) { ++copies;traceCalls+=26; }
__declspec(noinline) void WINAPI stubCopyPixels(GLint,GLint,GLsizei,GLsizei,GLenum) { ++copies;traceCalls+=27; }
__declspec(noinline) void WINAPI stubDrawBuffer(GLenum) { traceCalls+=28; }
__declspec(noinline) void WINAPI stubReadBuffer(GLenum) { traceCalls+=29; }
__declspec(dllexport) void bigfixSetSwapResult(BOOL value) { swapResult = value; }
__declspec(dllexport) void bigfixSetViewport(int w, int h) { viewport[2] = w; viewport[3] = h; }
__declspec(dllexport) unsigned bigfixIllegalQueries() { return illegalQueries; }
__declspec(dllexport) unsigned bigfixTraceCalls() { return traceCalls; }
__declspec(dllexport) GLuint bigfixBoundProgram() { return boundProgram; }
void WINAPI stubGetFloatv(GLenum name,GLfloat* value) {
    if (insideBegin) ++illegalQueries;
    if (name == GL_CURRENT_COLOR) { std::memcpy(value, currentColor, sizeof(currentColor)); return; }
    if(name==GL_DEPTH_RANGE) { value[0]=0;value[1]=1;return; }
    if(name==GL_DEPTH_SCALE || name==GL_DEPTH_BIAS) { *value=name==GL_DEPTH_SCALE?1.0f:0.0f;return; }
    value[0]=value[1]=value[2]=0;value[3]=1;
}
void WINAPI stubGetBooleanv(GLenum,GLboolean* value) { value[0]=value[1]=value[2]=value[3]=GL_TRUE; }
GLboolean WINAPI stubIsEnabled(GLenum n) { if(insideBegin) ++illegalQueries; return n==0x8804 && (shadowFixture || boundProgram) ? GL_TRUE : GL_FALSE; }
__declspec(noinline) void WINAPI stubViewport(GLint x, GLint y, GLsizei w, GLsizei h) { viewport[0]=x; viewport[1]=y; viewport[2]=w; viewport[3]=h; traceCalls+=1; }
__declspec(noinline) void WINAPI stubScissor(GLint, GLint, GLsizei, GLsizei) { traceCalls+=2; }
__declspec(noinline) void WINAPI stubMatrixMode(GLenum) { traceCalls+=3; }
__declspec(noinline) void WINAPI stubLoadMatrixf(const GLfloat*) { traceCalls+=4; }
__declspec(noinline) void WINAPI stubLoadMatrixd(const GLdouble*) { traceCalls+=5; }
__declspec(noinline) void WINAPI stubMultMatrixf(const GLfloat*) { traceCalls+=6; }
__declspec(noinline) void WINAPI stubMultMatrixd(const GLdouble*) { traceCalls+=7; }
__declspec(noinline) void WINAPI stubLoadIdentity() { traceCalls+=8; }
__declspec(noinline) void WINAPI stubOrtho(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble) { traceCalls+=9; }
__declspec(noinline) void WINAPI stubClear(GLbitfield) { traceCalls+=10; }
__declspec(noinline) void WINAPI stubTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*) { traceCalls+=11; }
__declspec(noinline) void WINAPI stubTexSubImage2D(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*) { traceCalls+=12; }
__declspec(noinline) void WINAPI stubBindTexture(GLenum target,GLuint texture) {
    if (shadowFixture && target==GL_TEXTURE_2D && activeTexture>=0x84C0 && activeTexture<0x84E0)
        textureBindings[activeTexture-0x84C0]=texture;
    traceCalls+=13;
}
__declspec(noinline) void WINAPI stubBegin(GLenum) { insideBegin=true; traceCalls+=14; }
__declspec(noinline) void WINAPI stubEnd() { insideBegin=false; traceCalls+=15; }
__declspec(noinline) void WINAPI stubDrawArrays(GLenum,GLint,GLsizei) { traceCalls+=16; }
__declspec(noinline) void WINAPI stubDrawElements(GLenum,GLsizei,GLenum,const void*) { traceCalls+=17; }
__declspec(noinline) void WINAPI stubVertex2f(GLfloat,GLfloat) { traceCalls+=18; }
__declspec(noinline) void WINAPI stubVertex3f(GLfloat,GLfloat,GLfloat) { traceCalls+=19; }
__declspec(noinline) void WINAPI stubVertex2i(GLint,GLint) { traceCalls+=20; }
__declspec(noinline) void WINAPI stubVertex3fv(const GLfloat*) { traceCalls+=21; }
__declspec(noinline) void WINAPI stubTexCoord2f(GLfloat,GLfloat) { traceCalls+=22; }
__declspec(noinline) void WINAPI stubVertexPointer(GLint,GLenum,GLsizei,const void*) { traceCalls+=23; }
__declspec(noinline) void WINAPI stubTexCoordPointer(GLint,GLenum,GLsizei,const void*) { traceCalls+=24; }
__declspec(noinline) BOOL WINAPI stubSwapBuffers(HDC) { SetLastError(1234); return swapResult; }
__declspec(noinline) void WINAPI stubCallList(GLuint) { traceCalls+=25; }
void WINAPI stubReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) {}
__declspec(dllexport) void bigfixSetContext(ULONG_PTR value) { context = value; }
__declspec(dllexport) GLuint bigfixQuery() { return query; }
__declspec(dllexport) GLuint bigfixAttribute() { return attribute; }
__declspec(dllexport) const char* bigfixProgram() { return lastProgram.c_str(); }
const GLubyte* WINAPI stubGetString(GLenum name) {
    static const std::string longList = std::string(20000, 'x') + " GL_EXT_bgra GL_ARB_multitexture_foo";
    if (!context) return nullptr;
    if (name == GL_EXTENSIONS) return reinterpret_cast<const GLubyte*>(context == 1 ?
        (recreated ? "GL_NV_fence" : longList.c_str()) : "GL_ARB_multitexture");
    return reinterpret_cast<const GLubyte*>("BigFix fake GL");
}
void WINAPI stubGetIntegerv(GLenum name, GLint* data) {
    if (insideBegin) ++illegalQueries;
    if (shadowFixture) {
        if (name==0x84E0) { *data=activeTexture;return; }
        if (name==GL_TEXTURE_BINDING_2D) { *data=textureBindings[activeTexture-0x84C0];return; }
        if (name==0x8514) { *data=0;return; }
    }
    if (name == GL_VIEWPORT) { std::memcpy(data, viewport, sizeof(viewport)); return; }
    if (name == GL_SCISSOR_BOX) { data[0]=data[1]=0;data[2]=1920;data[3]=1080;return; }
    if (name == 0x84E0) { *data=activeTexture; return; }
    if (name == GL_TEXTURE_BINDING_2D) { *data=10+activeTexture-0x84C0; return; }
    *data = name == 0x8872 ? 32 : -1;
}
void WINAPI stubColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    const GLfloat value[]{r, g, b, a};
    std::memcpy(currentColor, value, sizeof(value));
    if (aliasColor) std::memcpy(currentAttrib3, value, sizeof(value));
}
HGLRC WINAPI stubGetCurrentContext() { return reinterpret_cast<HGLRC>(context); }
BOOL WINAPI stubDeleteContext(HGLRC value) {
    if (!value) return FALSE;
    if (reinterpret_cast<ULONG_PTR>(value) == 1) recreated = true;
    if (reinterpret_cast<ULONG_PTR>(value) == context) context = 0;
    return TRUE;
}
PROC WINAPI stubGetProcAddress(LPCSTR name) {
    if (!std::strcmp(name,"glActiveTextureARB")) return reinterpret_cast<PROC>(&activeTextureProc);
    if (!std::strcmp(name,"glGetProgramivARB")) return reinterpret_cast<PROC>(&getProgramProc);
    if (!std::strcmp(name,"glGetProgramEnvParameterfvARB")) return reinterpret_cast<PROC>(&getEnvProc);
    if (!std::strcmp(name,"glBindProgramARB")) return reinterpret_cast<PROC>(context==2 ? &bindProgramOtherContext : &bindProgram);
    if (!std::strcmp(name, "glProgramStringARB")) return reinterpret_cast<PROC>(&program);
    if (!std::strcmp(name, "glVertexAttrib4f")) return reinterpret_cast<PROC>(&vertexAttrib);
    if (!std::strcmp(name, "glBeginQueryARB")) return reinterpret_cast<PROC>(context == 2 ? &beginQueryOtherContext : &beginQuery);
    if (!std::strcmp(name, "glEndQueryARB")) return reinterpret_cast<PROC>(&endQuery);
    if (!std::strcmp(name, "glGenQueriesARB")) return reinterpret_cast<PROC>(&genQueries);
    if (!std::strcmp(name, "glDeleteQueriesARB")) return reinterpret_cast<PROC>(&deleteQueries);
    if (!std::strcmp(name, "glIsQueryARB")) return reinterpret_cast<PROC>(&isQuery);
    if (!std::strcmp(name, "glGetQueryObjectivARB")) return reinterpret_cast<PROC>(&queryIv);
    if (!std::strcmp(name, "glGetQueryObjectuivARB")) return reinterpret_cast<PROC>(&queryUiv);
    return nullptr;
}
}
