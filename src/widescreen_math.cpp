#include "widescreen.h"
#include <algorithm>
#include <cmath>

CanvasMapping hudCanvasMapping(int width,int height) {
    if(width<=0 || height<=0) return {};
    const double uniform=(std::min)(width/640.0,height/480.0);
    const auto x=uniform*640/width,y=uniform*480/height;
    return {x,y,320*(1-x),240*(1-y)};
}
void mapHudMatrix(std::array<float,16>& matrix,const CanvasMapping& m) {
    const auto original=matrix;
    for(int i=0;i<4;++i) {
        matrix[i]=static_cast<float>(original[i]*m.scaleX);
        matrix[4+i]=static_cast<float>(original[4+i]*m.scaleY);
        matrix[12+i]=static_cast<float>(original[12+i]+original[i]*m.offsetX+original[4+i]*m.offsetY);
    }
}
double relativeGameplayFov(double authored,double configured) {
    if(!std::isfinite(authored) || authored<=0 || authored>=179 ||
       !std::isfinite(configured) || configured<60 || configured>120 || configured==90) return authored;
    constexpr double halfRadians=3.14159265358979323846/360;
    const auto result=std::atan(std::tan(authored*halfRadians)*std::tan(configured*halfRadians))/halfRadians;
    return result>0 && result<179 ? result:authored;
}
bool isGameplayCamera(unsigned flags,unsigned specialMode) {
    return !(flags&((1u<<23)|(1u<<26))) && specialMode!=2;
}
bool alignMenuQuad(std::array<float,15>& vertices,const std::array<float,16>& p,int width,int height) {
    if(width<=0 || height<=0 || width>32768 || height>32768 ||
       !std::isfinite(p[0]) || !std::isfinite(p[5]) || p[0]==0 || p[5]==0 ||
       p[1]!=0 || p[4]!=0 || p[3]!=0 || p[7]!=0) return false;
    // The helper's four corners describe the requested frustum. The renderer
    // uploads the projection of the clipped view area. Solve the quad against
    // that matrix, retaining depth, winding, UVs and subsequent artwork fit.
    double minX=0,maxX=0,minY=0,maxY=0;
    std::array<double,4> x{},y{},w{};
    for(int i=0;i<4;++i) {
        const auto j=(i+1)*3;
        const double z=vertices[j+2]; w[i]=z*p[11]+p[15];
        if(!std::isfinite(w[i]) || w[i]<=0) return false;
        x[i]=(vertices[j]*p[0]+z*p[8]+p[12])/w[i];
        y[i]=(vertices[j+1]*p[5]+z*p[9]+p[13])/w[i];
        if(!std::isfinite(x[i]) || !std::isfinite(y[i])) return false;
        if(!i) { minX=maxX=x[i];minY=maxY=y[i]; }
        minX=(std::min)(minX,x[i]);maxX=(std::max)(maxX,x[i]);
        minY=(std::min)(minY,y[i]);maxY=(std::max)(maxY,y[i]);
    }
    if(maxX<=minX || maxY<=minY) return false;
    auto corrected=vertices;
    for(int i=0;i<4;++i) {
        const auto j=(i+1)*3; const double z=vertices[j+2];
        const auto edgeX=x[i]<(minX+maxX)/2 ? -width/2.0:width/2.0;
        const auto edgeY=y[i]<(minY+maxY)/2 ? -height/2.0:height/2.0;
        corrected[j]=static_cast<float>((edgeX*w[i]-z*p[8]-p[12])/p[0]);
        corrected[j+1]=static_cast<float>((edgeY*w[i]-z*p[9]-p[13])/p[5]);
        if(!std::isfinite(corrected[j]) || !std::isfinite(corrected[j+1])) return false;
    }
    vertices=corrected;
    return true;
}
