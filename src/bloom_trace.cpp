#include "bloom_trace.h"
#include "log.h"
#include <atomic>
#include <cstdio>
#include <intrin.h>

namespace {
std::atomic<unsigned> records{0};
constexpr unsigned recordLimit = 8192;
bool exhausted() { return records.load(std::memory_order_relaxed) >= recordLimit; }
template<class... T> void append(std::string& out, const char* format, T... values) {
    char buffer[512]{};
    std::snprintf(buffer,sizeof(buffer),format,values...);
    out += buffer;
}
}

std::uint32_t shaderProgramCrc32(std::string_view source) noexcept {
    std::uint32_t crc=0xFFFFFFFFu;
    for(unsigned char byte:source) {
        crc^=byte;
        for(int bit=0;bit<8;++bit) crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1u)));
    }
    return crc^0xFFFFFFFFu;
}

BloomTrace::BloomTrace(bool enabled, Sink sink) : enabled_(enabled), sink_(sink ? sink : logLineNoThrow) {}
void BloomTrace::emit(std::string_view line) {
    // Saturating process-wide budget, including all contexts and upload events.
    auto count=records.load(std::memory_order_relaxed);
    do { if(count>=recordLimit) return; }
    while(!records.compare_exchange_weak(count,count+1,std::memory_order_relaxed));
    sink_(line);
    if(count+1==recordLimit) sink_("TraceBloom limit reached; further diagnostics disabled");
}
void BloomTrace::upload(const BloomApi& api, GLenum target, std::string_view source, std::string_view submitted) noexcept try {
    if(!enabled_ || exhausted() || !api.getProgram || (target!=0x8804 && target!=0x8620)) return;
    GLint id=0; api.getProgram(target,0x8677,&id); // GL_PROGRAM_BINDING_ARB
    const auto key=std::make_pair(target,GLuint(id));
    const auto crc=std::make_pair(shaderProgramCrc32(source),shaderProgramCrc32(submitted));
    // Drop all associations at capacity; never retain a stale CRC for a reused ID.
    if(programs_.size()>=512 && !programs_.count(key)) programs_.clear();
    programs_[key]=crc;
    if(uploads_++>=256) return;
    std::string line;
    append(line,"TraceBloom upload ctx=%p target=%X id=%d sourceCRC=%08X submittedCRC=%08X bytes=%zu/%zu",
        api.context ? api.context() : nullptr,target,id,crc.first,crc.second,source.size(),submitted.size());
    emit(line);
} catch(...) { enabled_=false; logLineNoThrow("TraceBloom upload observation failed; tracing disabled for this context"); }
void BloomTrace::draw(const BloomApi& api, const char* event, const void* caller) noexcept try {
    if(!enabled_ || !sample_ || exhausted() || !api.getInteger || !api.isEnabled) return;
    std::string line;
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(caller,&memory,sizeof(memory));
    append(line,"TraceBloom frame=%llu event=%s caller=%p base=%p rva=%llX ctx=%p dc=%p",
        frame_,event,caller,memory.AllocationBase,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(caller)-reinterpret_cast<std::uintptr_t>(memory.AllocationBase)),
        api.context ? api.context() : nullptr,api.dc ? api.dc() : nullptr);
    GLint viewport[4]{}, draw=0, read=0, src=0, dst=0, equation=0x8006, fbo=0;
    api.getInteger(GL_VIEWPORT,viewport); api.getInteger(GL_DRAW_BUFFER,&draw); api.getInteger(GL_READ_BUFFER,&read);
    api.getInteger(GL_BLEND_SRC,&src); api.getInteger(GL_BLEND_DST,&dst);
    if(api.blendEquationQuery) api.getInteger(0x8009,&equation);
    if(api.framebufferQuery) api.getInteger(0x8CA6,&fbo);
    append(line," mxcsr=%X viewport=%d,%d,%d,%d draw=%X read=%X fbo=%d:%d blend=%u:%X,%X,%X",
        _mm_getcsr(),
        viewport[0],viewport[1],viewport[2],viewport[3],draw,read,api.framebufferQuery,fbo,
        unsigned(api.isEnabled(GL_BLEND)),src,dst,equation);
    if(api.getFloat) {
        GLfloat color[4]{}; api.getFloat(GL_CURRENT_COLOR,color);
        append(line," color=%.9g,%.9g,%.9g,%.9g",color[0],color[1],color[2],color[3]);
    }
    if(api.getAttrib) {
        GLfloat color[4]{}; api.getAttrib(3,0x8626,color); // generic color remap used by 2.0++
        append(line," attrib3=%.9g,%.9g,%.9g,%.9g",color[0],color[1],color[2],color[3]);
    }
    if(api.getProgram) {
        for(GLenum target:{GLenum(0x8804),GLenum(0x8620)}) {
            const bool active=api.isEnabled(target)!=GL_FALSE;
            GLint id=0; api.getProgram(target,0x8677,&id);
            append(line," %s=%d:",target==0x8804 ? "fp" : "vp",id);
            const auto found=programs_.find({target,GLuint(id)});
            if(found==programs_.end()) line+="unknown";
            else append(line,"%08X/%08X",found->second.first,found->second.second);
            append(line,":enabled=%d",active);
            if(target==0x8804 && active && api.getEnv) for(GLuint i=0;i<8;++i) {
                GLfloat value[4]{}; api.getEnv(target,i,value);
                append(line," env%u=%.9g,%.9g,%.9g,%.9g",i,value[0],value[1],value[2],value[3]);
            }
        }
    }
    // Only native 2D targets: these are the screen-copy/blur inputs. Keep the
    // active server unit exactly as found; never touch client-active texture.
    GLint oldUnit=0x84C0;
    if(api.activeTexture) api.getInteger(0x84E0,&oldUnit);
    struct RestoreUnit {
        const BloomApi& api; GLint unit;
        ~RestoreUnit() { if(api.activeTexture) api.activeTexture(unit); }
    } restore{api,oldUnit};
    for(unsigned i=0;i<(api.activeTexture ? 4u : 1u);++i) {
        if(api.activeTexture) api.activeTexture(0x84C0+i);
        GLint id=0,w=0,h=0,format=0;
        api.getInteger(GL_TEXTURE_BINDING_2D,&id);
        if(id && api.getTexLevel) {
            api.getTexLevel(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
            api.getTexLevel(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            api.getTexLevel(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        }
        append(line," tex%u=%d:%dx%d:%X",i,id,w,h,format);
    }
    tail_[events_++%tail_.size()]=std::move(line);
} catch(...) { enabled_=false; logLineNoThrow("TraceBloom draw observation failed; tracing disabled for this context"); }
void BloomTrace::swap(std::uint64_t now,HDC dc,BOOL result) noexcept try {
    if(!enabled_ || exhausted()) return;
    if(sample_) {
        const auto start=events_>tail_.size() ? events_-tail_.size() : 0;
        for(auto i=start;i<events_;++i) emit(tail_[i%tail_.size()]);
        std::string line;
        append(line,"TraceBloom frame=%llu SwapBuffers dc=%p result=%d events=%llu omitted=%llu timeMs=%llu",
            frame_,dc,result,events_,start,now);
        emit(line);
    }
    ++frame_; events_=0;
    sample_=frame_<3 || now>=nextSample_;
    if(sample_) nextSample_=now+1000;
} catch(...) { enabled_=false; logLineNoThrow("TraceBloom swap observation failed; tracing disabled for this context"); }
