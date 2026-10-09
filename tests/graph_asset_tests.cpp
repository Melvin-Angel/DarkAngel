#include <darkangel/graph_assets.hpp>
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool denied=false;try{f();}catch(const std::exception&){denied=true;}check(denied,"Expected graph rejection");}
Json read(const std::filesystem::path& path){std::ifstream file(path);return Json::parse(file);}
void write(const std::filesystem::path& path,const Json& value){std::ofstream(path)<<value.dump(2);}
}
int main(){try{
    auto root=std::filesystem::path(DAE_BINARY_DIR)/("graph-assets-"+AssetId::random().text()),sources=root/"sources";std::filesystem::create_directories(sources/"animation");auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content";
    std::filesystem::copy_file(content/"animation/canonical_human.daskeleton",sources/"animation/canonical_human.daskeleton");
    std::array<AssetId,2> clips;unsigned index{};for(auto name:{"idle","omni-walk"}){auto relative=std::string(name)+".glb";std::filesystem::copy_file(content/"royal_district/clips"/relative,sources/relative);auto metadata=read(content/"royal_district/clips"/(relative+".daimport"));write(sources/(relative+".daimport"),metadata);clips[index++]=AssetId::parse(metadata.at("id").get<std::string>());}
    auto graph_id=AssetId::random();Json graph={{"schema",1},{"kind","graph"},{"asset",graph_id.text()},{"root",10},{"nodes",Json::array({{{"id",1},{"kind","clip"},{"clip",clips[0].text()}},{{"id",2},{"kind","clip"},{"clip",clips[1].text()}},{{"id",10},{"kind","blend1d"},{"parameter","speed"},{"points",Json::array({{{"input",1},{"x",0.0},{"y",0.0}},{{"input",2},{"x",2.0},{"y",0.0}}})},{"triangles",Json::array()}}})},{"sources",{{clips[0].text(),"idle.glb"},{clips[1].text(),"omni-walk.glb"}}}};
    write(sources/"locomotion.dagraph",graph);std::string generation;CookedGraph pinned;
    {
        AssetService service(sources,root/"cache");check(service.adopt("locomotion.dagraph")==graph_id,"Embedded graph identity");service.cook("locomotion.dagraph");auto converted=service.conversion_count();check(!service.cook("locomotion.dagraph").changed&&service.conversion_count()==converted,"Warm graph skips conversion");service.package(graph_id,root/"registry.json");check(read(root/"registry.json").at("assets").size()==7,"Shared rig deduplicates in graph closure");pinned=load_cooked_graph(root/"registry.json",service.cas_path(),graph_id);AnimationGraphInstance instance(pinned.plan);generation=instance.state().generation;auto pose=instance.advance(1,{1});check(pose.count==2&&pose.layers[0].weight==.5f&&pose.layers[1].weight==.5f,"Cooked graph drives existing blend runtime");RigPose sampled(pinned.rig.definition,pinned.rig.archive);sampled.blend(pose.span());
        auto invalid=graph;invalid["nodes"][2]["points"][1]["input"]=10;write(sources/"locomotion.dagraph",invalid);rejects([&]{service.cook("locomotion.dagraph");});service.package(graph_id,root/"after-failure.json");check(AnimationGraphInstance(load_cooked_graph(root/"after-failure.json",service.cas_path(),graph_id).plan).state().generation==generation,"Cycle failure preserves usable head");
        invalid=graph;invalid["nodes"][2]["points"][1]["x"]=0;write(sources/"locomotion.dagraph",invalid);rejects([&]{service.cook("locomotion.dagraph");});invalid=graph;invalid["sources"][clips[0].text()]="../idle.glb";write(sources/"locomotion.dagraph",invalid);rejects([&]{service.cook("locomotion.dagraph");});write(sources/"locomotion.dagraph",graph);
        auto metadata=read(sources/"idle.glb.daimport");auto changed=metadata;changed["loop"]=false;write(sources/"idle.glb.daimport",changed);rejects([&]{service.cook("locomotion.dagraph");});changed=metadata;changed["subassets"]["runtime"]=AssetId::random().text();write(sources/"idle.glb.daimport",changed);check(service.cook("locomotion.dagraph").changed,"Dependency sidecar edit invalidates graph recipe");service.package(graph_id,root/"changed.json");auto newer=load_cooked_graph(root/"changed.json",service.cas_path(),graph_id);check(AnimationGraphInstance(newer.plan).state().generation!=generation&&AnimationGraphInstance(pinned.plan).state().generation==generation,"Old graph remains frozen across dependency replacement");
        auto mixed=read(root/"registry.json"),new_manifest=read(root/"changed.json");for(auto& record:mixed["assets"])if(record["id"]==clips[0].text())for(const auto& replacement:new_manifest["assets"])if(replacement["id"]==record["id"])record=replacement;write(root/"bad.json",mixed);rejects([&]{load_cooked_graph(root/"bad.json",service.cas_path(),graph_id);});
        auto missing=read(root/"registry.json");missing["assets"].erase(0);write(root/"bad.json",missing);rejects([&]{load_cooked_graph(root/"bad.json",service.cas_path(),graph_id);});write(sources/"idle.glb.daimport",metadata);service.cook("locomotion.dagraph");
        std::filesystem::rename(sources,root/"offline-sources");auto offline=load_cooked_graph(root/"registry.json",service.cas_path(),graph_id);check(AnimationGraphInstance(offline.plan).state().generation==generation,"Runtime graph loads without source mount/catalog");std::filesystem::rename(root/"offline-sources",sources);
    }
    {AssetService fresh(sources,root/"fresh");fresh.cook("locomotion.dagraph");fresh.package(graph_id,root/"fresh.json");check(AnimationGraphInstance(load_cooked_graph(root/"fresh.json",fresh.cas_path(),graph_id).plan).state().generation==generation,"Fresh cache reproduces graph generation");}
    std::cout<<"Frozen graph closure, source-free blending, failure preservation and generation fencing passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
