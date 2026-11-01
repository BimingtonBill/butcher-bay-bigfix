#pragma once
#include "config.h"
#include "patch_table.h"
#include <array>

struct CanvasMapping {
    double scaleX=1, scaleY=1, offsetX=0, offsetY=0;
};
CanvasMapping hudCanvasMapping(int width,int height);
void mapHudMatrix(std::array<float,16>& matrix,const CanvasMapping& mapping);
double relativeGameplayFov(double authored,double configured);
bool isGameplayCamera(unsigned flags,unsigned specialMode);
// Engine (pre-GL-normalization) projection and its clipped physical view area.
bool alignMenuQuad(std::array<float,15>& vertices,const std::array<float,16>& projection,int width,int height);

enum class WidescreenModule { World, Classes, Renderer, System };
struct WidescreenSite { WidescreenModule module; PatchSite patch; };
const std::vector<WidescreenSite>& widescreenSites();
std::size_t installWidescreenHooks(HMODULE module,WidescreenModule kind,const Settings& settings);
void widescreenFrameEnd(BOOL success) noexcept;
void traceFovProjection(const float* matrix,const void* caller,HGLRC context) noexcept;
