#include <windows.h>
#include <gl/GL.h>
#include <algorithm>
namespace { GLint observed[4]{};bool inBegin=false;void record(){if(!inBegin) glGetIntegerv(GL_SCISSOR_BOX,observed);} }
extern "C" {
__declspec(dllexport) void bigfixRecordedScissor(GLint* out){std::copy(observed,observed+4,out);}
void WINAPI fixtureArrays(GLenum mode,GLint first,GLsizei count){record();glDrawArrays(mode,first,count);}
void WINAPI fixtureElements(GLenum mode,GLsizei count,GLenum type,const void* indices){record();glDrawElements(mode,count,type,indices);}
void WINAPI fixtureBegin(GLenum mode){record();glBegin(mode);inBegin=true;}
void WINAPI fixtureEnd(){glEnd();inBegin=false;}
void WINAPI fixtureList(GLuint list){record();glCallList(list);}
void WINAPI fixtureClear(GLbitfield mask){glClear(mask);}
}
