#pragma once
#include "config.h"
#include <gl/GL.h>
#include <array>
#include <cstdint>
#include <string>

constexpr int eyeshineVariantCount=8;
struct EyeshineAbSequence {
    int remaining=0,total=0,target=0,saved=0;
    bool begin(int frames,int variant,int original) noexcept;
    int advance() noexcept; // next frame's variant; restores saved after the final sample
};
constexpr unsigned eyeshineScaleRva=0x5870;
inline constexpr std::array<unsigned char,10> eyeshineScaleBytes{0xF6,0x81,0x9C,0x20,0,0,4,0x48,0x8B,0xC2};
struct EyeshineVariantCycle {
    int value=0;
    bool held=false;
    bool press(bool down) noexcept;
};
struct EyeshineMaskMapping {
    bool valid=false;
    std::array<GLfloat,4> map{}, bounds{};
};
EyeshineMaskMapping eyeshineMaskMapping(GLint tw,GLint th,GLint dx,GLint dy,
                                      GLint x,GLint y,GLsizei w,GLsizei h) noexcept;
std::string instrumentEyeshineProgram(std::string_view source);
// Experiments are installed for TraceShadowPass=1 or a nonzero initial variant.
// Variant zero keeps original pixels/math; F9 switches only on successful swaps.
void configureEyeshineVariants(HMODULE gl,const Settings& settings) noexcept;
bool eyeshineVariantsEnabled() noexcept;
int eyeshineVariant() noexcept;
void eyeshineVariantSwap(BOOL result,bool down) noexcept;
bool eyeshineAbConfigured() noexcept;
bool eyeshineAbBegin(int frames) noexcept; // after presentation, before the first sampled frame
void eyeshineAbCapture(std::uint64_t index) noexcept;
void eyeshineAbSwap(BOOL result) noexcept;
void eyeshineAbCancel() noexcept;
std::string eyeshineVariantProgram(std::string_view source);
void eyeshineVariantDraw() noexcept;
void eyeshineVariantClear(GLbitfield mask) noexcept;
bool eyeshineVariantBeforePixels(GLenum type) noexcept;
void eyeshineVariantAfterPixels(bool restore,GLint x,GLint y,GLsizei w,GLsizei h,GLenum type) noexcept;
void eyeshineVariantTexture(GLenum target,GLint level,GLint dx,GLint dy,GLint x,GLint y,GLsizei w,GLsizei h) noexcept;
void eyeshineVariantOtherCopy() noexcept;
void eyeshineVariantContextDeleted(HGLRC context) noexcept;
void installEyeshineScaleHook(HMODULE world,const Settings& settings);
