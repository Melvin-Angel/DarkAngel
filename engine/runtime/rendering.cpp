#include <darkangel/rendering.hpp>
#include <algorithm>
#include <cmath>

namespace darkangel::rendering {
namespace {
float clamp01(float value) { return std::clamp(value, 0.f, 1.f); }
float influence(const SceneLight& light) { return std::max(0.f, light.intensity) * (light.color.r * .2126f + light.color.g * .7152f + light.color.b * .0722f); }
float band(float value, float threshold, float softness) {
    if (softness <= 1e-5f) return value >= threshold ? 1.f : 0.f;
    const float t=clamp01((value-threshold+softness)/(2*softness));
    return t*t*(3-2*t);
}
float toon_ramp(const ToonLighting& s,float value) {
    return .4f*band(value,s.band_thresholds[0],s.transition_softness)+
           .35f*band(value,s.band_thresholds[1],s.transition_softness)+
           .25f*band(value,s.band_thresholds[2],s.transition_softness);
}
}
ClusterCell& ClusterAssignment::at(unsigned x, unsigned y, unsigned z) { return cells[(static_cast<std::size_t>(z) * tiles_y + y) * tiles_x + x]; }
const ClusterCell& ClusterAssignment::at(unsigned x, unsigned y, unsigned z) const { return cells[(static_cast<std::size_t>(z) * tiles_y + y) * tiles_x + x]; }
ClusterLayout cluster_layout(const ClusterConfig& input, const ClusterView& view) { const auto tile_size=std::max(1u,input.tile_size),slices=std::max(1u,input.depth_slices),width=std::max(1u,view.width),height=std::max(1u,view.height);const auto tiles_x=(width+tile_size-1)/tile_size,tiles_y=(height+tile_size-1)/tile_size;return {tiles_x,tiles_y,slices,static_cast<std::size_t>(tiles_x)*tiles_y*slices}; }
ClusterAssignment assign_lights(const ClusterConfig& input, const ClusterView& view, std::span<const SceneLight> lights) {
    ClusterConfig c=input;c.tile_size=std::max(1u,c.tile_size);c.depth_slices=std::max(1u,c.depth_slices);c.max_candidates=std::max(1u,c.max_candidates);c.shading_budget=std::min(c.max_candidates,std::max(1u,c.shading_budget));const auto layout=cluster_layout(c,view);ClusterAssignment r;r.tiles_x=layout.tiles_x;r.tiles_y=layout.tiles_y;r.slices=layout.slices;r.cells.resize(layout.clusters);r.stats.clusters=r.cells.size();const float n=std::max(.0001f,view.near_plane),f=std::max(n*1.01f,view.far_plane),lr=std::log(f/n),ty=std::max(.001f,view.tan_half_fov_y),tx=ty*std::max(.01f,view.aspect);
    for(const auto&l:lights){if(!l.enabled)continue;if(l.type==LightType::Directional){++r.stats.directional_lights;continue;}const float range=std::max(.001f,l.range),depth=-l.position_view[2];if(depth+range<n||depth-range>f||depth<=.001f)continue;auto slice=[&](float z){return std::min(r.slices-1,static_cast<unsigned>(std::max(0.f,std::log(z/n)/lr*r.slices)));};const unsigned z0=slice(std::max(n,depth-range)),z1=slice(std::min(f,depth+range));const float screen_depth=std::max(n,depth-range),minx=(l.position_view[0]-range)/screen_depth/tx,maxx=(l.position_view[0]+range)/screen_depth/tx,miny=(l.position_view[1]-range)/screen_depth/ty,maxy=(l.position_view[1]+range)/screen_depth/ty;const unsigned x0=std::min(r.tiles_x,static_cast<unsigned>(std::max(0.f,(minx*.5f+.5f)*r.tiles_x))),x1=std::min(r.tiles_x,static_cast<unsigned>(std::ceil(std::clamp(maxx*.5f+.5f,0.f,1.f)*r.tiles_x))),y0=std::min(r.tiles_y,static_cast<unsigned>(std::max(0.f,(.5f-maxy*.5f)*r.tiles_y))),y1=std::min(r.tiles_y,static_cast<unsigned>(std::ceil(std::clamp(.5f-miny*.5f,0.f,1.f)*r.tiles_y)));if(x0>=x1||y0>=y1)continue;for(unsigned z=z0;z<=z1;++z)for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x)r.at(x,y,z).candidates.push_back(l.id);}
    for(auto&cell:r.cells){if(cell.candidates.empty())continue;std::sort(cell.candidates.begin(),cell.candidates.end());cell.candidates.erase(std::unique(cell.candidates.begin(),cell.candidates.end()),cell.candidates.end());std::stable_sort(cell.candidates.begin(),cell.candidates.end(),[&](const StableId&a,const StableId&b){auto find=[&](const StableId&id){return std::find_if(lights.begin(),lights.end(),[&](const SceneLight&l){return l.id==id;});};const auto aa=find(a),bb=find(b);const float sa=aa==lights.end()?0:influence(*aa),sb=bb==lights.end()?0:influence(*bb);return sa!=sb?sa>sb:a<b;});if(cell.candidates.size()>c.max_candidates){cell.overflow=true;cell.candidates.resize(c.max_candidates);}const auto count=std::min<std::size_t>(c.shading_budget,cell.candidates.size());cell.selected.assign(cell.candidates.begin(),cell.candidates.begin()+count);++r.stats.non_empty;r.stats.candidate_links+=cell.candidates.size();r.stats.selected_links+=cell.selected.size();r.stats.overflowed+=cell.overflow?1u:0u;}return r;
}
std::array<float,3> evaluate_toon(const ToonLighting&s,Color3 b,Color3 m,float intensity,float visibility,float normal,std::span<const SceneLight>local,float ao){
    const float v=toon_ramp(s,clamp01(normal)*clamp01(visibility));
    const std::array<float,3> base{b.r,b.g,b.b},main{m.r,m.g,m.b},ambient{s.ambient_color.r,s.ambient_color.g,s.ambient_color.b},shadow{s.shadow_tint.r,s.shadow_tint.g,s.shadow_tint.b};
    std::array<float,3> out{},fill{};
    for(unsigned i=0;i<3;++i){const float key=(.35f+.65f*main[i])*intensity;out[i]=base[i]*(shadow[i]+(key-shadow[i])*v+ambient[i]*s.ambient_strength*ao*.35f);}
    // This scalar reference assumes a local-light-facing surface. The GPU uses
    // its actual per-pixel normal in the same attenuation-before-ramp rule.
    for(const auto& l:local){
        if(!l.enabled||l.type==LightType::Directional)continue;
        const float d=std::sqrt(l.position_view[0]*l.position_view[0]+l.position_view[1]*l.position_view[1]+l.position_view[2]*l.position_view[2]);
        const float a=std::max(0.f,1.f-d/std::max(.001f,l.range));
        float spot=1;
        if(l.type==LightType::Spot){const float length=std::max(d,.001f),cone=(-l.position_view[0]*l.direction_view[0]-l.position_view[1]*l.direction_view[1]-l.position_view[2]*l.direction_view[2])/length;const float t=clamp01((cone-std::cos(l.outer_cone_degrees*3.14159265f/180.f))/std::max(.001f,std::cos(l.inner_cone_degrees*3.14159265f/180.f)-std::cos(l.outer_cone_degrees*3.14159265f/180.f)));spot=std::pow(t*t*(3-2*t),std::max(.1f,l.falloff));}
        const float q=.5f*std::max(0.f,l.intensity)*toon_ramp(s,a*a*spot)*s.local_light_strength;
        fill[0]+=l.color.r*q;fill[1]+=l.color.g*q;fill[2]+=l.color.b*q;
    }
    for(unsigned i=0;i<3;++i)out[i]+=base[i]*std::min(.65f,fill[i]);
    return out;
}
float evaluate_fog(const FogSettings&s,float d,float h){if(!s.enabled)return 0;float a=s.exponential?1.f-std::exp(-std::max(0.f,s.density)*std::max(0.f,d)):clamp01((d-s.start)/std::max(.001f,s.end-s.start));if(s.height_falloff>0)a*=std::exp(-std::max(0.f,h-s.height)*s.height_falloff);return clamp01(a);}
Color3 apply_color_grading(const PostProcessSettings&s,Color3 l){const float e=std::exp2(s.exposure+s.exposure_compensation),g=(l.r*.2126f+l.g*.7152f+l.b*.0722f)*e,t=std::clamp(s.grade_temperature,-1.f,1.f),warm=1+.08f*t,cool=1-.08f*t;auto grade=[&](float v,float tint,float lift,float gamma,float gain,float temperature){const float contrasted=std::max(0.f,((v*e-g)*s.contrast+g)*s.saturation*tint);return std::pow(std::max(0.f,contrasted+lift),1/std::max(.001f,gamma))*gain*temperature;};return{grade(l.r,s.tint.r,s.grade_lift.r,s.grade_gamma.r,s.grade_gain.r,warm),grade(l.g,s.tint.g,s.grade_lift.g,s.grade_gamma.g,s.grade_gain.g,1),grade(l.b,s.tint.b,s.grade_lift.b,s.grade_gamma.b,s.grade_gain.b,cool)};}
}
