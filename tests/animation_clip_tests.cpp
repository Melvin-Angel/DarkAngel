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
    const std::map<std::string,std::size_t> unoptimized={{"idle",155217},{"run",61655},{"attack",79210},{"dodge",137680}};
    std::cout<<name<<" archive_bytes="<<cooked.archive.size()<<std::endl;
    require(cooked.archive.size()<unoptimized.at(name)*.7,"Translation optimization must reduce fixture archive size by at least 30 percent");
    RigPose first(rig.definition,rig.archive),second(rig.definition,rig.archive);
    const auto saved_second=second.sample(clip,3);first.sample(clip,17);
    require(second.sample(clip,3)[12].values==saved_second[12].values,"Independent sampling context/buffers");
    auto close_pose=[&](const auto& a,const auto& b){require(a.size()==b.size(),"Pose palette dimensions");for(unsigned joint=0;joint<a.size();++joint)for(unsigned value=0;value<16;++value)require(std::abs(a[joint].values[value]-b[joint].values[value])<.00002,"Blended pose endpoint/rest equivalence");};
    const auto endpoint=second.sample(clip,17);PoseLayer one{&clip,17,1,{}};close_pose(first.blend(std::span(&one,1)),endpoint);auto rest=second.rest_pose();close_pose(first.blend({}),rest);
    std::vector<float> mask(rig.definition.joints.size(),0);PoseLayer layered[]={{&clip,3,1,{}},{&clip,17,1,mask}};const auto base=second.sample(clip,3);close_pose(first.blend(layered),base);layered[0].weight=0;std::fill(mask.begin(),mask.end(),1.f);close_pose(first.blend(layered),endpoint);
    std::fill(mask.begin(),mask.end(),0.f);auto wrist=std::find_if(rig.definition.joints.begin(),rig.definition.joints.end(),[](const auto& joint){return joint.key=="Wrist_R";})-rig.definition.joints.begin();mask[wrist]=1;layered[0].weight=1;auto masked=first.blend(layered);for(unsigned value=0;value<16;++value)require(std::abs(masked[0].values[value]-base[0].values[value])<.00002,"Masked hand layer cannot change root pose");
    const auto saved=first.blend(layered);mask[0]=NAN;bool invalid_mask=false;try{first.blend(layered);}catch(...){invalid_mask=true;}require(invalid_mask,"Invalid mask rejects before pose mutation");mask[0]=0;close_pose(first.blend(layered),saved);close_pose(second.sample(clip,3),base);std::array<PoseLayer,5> excessive;for(auto& value:excessive)value=one;bool over_budget=false;try{first.blend(excessive);}catch(...){over_budget=true;}require(over_budget,"Pose layer work budget rejects");one.weight=2;bool invalid_weight=false;try{first.blend(std::span(&one,1));}catch(...){invalid_weight=true;}require(invalid_weight,"Pose layer weight rejects");one.weight=1;

    auto bytes=read(source/"clip.glb");cgltf_options options{};cgltf_data* pointer{};
    require(cgltf_parse(&options,bytes.data(),bytes.size(),&pointer)==cgltf_result_success,"Reference GLB parse");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(pointer,cgltf_free);data->buffers[0].data=const_cast<void*>(data->bin);
    const auto count=rig.definition.joints.size();double max_tip_error{};double max_root{};
    // Independent uncompressed glTF local-space blend reference, not a second
    // call to the Ozz blender. Root is held by the base; all children blend 50/50.
    std::array<std::vector<std::array<float,3>>,2> blend_t;std::array<std::vector<std::array<float,4>>,2> blend_q;
    for(unsigned layer=0;layer<2;++layer){blend_t[layer].resize(count);blend_q[layer].resize(count);for(const auto& channel:std::span(data->animations[0].channels,data->animations[0].channels_count)){auto node=channel.target_node-data->nodes;auto joint=std::find_if(rig.definition.joints.begin(),rig.definition.joints.end(),[&](const auto& value){return value.key==data->nodes[node].name;})-rig.definition.joints.begin();auto tick=layer?17:3;if(channel.target_path==cgltf_animation_path_type_translation)require(cgltf_accessor_read_float(channel.sampler->output,tick,blend_t[layer][joint].data(),3),"Blend reference translation");else require(cgltf_accessor_read_float(channel.sampler->output,tick,blend_q[layer][joint].data(),4),"Blend reference rotation");}auto rest=rig.definition.joints[0].rotation;rest[0]*=-1;rest[1]*=-1;rest[2]*=-1;auto delta=quat_multiply(blend_q[layer][0],rest);auto yaw=2*std::atan2(delta[1],delta[3]);blend_q[layer][0]=quat_multiply({0,float(std::sin(-yaw/2)),0,float(std::cos(-yaw/2))},blend_q[layer][0]);blend_t[layer][0]=rig.definition.joints[0].translation;}
    std::fill(mask.begin(),mask.end(),1.f);mask[0]=0;auto mixed=first.blend(layered);std::vector<Matrix> reference(count);double blend_error{};
    for(unsigned joint=0;joint<count;++joint){std::array<float,3> translation;std::array<float,4> quaternion;double alpha=joint?.5:0,dot{},norm{};for(unsigned axis=0;axis<4;++axis)dot+=blend_q[0][joint][axis]*blend_q[1][joint][axis];for(unsigned axis=0;axis<3;++axis)translation[axis]=float(blend_t[0][joint][axis]*(1-alpha)+blend_t[1][joint][axis]*alpha);for(unsigned axis=0;axis<4;++axis){quaternion[axis]=float(blend_q[0][joint][axis]*(1-alpha)+blend_q[1][joint][axis]*alpha*(dot<0?-1:1));norm+=quaternion[axis]*quaternion[axis];}for(auto& value:quaternion)value=float(value/std::sqrt(norm));auto local=trs(translation,quaternion);reference[joint]=rig.definition.joints[joint].parent<0?local:multiply(reference[rig.definition.joints[joint].parent],local);}
    for(const auto& [socket,key]:rig.definition.sockets){auto joint=std::find_if(rig.definition.joints.begin(),rig.definition.joints.end(),[&](const auto& value){return value.key==key;})-rig.definition.joints.begin();for(double length:{0.0,.5}){double squared{};for(unsigned axis=0;axis<3;++axis){auto expected=reference[joint][12+axis]+length*reference[joint][8+axis];auto actual=mixed[joint].values[12+axis]+length*mixed[joint].values[8+axis];squared+=(expected-actual)*(expected-actual);}blend_error=std::max(blend_error,std::sqrt(squared));}}
    require(blend_error<.0005,"Masked local-space blend exceeds independent 0.5 mm socket/tip reference");std::cout<<name<<" masked_blend_tip_error_m="<<blend_error<<std::endl;
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
    std::cout<<name<<" optimized_bytes="<<cooked.archive.size()<<" max_tip_error_m="<<max_tip_error<<std::endl;
    require(max_tip_error<.0005,"Ozz optimized compression error at sockets/half-metre weapon tips exceeds 0.5 mm");
    if(cooked.definition.loop){auto start=first.sample(clip,0)[0].values;require(start==first.sample(clip,cooked.definition.ticks)[0].values,"Loop pose wraps at exact duration");}
    auto wrong=cooked.definition;wrong.signature=std::string(64,'a');AnimationClip incompatible(wrong,cooked.archive);bool denied=false;try{first.sample(incompatible,0);}catch(...){denied=true;}require(denied,"Incompatible clip rejects before sampling");PoseLayer mismatch{&incompatible,3,1,{}};denied=false;try{first.blend(std::span(&mismatch,1));}catch(...){denied=true;}require(denied,"Incompatible blended clip rejects before pose mutation");
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
void mixed_clips(const std::filesystem::path& project,const std::filesystem::path& output){
    auto source=output/"mixed";std::filesystem::create_directories(source);std::filesystem::copy_file(project/"content/animation/canonical_human.daskeleton",source/"human.daskeleton");for(auto name:{"idle","run"})std::filesystem::copy_file(project/".cache/fixtures"/(std::string(name)+".glb"),source/(std::string(name)+".glb"));AssetService assets(source,source/"cache");auto idle_id=assets.adopt_clip("idle.glb","human.daskeleton",true),run_id=assets.adopt_clip("run.glb","human.daskeleton",true);assets.cook("idle.glb");assets.cook("run.glb");assets.package(idle_id,source/"idle.json");assets.package(run_id,source/"run.json");auto a=load_cooked_clip(source/"idle.json",assets.cas_path(),idle_id),b=load_cooked_clip(source/"run.json",assets.cas_path(),run_id);auto rig=load_cooked_rig(source/"idle.json",assets.cas_path(),a.definition.skeleton);AnimationClip idle(a.definition,a.archive),run(b.definition,b.archive);RigPose pose(rig.definition,rig.archive),oracle(rig.definition,rig.archive);auto expected_idle=oracle.sample(idle,7),expected_run=oracle.sample(run,18);
    auto close=[&](const auto& expected,const auto& actual){for(unsigned joint=0;joint<expected.size();++joint)for(unsigned value=0;value<16;++value)require(std::abs(expected[joint].values[value]-actual[joint].values[value])<.00002,"Different-clip blend endpoint/cache equivalence");};PoseLayer layers[]={{&idle,7,1,{}},{&run,18,0,{}}};close(expected_idle,pose.blend(layers));layers[0].weight=0;layers[1].weight=1;close(expected_run,pose.blend(layers));layers[0].weight=layers[1].weight=.5;auto mixed=pose.blend(layers);bool differs_idle=false,differs_run=false;for(unsigned joint=0;joint<mixed.size();++joint)for(unsigned value=0;value<16;++value){auto actual=mixed[joint].values[value];require(std::isfinite(actual),"Mixed real-clip finite palette");differs_idle|=std::abs(actual-expected_idle[joint].values[value])>.001;differs_run|=std::abs(actual-expected_run[joint].values[value])>.001;}require(differs_idle&&differs_run,"Idle/run blend produces a composed pose");std::swap(layers[0],layers[1]);close(mixed,pose.blend(layers));close(expected_idle,oracle.sample(idle,7));std::cout<<"Real idle/run composition, archive/context switching and independent instances passed\n";
}

}
int main(){try{
    ClipDefinition looping;looping.ticks=2;looping.loop=true;looping.root={{{0,0,0,0}},{{.5,0,0,float(3.141592653589793/4)}},{{1,0,0,float(3.141592653589793/2)}}};
    auto first=clip_root_delta(looping,0,2),second=clip_root_delta(looping,2,4),both=clip_root_delta(looping,0,4);
    require(std::abs(first.translation[0]-1)<.00001&&std::abs(second.translation[0]-1)<.00001&&std::abs(second.translation[2])<.00001,"Each loop produces the same local root delta");
    require(std::abs(both.translation[0]-1)<.00001&&std::abs(both.translation[2]+1)<.00001&&std::abs(both.yaw-3.141592653589793)<.00001,"Loop yaw rotates accumulated translation");
    auto crossing=clip_root_delta(looping,1.5,2.5);require(std::isfinite(crossing.translation[0])&&crossing.yaw>.7&&crossing.yaw<.9,"Fractional root sampling splits a loop boundary");
    bool bounded=false;try{clip_root_delta(looping,0,9);}catch(...){bounded=true;}require(bounded,"Root traversal work remains bounded");
    auto project=std::filesystem::path(DAE_SOURCE_DIR),output=std::filesystem::path(DAE_BINARY_DIR)/("clip-fixture-"+AssetId::random().text());for(auto name:{"idle","run","attack","dodge"})clip_test(project,output,name);mixed_clips(project,output);
    std::cout<<"Real canonical FBX clip cook, compression, stripped poses, independent contexts, loop root and generation preservation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
