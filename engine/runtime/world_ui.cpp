#include <darkangel/world_ui.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {
std::optional<WorldUiProjection> project_world_ui(const WorldUiCamera& camera,double x,double y,double z){
    if(!std::isfinite(camera.width)||!std::isfinite(camera.height)||camera.width<=0||camera.height<=0||camera.width>16384||camera.height>16384)throw std::invalid_argument("World UI viewport bound");
    for(float value:camera.view)if(!std::isfinite(value))throw std::invalid_argument("World UI view matrix");for(float value:camera.projection)if(!std::isfinite(value))throw std::invalid_argument("World UI projection matrix");
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return {};
    auto multiply=[](const std::array<double,4>& v,const std::array<float,16>& matrix){std::array<double,4> result{};for(unsigned column=0;column<4;++column)for(unsigned row=0;row<4;++row)result[column]+=v[row]*matrix[row*4+column];return result;};
    const auto eye=multiply({x,y,z,1},camera.view);const auto clip=multiply(eye,camera.projection);const auto w=clip[3];
    if(w<=1e-5||clip[0]<-w||clip[0]>w||clip[1]<-w||clip[1]>w||clip[2]<0||clip[2]>w)return {};
    return WorldUiProjection{float((clip[0]/w+1)*.5*camera.width),float((1-clip[1]/w)*.5*camera.height),float(std::clamp(6./w,.45,1.3)),float(w)};
}
}
