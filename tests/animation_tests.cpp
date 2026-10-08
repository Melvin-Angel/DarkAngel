#include <darkangel/animation_assets.hpp>
#include <darkangel/editor_document.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
void require(bool b,const char* s){
    if(!b)throw std::runtime_error(s);
}
std::string read(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);
    return {
        std::istreambuf_iterator<char>(in),{
        }
    };
}
int main(){
    try{
        auto project=std::filesystem::path(DAE_SOURCE_DIR);
        auto output=std::filesystem::path(DAE_BINARY_DIR)/("animation-fixture-"+AssetId::random().text());
        auto source=output/"sources";
        std::filesystem::create_directories(source);
        auto canonical=project/"content/animation/canonical_human.daskeleton";
        std::filesystem::copy_file(canonical,source/"human.daskeleton");
        std::filesystem::copy_file(project/".cache/fixtures/canonical-human.glb",source/"human.glb");
        auto original=read(canonical);
        auto definition=decode_rig_source(original);
        require(definition.human&&definition.joints.size()==81,"Actual canonical human bone count");
        AssetId human_id;
        std::string registry_bytes;
        {
            AssetService assets(source,output/"cache");
            require(assets.adopt("human.daskeleton")==definition.id,"Native skeleton embedded UUID preserved");
            auto id=assets.adopt_human("human.glb","human.daskeleton");
            human_id=id;
            auto skeleton=assets.cook("human.daskeleton");
            require(skeleton.changed&&!assets.cook("human.daskeleton").changed,"Ozz skeleton cold/warm cook");
            auto cooked=assets.cook("human.glb");
            require(cooked.changed&&!assets.cook("human.glb").changed,"Human binding cold/warm cook");
            assets.package(id,output/"registry.json");
            registry_bytes=read(output/"registry.json");
            auto loaded=load_cooked_rig(output/"registry.json",assets.cas_path(),definition.id);
            RigPose pose(loaded.definition,loaded.archive),second(loaded.definition,loaded.archive);
            const auto& matrices=pose.rest_pose();
            require(matrices.size()==81&&second.rest_pose().size()==81,"Independent Ozz CPU rest pose instances");
            require(std::abs(matrices[0].values[13]-definition.joints[0].translation[1])<.0001,"Ozz root metre normalization");
            auto binding=load_cooked_human_binding(output/"registry.json",assets.cas_path(),id);
            auto skin=Json::parse(binding);
            require(!skin.at("meshes").empty()&&skin.at("signature")==definition.signature,"Cooked CPU skin canonical binding");
            unsigned vertices{
            };
            for(auto mesh:skin.at("meshes"))for(auto vertex:mesh.at("vertices")){
                auto position=vertex.at("position").get<std::array<float,3>>();
                for(float value:position)require(std::isfinite(value)&&std::abs(value)<3,"Skin positions normalized to metres");
                for(auto joint:vertex.at("joints"))require(joint.get<unsigned>()<81,"Explicit skin to Ozz remap");
                ++vertices;
            }require(vertices>100,"Real human skin vertices");
            auto inverses=skin.at("inverse_bind").get<std::vector<std::array<float,16>>>();
            require(inverses.size()==81,"Cooked inverse bind palette");
            auto transform=[](const std::array<float,16>& matrix,std::array<float,3> value){
                std::array<float,3> result;
                for(unsigned row=0;row<3;++row)result[row]=matrix[row]*value[0]+matrix[4+row]*value[1]+matrix[8+row]*value[2]+matrix[12+row];
                return result;
            };
            for(auto mesh:skin.at("meshes"))for(auto vertex:mesh.at("vertices")){
                auto expected=vertex.at("position").get<std::array<float,3>>();
                std::array<float,3> result{
                };
                for(unsigned influence=0;influence<4;++influence){
                    unsigned joint=vertex.at("joints")[influence].get<unsigned>();
                    auto local=transform(inverses[joint],expected);
                    auto posed=transform(matrices[joint].values,local);
                    for(unsigned axis=0;axis<3;++axis)result[axis]+=posed[axis]*vertex.at("weights")[influence].get<float>();
                }for(unsigned axis=0;axis<3;++axis)require(std::abs(result[axis]-expected[axis])<.0005,"Ozz CPU reference bind-pose skinning error");
            }
            // Duplicate the real bound mesh in an owned temporary GLB. Its source
            // size remains legal, but its cooked skin exceeds the decoder work cap.
            auto glb=read(source/"human.glb");
            auto u32=[](std::string_view bytes,std::size_t offset){unsigned value{};for(unsigned i=0;i<4;++i)value|=static_cast<unsigned char>(bytes[offset+i])<<(8*i);return value;};
            auto json_size=u32(glb,12);auto gltf=Json::parse(glb.substr(20,json_size));
            std::size_t mesh_node{};for(std::size_t i=0;i<gltf["nodes"].size();++i)if(gltf["nodes"][i].contains("mesh")){mesh_node=i;break;}
            for(unsigned i=0;i<16;++i)gltf["nodes"].push_back(gltf["nodes"][mesh_node]);
            auto json_bytes=gltf.dump();while(json_bytes.size()%4)json_bytes+=' ';
            std::string oversized="glTF";auto append=[&](unsigned value){for(unsigned i=0;i<4;++i)oversized+=static_cast<char>(value>>(8*i));};
            append(2);append(static_cast<unsigned>(20+json_bytes.size()+glb.size()-20-json_size));append(static_cast<unsigned>(json_bytes.size()));append(0x4e4f534a);oversized+=json_bytes;oversized+=glb.substr(20+json_size);
            {std::ofstream out(source/"human.glb",std::ios::binary);out.write(oversized.data(),oversized.size());}
            bool work_rejected=false;try{assets.cook("human.glb");}catch(const std::exception& e){work_rejected=std::string(e.what()).find("work limit")!=std::string::npos;}
            require(work_rejected,"Oversized cooked skin rejected before publication");
            assets.package(id,output/"registry.json");require(load_cooked_human_binding(output/"registry.json",assets.cas_path(),id)==binding,"Cooked work overflow preserves prior coherent generation");
            {std::ofstream out(source/"human.glb",std::ios::binary);out.write(glb.data(),glb.size());}
            auto bad=Json::parse(original);
            bad["joints"][0]["translation"][1]=5;
            std::ofstream(source/"human.daskeleton")<<bad.dump();
            bool rejected=false;
            try{
                assets.cook("human.glb");
            }catch(...){
                rejected=true;
            }require(rejected,"Same joint names with incompatible rest pose rejected");
            assets.package(id,output/"registry.json");
            require(load_cooked_human_binding(output/"registry.json",assets.cas_path(),id)==binding,"Failed reimport preserves frozen coherent rig/skin generation");
            require(pose.rest_pose().size()==81,"Loaded old Ozz generation remains alive");
            std::ofstream(source/"human.daskeleton")<<original;
            std::cout<<"M5 canonical human joints=81 vertices="<<vertices<<" signature="<<definition.signature<<"\n";
        }
        {
            AssetService rebuilt(source,output/"cache-rebuilt");
            rebuilt.scan();
            rebuilt.cook("human.glb");
            rebuilt.package(human_id,output/"rebuilt.registry.json");
            require(read(output/"rebuilt.registry.json")==registry_bytes,"Fresh index/cache cook preserves UUIDs and byte-identical products");
        }
        auto root=AssetId::random();
        Json graph={
            {
                "schema",1
            },{
                "asset",root.text()
            },{
                "entities",Json::object()
            },{
                "mounts",Json::object()
            }
        };
        EditorDocument document(root,{
            7,1
        },{
            {
                root,graph.dump()
            },{
                definition.id,original
            }
        });
        auto edited=Json::parse(original);
        edited["sockets"]["weapon_right"]="Wrist_R";
        auto transaction=document.prepare_source(document.revision(),definition.id,edited.dump());
        document.commit(transaction,"M5 native authoring");
        require(document.dirty()&&document.history_head().origin=="M5 native authoring","Skeleton edit shared native history");
        document.undo(document.revision());
        require(!document.dirty(),"Skeleton source undo returns saved fingerprint");
        document.redo(document.revision());
        document.save(output/"scene.dadoc");
        auto reopened=EditorDocument::open(output/"scene.dadoc");
        require(reopened->source(definition.id)==document.source(definition.id)&&!reopened->dirty(),"Skeleton source journal save/Open");
        edited["sockets"]["weapon_right"]="missing-bone";
        bool rejected=false;
        try{
            document.prepare_source(document.revision(),definition.id,edited.dump());
        }catch(...){
            rejected=true;
        }require(rejected,"Invalid socket rejected before authoring commit");
        std::cout<<"M5 native skeleton prepare/commit/undo/redo/save/Open and invalid socket rejection passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
