#include <darkangel/joint_mask.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"Mask input missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,std::string_view bytes){std::ofstream out(path,std::ios::binary);out<<bytes;require(bool(out),"Mask fixture write failed");}
template<class F>void rejected(F&& action){bool denied=false;try{action();}catch(const std::exception&){denied=true;}require(denied,"Invalid mask operation accepted");}
}
int main(){try{
    auto project=std::filesystem::path(DAE_SOURCE_DIR),root=std::filesystem::path(DAE_BINARY_DIR)/("mask-fixture-"+AssetId::random().text());auto source=root/"sources";std::filesystem::create_directories(source);
    auto rig_source=read(project/"content/animation/canonical_human.daskeleton"),mask_source=read(project/"content/animation/m5_upper_body.damask");auto definition=decode_joint_mask_source(mask_source);auto original=Json::parse(mask_source);auto rig=decode_rig_source(rig_source);auto compiled=compile_joint_mask(definition,rig);
    auto index=[&](std::string_view key){auto found=std::find_if(rig.joints.begin(),rig.joints.end(),[&](const auto& joint){return joint.key==key;});require(found!=rig.joints.end(),"Test canonical joint missing");return unsigned(found-rig.joints.begin());};
    require(compiled.weights.size()==81&&compiled.weights[index("Root_M")]==0&&compiled.weights[index("Hip_L")]==0&&compiled.weights[index("Wrist_R")]==1,"Canonical-order upper-body mask compilation");
    write(source/"canonical_human.daskeleton",rig_source);write(source/"upper.damask",mask_source);
    JointMaskDefinition frozen;
    {AssetService assets(source,root/"cache");require(assets.adopt("upper.damask")==definition.id,"Native mask embedded UUID adoption");assets.scan();require(assets.cook("upper.damask").changed&&!assets.cook("upper.damask").changed,"Mask cold/warm cache");assets.package(definition.id,root/"mask.json");frozen=load_cooked_joint_mask(root/"mask.json",assets.cas_path(),definition.id);require(frozen.weights==compiled.weights&&frozen.generation==compiled.generation,"Frozen mask compatibility/order/generation");
        auto edit=original;edit["weights"]["Wrist_R"]=.5;write(source/"upper.damask",edit.dump());assets.cook("upper.damask");assets.package(definition.id,root/"changed.json");auto changed=load_cooked_joint_mask(root/"changed.json",assets.cas_path(),definition.id);require(changed.generation!=frozen.generation&&changed.weights[index("Wrist_R")]==.5&&frozen.weights[index("Wrist_R")]==1,"Live mask generation retained on reimport");auto retained=read(root/"changed.json");edit["weights"]["Typo_Wrist"]=1;write(source/"upper.damask",edit.dump());rejected([&]{assets.cook("upper.damask");});assets.package(definition.id,root/"retained.json");require(read(root/"retained.json")==retained,"Failed mask reimport preserves complete closure");
        // Consume compiled weights through the same private Ozz pose blender.
        std::filesystem::copy_file(project/".cache/fixtures/attack.glb",source/"attack.glb");auto clip_id=assets.adopt_clip("attack.glb","canonical_human.daskeleton",false);assets.cook("attack.glb");assets.package(clip_id,root/"clip.json");auto clip_data=load_cooked_clip(root/"clip.json",assets.cas_path(),clip_id);auto rig_data=load_cooked_rig(root/"clip.json",assets.cas_path(),rig.id);AnimationClip clip(clip_data.definition,clip_data.archive);RigPose pose(rig_data.definition,rig_data.archive);auto base=pose.sample(clip,3);PoseLayer layers[]={{&clip,3,1,{}},{&clip,17,1,frozen.weights}};auto masked=pose.blend(layers);for(auto joint:{index("Root_M"),index("Hip_L"),index("Ankle_R")})for(unsigned value=0;value<16;++value)require(std::abs(masked[joint].values[value]-base[joint].values[value])<.00002,"Native upper-body mask changed lower-body ancestry");
    }
    write(source/"upper.damask",mask_source);{AssetService fresh(source,root/"fresh");fresh.scan();fresh.cook("upper.damask");fresh.package(definition.id,root/"fresh.json");require(load_cooked_joint_mask(root/"fresh.json",fresh.cas_path(),definition.id).generation==frozen.generation,"Fresh cache stable mask identity/generation");}
    auto assembly=AssetId::random();Json scene={{"schema",1},{"asset",assembly.text()},{"entities",Json::object()},{"mounts",Json::object()}};EditorDocument doc(assembly,{8,1},{{assembly,scene.dump()},{rig.id,rig_source},{definition.id,mask_source}});auto edit=original;edit["weights"]["Wrist_R"]=.5;doc.commit(doc.prepare_source(doc.revision(),definition.id,edit.dump()),"Native joint mask edit");doc.undo(doc.revision());require(!doc.dirty(),"Mask undo restores original source");doc.redo(doc.revision());doc.save(root/"mask.dadoc");require(EditorDocument::open(root/"mask.dadoc")->source(definition.id)==edit.dump(),"Mask save/Open");auto revision=doc.revision();edit["weights"]["Unknown"]=1;rejected([&]{doc.prepare_source(revision,definition.id,edit.dump());});require(doc.revision()==revision,"Rejected mask prepare mutated document");
    edit=original;edit["weights"]["Wrist_R"]=2;rejected([&]{decode_joint_mask_source(edit.dump());});edit=original;edit["canonical_source"]="../outside.daskeleton";rejected([&]{decode_joint_mask_source(edit.dump());});edit=original;edit["skeleton"]=AssetId::random().text();rejected([&]{compile_joint_mask(decode_joint_mask_source(edit.dump()),rig);});
    auto malformed=mask_source;auto begin=malformed.find("\"weights\"");malformed.insert(begin,"\"schema\":1,");rejected([&]{decode_joint_mask_source(malformed);});
    std::cout<<"Native canonical-order masks, frozen cook generations, Ozz consumption and atomic editor undo/save/Open passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
