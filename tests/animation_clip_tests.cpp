#include <darkangel/animation_assets.hpp>
#include <cgltf.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <span>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"Missing clip test input");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());require(bool(out),"Clip fixture write");}
std::string edit_glb(const std::string& bytes,const std::function<void(Json&)>& edit){
    std::uint32_t size{};std::memcpy(&size,bytes.data()+12,4);auto json=Json::parse(bytes.substr(20,size));edit(json);auto document=json.dump();while(document.size()%4)document+=' ';
    auto tail=bytes.substr(20+size);std::string result=bytes.substr(0,20)+document+tail;
    auto length=static_cast<std::uint32_t>(result.size()),json_length=static_cast<std::uint32_t>(document.size());std::memcpy(result.data()+8,&length,4);std::memcpy(result.data()+12,&json_length,4);return result;
}
using Matrix=std::array<double,16>;
Matrix trs(const std::array<float,3>& t,const std::array<float,4>& q){
    double x=q[0],y=q[1],z=q[2],w=q[3];
    return {1-2*y*y-2*z*z,2*x*y+2*w*z,2*x*z-2*w*y,0,2*x*y-2*w*z,1-2*x*x-2*z*z,2*y*z+2*w*x,0,2*x*z+2*w*y,2*y*z-2*w*x,1-2*x*x-2*y*y,0,t[0],t[1],t[2],1};
}
Matrix multiply(const Matrix& a,const Matrix& b){Matrix c{};for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)c[col*4+row]+=a[k*4+row]*b[col*4+k];return c;}
std::array<float,4> quat_multiply(std::array<float,4> a,std::array<float,4> b){return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};}
void clip_test(const std::filesystem::path& project,const std::filesystem::path& output,const std::string& name){
    auto source=output/name;std::filesystem::create_directories(source);
    std::filesystem::copy_file(project/"content/animation/canonical_human.daskeleton",source/"human.daskeleton");
    std::filesystem::copy_file(project/".cache/fixtures"/(name+".glb"),source/"clip.glb");
    AssetService assets(source,source/"cache");auto id=assets.adopt_clip("clip.glb","human.daskeleton",name=="idle"||name=="run");
    auto result=assets.cook("clip.glb");require(result.changed&&!assets.cook("clip.glb").changed,"Clip cold/warm cook");
    assets.package(id,source/"registry.json");auto registry=read(source/"registry.json");
    auto rig=load_cooked_rig(source/"registry.json",assets.cas_path(),decode_rig_source(read(source/"human.daskeleton")).id);
    auto cooked=load_cooked_clip(source/"registry.json",assets.cas_path(),id);AnimationClip clip(cooked.definition,cooked.archive);
    RigPose first(rig.definition,rig.archive),second(rig.definition,rig.archive);
    const auto saved_second=second.sample(clip,3);first.sample(clip,17);
    require(second.sample(clip,3)[12].values==saved_second[12].values,"Independent sampling context/buffers");
    auto bytes=read(source/"clip.glb");cgltf_options options{};cgltf_data* pointer{};
    require(cgltf_parse(&options,bytes.data(),bytes.size(),&pointer)==cgltf_result_success,"Reference GLB parse");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(pointer,cgltf_free);data->buffers[0].data=const_cast<void*>(data->bin);
    const auto count=rig.definition.joints.size();double max_tip_error{};double max_root{};
    std::vector<Matrix> expected(count);
    auto definition=cooked.definition;definition.loop=false;AnimationClip terminal(definition,cooked.archive);
    for(unsigned tick=0;tick<=cooked.definition.ticks;++tick){
        std::vector<std::array<float,3>> translations(count);std::vector<std::array<float,4>> rotations(count);
        for(const auto& channel:std::span(data->animations[0].channels,data->animations[0].channels_count)){
            auto node=channel.target_node-data->nodes;auto key=std::find_if(rig.definition.joints.begin(),rig.definition.joints.end(),[&](const auto& j){return j.key==data->nodes[node].name;});auto index=key-rig.definition.joints.begin();
            require(key!=rig.definition.joints.end(),"Reference joint key");
            if(channel.target_path==cgltf_animation_path_type_translation)require(cgltf_accessor_read_float(channel.sampler->output,tick,translations[index].data(),3),"Reference translation");
            else require(cgltf_accessor_read_float(channel.sampler->output,tick,rotations[index].data(),4),"Reference rotation");
        }
        auto rest=rig.definition.joints[0].rotation;rest[0]*=-1;rest[1]*=-1;rest[2]*=-1;
        auto delta=quat_multiply(rotations[0],rest);const auto yaw=2*std::atan2(delta[1],delta[3]);
        rotations[0]=quat_multiply({0,float(std::sin(-yaw/2)),0,float(std::cos(-yaw/2))},rotations[0]);translations[0]=rig.definition.joints[0].translation;
        for(unsigned i=0;i<count;++i){auto local=trs(translations[i],rotations[i]);expected[i]=rig.definition.joints[i].parent<0?local:multiply(expected[rig.definition.joints[i].parent],local);}
        // Loop sampling intentionally wraps the final tick; compare end keys in a
        // separate non-loop instance, independent of the sampler implementation.
        const auto& actual=first.sample(terminal,tick);
        for(const auto& [socket,key]:rig.definition.sockets){
            auto joint=std::find_if(rig.definition.joints.begin(),rig.definition.joints.end(),[&](const auto& j){return j.key==key;})-rig.definition.joints.begin();
            for(double length:{0.0,.5}){
                double error{};for(unsigned axis=0;axis<3;++axis){double wanted=expected[joint][12+axis]+length*expected[joint][8+axis];double got=actual[joint].values[12+axis]+length*actual[joint].values[8+axis];error+=(wanted-got)*(wanted-got);}max_tip_error=std::max(max_tip_error,std::sqrt(error));
            }
        }
        for(unsigned axis=0;axis<3;++axis)require(std::abs(actual[0].values[12+axis]-translations[0][axis])<.001,"Root translation stripped from sampled pose");
        auto root=cooked.definition.root[tick];max_root=std::max(max_root,double(std::sqrt(root[0]*root[0]+root[1]*root[1]+root[2]*root[2])));
    }
    require(max_tip_error<.002,"Ozz compression error at sockets/half-metre weapon tips exceeds 2 mm");
    if(cooked.definition.loop){auto start=first.sample(clip,0)[0].values;require(start==first.sample(clip,cooked.definition.ticks)[0].values,"Loop pose wraps at exact duration");}
    auto wrong=cooked.definition;wrong.signature=std::string(64,'a');AnimationClip incompatible(wrong,cooked.archive);bool denied=false;try{first.sample(incompatible,0);}catch(...){denied=true;}require(denied,"Incompatible clip rejects before sampling");
    auto sidecar=Json::parse(read(source/"clip.glb.daimport"));sidecar["loop"]=!cooked.definition.loop;write(source/"clip.glb.daimport",sidecar.dump());
    require(assets.cook("clip.glb").changed,"Changed import loop setting invalidates recipe");
    assets.package(id,source/"new-registry.json");require(load_cooked_clip(source/"new-registry.json",assets.cas_path(),id).definition.loop!=cooked.definition.loop,"Loop setting published coherently");
    // Unchanged live instances retain the old immutable clip generation.
    require(clip.definition().loop==cooked.definition.loop,"Live clip generation remains pinned");
    const auto preserved=read(source/"new-registry.json");
    for(const auto& interpolation:{"STEP","CUBICSPLINE"}){
        write(source/"clip.glb",edit_glb(bytes,[&](Json& j){j["animations"][0]["samplers"][0]["interpolation"]=interpolation;}));
        bool unsupported=false;try{assets.cook("clip.glb");}catch(...){unsupported=true;}require(unsupported,"Unsupported interpolation rejects explicitly");
        assets.package(id,source/"rejected-registry.json");require(read(source/"rejected-registry.json")==preserved,"Rejected interpolation preserves prior complete generation");
    }
    write(source/"clip.glb",bytes);
    auto original=read(source/"human.daskeleton");auto broken=Json::parse(original);broken["joints"][0]["translation"][0]=.1;write(source/"human.daskeleton",broken.dump());
    auto prior=read(source/"new-registry.json");bool rejected=false;try{assets.cook("clip.glb");}catch(...){rejected=true;}require(rejected,"Bad canonical rest rejects reimport");
    assets.package(id,source/"retained-registry.json");require(read(source/"retained-registry.json")==prior,"Failed clip cook preserves frozen rig/archive closure");
    std::cout<<name<<" ticks="<<cooked.definition.ticks<<" archive_bytes="<<cooked.archive.size()<<" max_tip_error_m="<<max_tip_error<<" root_extent_m="<<max_root<<'\n';
}
}
int main(){try{
    ClipDefinition looping;looping.ticks=2;looping.loop=true;looping.root={{{0,0,0,0}},{{.5,0,0,float(3.141592653589793/4)}},{{1,0,0,float(3.141592653589793/2)}}};
    auto first=clip_root_delta(looping,0,2),second=clip_root_delta(looping,2,4),both=clip_root_delta(looping,0,4);
    require(std::abs(first.translation[0]-1)<.00001&&std::abs(second.translation[0]-1)<.00001&&std::abs(second.translation[2])<.00001,"Each loop produces the same local root delta");
    require(std::abs(both.translation[0]-1)<.00001&&std::abs(both.translation[2]+1)<.00001&&std::abs(both.yaw-3.141592653589793)<.00001,"Loop yaw rotates accumulated translation");
    auto crossing=clip_root_delta(looping,1.5,2.5);require(std::isfinite(crossing.translation[0])&&crossing.yaw>.7&&crossing.yaw<.9,"Fractional root sampling splits a loop boundary");
    bool bounded=false;try{clip_root_delta(looping,0,9);}catch(...){bounded=true;}require(bounded,"Root traversal work remains bounded");
    auto project=std::filesystem::path(DAE_SOURCE_DIR),output=std::filesystem::path(DAE_BINARY_DIR)/("clip-fixture-"+AssetId::random().text());for(auto name:{"idle","run","attack","dodge"})clip_test(project,output,name);
    std::cout<<"Real canonical FBX clip cook, compression, stripped poses, independent contexts, loop root and generation preservation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
