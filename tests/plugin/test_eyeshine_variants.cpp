// Native GL fixture only: never loads or runs a game module.
#include "eyeshine_variants.h"
#include "shader_rewrite.h"
#include <windows.h>
#include <gl/GL.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
template<class T> T proc(const char* name) {return reinterpret_cast<T>(wglGetProcAddress(name));}
struct State {
    GLint viewport[4]{},scissor[4]{},matrix=0,stencilMask=0;
    GLfloat color[4]{},clear[4]{},zoomX=0,zoomY=0,model[16]{},projection[16]{};
    GLboolean mask[4]{},blend=false,alpha=false,scissorOn=false;
    State() {
        glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
        glGetIntegerv(GL_MATRIX_MODE,&matrix);glGetIntegerv(GL_STENCIL_WRITEMASK,&stencilMask);
        glGetFloatv(GL_CURRENT_COLOR,color);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
        glGetFloatv(GL_ZOOM_X,&zoomX);glGetFloatv(GL_ZOOM_Y,&zoomY);
        glGetFloatv(GL_MODELVIEW_MATRIX,model);glGetFloatv(GL_PROJECTION_MATRIX,projection);
        glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        blend=glIsEnabled(GL_BLEND);alpha=glIsEnabled(GL_ALPHA_TEST);scissorOn=glIsEnabled(GL_SCISSOR_TEST);
    }
    void verify() {
        State after;
        check(matrix==after.matrix && stencilMask==after.stencilMask,"matrix mode/stencil mask leaked");
        check(blend==after.blend && alpha==after.alpha && scissorOn==after.scissorOn,"enable state leaked");
        check(zoomX==after.zoomX && zoomY==after.zoomY,"pixel zoom leaked");
        for(int i=0;i<4;++i) {
            check(viewport[i]==after.viewport[i] && scissor[i]==after.scissor[i],"viewport/scissor leaked");
            check(color[i]==after.color[i] && clear[i]==after.clear[i] && mask[i]==after.mask[i],"color state leaked");
        }
        for(int i=0;i<16;++i) check(model[i]==after.model[i] && projection[i]==after.projection[i],"matrix leaked");
    }
};
}
int wmain(int argc,wchar_t** argv) {
    try {
        check(argc==2,"variant argument");const int variant=_wtoi(argv[1]);
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"BigFixEyeshineFixture";cls.style=CS_OWNDC;
        check(RegisterClassW(&cls)!=0,"register hidden fixture");
        const auto window=CreateWindowW(cls.lpszClassName,L"",WS_POPUP,0,0,32,32,nullptr,nullptr,cls.hInstance,nullptr);
        check(window!=nullptr,"create hidden fixture");const auto dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;
        pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;pfd.cDepthBits=24;pfd.cStencilBits=8;
        const auto format=ChoosePixelFormat(dc,&pfd);check(format && SetPixelFormat(dc,format,&pfd),"pixel format");
        const auto context=wglCreateContext(dc);check(context && wglMakeCurrent(dc,context),"hidden context");
        const auto gl=GetModuleHandleW(L"opengl32.dll");Settings settings;settings.traceShadowPass=true;settings.eyeshineFixVariant=variant;
        configureEyeshineVariants(gl,settings);check(eyeshineVariantsEnabled(),"variant dispatch incomplete");
        check(eyeshineVariant()==variant,"initial variant");
        eyeshineVariantSwap(FALSE,true);check(eyeshineVariant()==variant,"failed swap cycles");
        eyeshineVariantSwap(TRUE,false);

        glViewport(0,0,32,32);glDisable(GL_SCISSOR_TEST);glStencilMask(255);glClearStencil(128);glClear(GL_STENCIL_BUFFER_BIT);
        glScissor(1,1,2,2);glEnable(GL_SCISSOR_TEST);glClearStencil(0);glStencilMask(127);
        State clearState;eyeshineVariantClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);clearState.verify();
        unsigned char stencil=255;glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(20,20,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);
        check(stencil==(variant==2?0:128),"full stencil clear policy");

        glPixelZoom(2,3);glEnable(GL_ALPHA_TEST);glEnable(GL_BLEND);glColorMask(0,1,0,1);
        State packedState;
        const auto restored=eyeshineVariantBeforePixels(0x886E);
        if(restored) {
            GLfloat zoom=0;glGetFloatv(GL_ZOOM_X,&zoom);check(zoom==1,"packed copy zoom not normalized");
            check(!glIsEnabled(GL_ALPHA_TEST) && !glIsEnabled(GL_BLEND),"packed fragment tests not neutralized");
        } else if(variant==1) std::cout<<"packed-copy probe skipped: WindowPos unavailable\n";
        eyeshineVariantAfterPixels(restored,0,0,32,32,0x886E);packedState.verify();

        using Activate=void(WINAPI*)(GLenum);
        const auto activate=proc<Activate>("glActiveTextureARB");
        if(activate) {
            GLuint textures[2]{};glGenTextures(2,textures);
            for(auto texture:textures) {
                glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            }
            // NV packed copy itself is not executed. Its observed protocol arms
            // the production recognizer, then a distinct mask receives the copy.
            glBindTexture(GL_TEXTURE_2D,textures[0]);eyeshineVariantAfterPixels(false,0,0,32,32,0x886E);
            eyeshineVariantTexture(GL_TEXTURE_2D,0,0,0,0,0,32,32);
            glBindTexture(GL_TEXTURE_2D,textures[1]);
            // Observe an actual conversion recipe after the depth copy. This
            // makes the repair follow the pass, rather than assuming polarity.
            glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_DEPTH_TEST);glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL,128,255);glStencilMask(0);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);glColor4ub(0,0,0,0);
            eyeshineVariantDraw();
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SCISSOR_TEST);glStencilMask(255);glClearStencil(128);glClear(GL_STENCIL_BUFFER_BIT);
            glScissor(16,0,16,32);glEnable(GL_SCISSOR_TEST);glClearStencil(129);glClear(GL_STENCIL_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glClearColor(.2f,.3f,.4f,.7f);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);glEnable(GL_ALPHA_TEST);glColorMask(0,1,0,1);glScissor(1,2,3,4);glEnable(GL_SCISSOR_TEST);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glTranslatef(.1f,.2f,.3f);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glScalef(2,3,4);
            glMatrixMode(GL_TEXTURE);glColor4ub(13,17,19,23);
            State maskState;eyeshineVariantTexture(GL_TEXTURE_2D,0,0,16,0,8,24,16);maskState.verify();
            std::array<unsigned char,4> left{},right{};
            glReadPixels(8,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left.data());glReadPixels(20,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right.data());
            if(variant==3) check(left[3]==0 && right[3]==255,"observed stencil test alpha repair pixels");
            else check(std::abs(int(left[3])-179)<=1 && left[3]==right[3],"non-mask variant altered alpha");
            for(int i=0;i<3;++i) check(left[i]==right[i] && std::abs(int(left[i])-int((i+2)*25.5))<=1,"mask repair altered RGB");
            glReadPixels(20,12,1,1,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,&stencil);check(stencil==129,"mask repair altered stencil");
            if(variant==3) {
                glScissor(0,0,16,32);glEnable(GL_SCISSOR_TEST);glStencilMask(255);glClearStencil(127);glClear(GL_STENCIL_BUFFER_BIT);
                glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glEnable(GL_STENCIL_TEST);
                glStencilFunc(GL_LEQUAL,128,255);glStencilMask(0);glColor4ub(0,0,0,0);
                eyeshineVariantDraw();
                State thresholdState;eyeshineVariantTexture(GL_TEXTURE_2D,0,0,16,0,8,24,16);thresholdState.verify();
                glReadPixels(8,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left.data());glReadPixels(20,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right.data());
                check(left[3]==255 && right[3]==0,"per-light threshold recipe was inverted");
                glDisable(GL_STENCIL_TEST);
            }

            const std::string extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if(extensions.find("GL_ARB_fragment_program")!=extensions.npos) {
                using Bind=void(WINAPI*)(GLenum,GLuint);using Upload=void(WINAPI*)(GLenum,GLenum,GLsizei,const void*);
                using GetLocal=void(WINAPI*)(GLenum,GLuint,GLfloat*);
                const auto bind=proc<Bind>("glBindProgramARB");const auto upload=proc<Upload>("glProgramStringARB");
                const auto getLocal=proc<GetLocal>("glGetProgramLocalParameterfvARB");check(bind && upload && getLocal,"ARB fixture dispatch");
                bind(0x8804,123);
                const std::string source="!!ARBfp1.0\nOUTPUT oCol = result.color;\nTEMP smtc0;\nTEMP smtc1;\n"
                    "PARAM SMOfs0 = {0.5,1.5,0,0};\nPARAM PixelUV = program.env[4];\n"
                    "# ShadowMaskTexel NormalMapTexel texture[6]\nMOV smtc0, {0,0,0,1};\n"
                    "ADD smtc0.xy, smtc0, 0.000001;\nMAD smtc1.xy, SMOfs0, PixelUV, smtc0;\nMOV oCol, smtc1;\nEND";
                const auto compiled=eyeshineVariantProgram(source);upload(0x8804,0x8875,GLsizei(compiled.size()),compiled.data());
                GLint position=0;glGetIntegerv(0x864B,&position);check(position==-1,"instrumented ARB shader syntax");
                glEnable(0x8804);activate(0x84C5);glBindTexture(GL_TEXTURE_2D,textures[1]);activate(0x84C3);
                eyeshineVariantDraw();GLfloat flags[4]{};getLocal(0x8804,2,flags);
                check(flags[0]==(variant==4?1.f:0.f) && flags[1]==(variant==5?1.f:0.f),"shader live flags");
                // The actual shipped source is read as data only, when available.
                if(const auto root=_wgetenv(L"RIDDICK_DIR");root && *root) {
                    std::ifstream in(std::filesystem::path(root)/L"Gl/NV_Fragment_Program2/XRShader_FP20SS_SpecNormal.fp",std::ios::binary);
                    check(in.is_open(),"read reference SpecNormal");const std::string original{std::istreambuf_iterator<char>(in),{}};
                    const auto rewritten=eyeshineVariantProgram(rewriteFragmentProgram(original,true,false));
                    upload(0x8804,0x8875,GLsizei(rewritten.size()),rewritten.data());glGetIntegerv(0x864B,&position);
                    if(position!=-1) std::cerr<<"shader: "<<reinterpret_cast<const char*>(glGetString(0x8874))<<" at "<<position<<'\n';
                    check(position==-1,"reference SpecNormal variant compilation");
                }
                glDisable(0x8804);
            } else std::cout<<"ARB compile/local checks skipped: unsupported driver\n";
            glDeleteTextures(2,textures);
        } else std::cout<<"mask and shader probes skipped: multitexture unavailable\n";
        check(glGetError()==GL_NO_ERROR,"variant injected GL error");
        eyeshineVariantSwap(TRUE,true);check(eyeshineVariant()==(variant+1)%8,"cycle from initial setting");
        eyeshineVariantSwap(TRUE,true);check(eyeshineVariant()==(variant+1)%8,"held key cycles repeatedly");
        configureEyeshineVariants(gl,Settings{});check(!eyeshineVariantsEnabled(),"default leaves instrumentation enabled");
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
