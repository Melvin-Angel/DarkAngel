#include "assets_internal.hpp"
#include <darkangel/graph_assets.hpp>
#include <darkangel/hash.hpp>
#include <cmath>
#include <functional>
#include <set>

namespace darkangel {
using namespace assets_detail;
namespace {
void fields(const Json& value,std::initializer_list<const char*> names){
    require(value.is_object()&&value.size()==names.size(),"Graph field count");
    for(auto name:names)require(value.contains(name),"Graph missing/unknown field");
}
unsigned integer(const Json& value){require(value.is_number_unsigned()&&value.get<std::uint64_t>()<=UINT32_MAX,"Graph integer type/range");return value.get<unsigned>();}
float scalar(const Json& value){require(value.is_number(),"Graph scalar type");auto result=value.get<double>();require(std::isfinite(result)&&std::abs(result)<=100,"Graph scalar bounds");return static_cast<float>(result);}
AssetId id(const Json& value){return AssetId::parse(value.get<std::string>());}
struct Source {AssetId asset;unsigned root{};std::vector<GraphNode> nodes;std::map<unsigned,AssetId> clips;};
Source decode(const Json& data){
    fields(data,{"schema","kind","asset","root","nodes"});require(data.at("schema")==1&&data.at("kind")=="graph","Graph source schema/type");
    Source result;result.asset=id(data.at("asset"));result.root=integer(data.at("root"));require(result.root,"Graph root ID");
    const auto& nodes=data.at("nodes");require(nodes.is_array()&&!nodes.empty()&&nodes.size()<=32,"Graph node count");std::set<unsigned> unique;
    for(const auto& entry:nodes){
        GraphNode node;node.id=integer(entry.at("id"));require(node.id&&unique.insert(node.id).second,"Graph node identity");auto kind=entry.at("kind").get<std::string>();
        if(kind=="clip"){fields(entry,{"id","kind","clip"});node.kind=GraphNodeKind::Clip;result.clips.emplace(node.id,id(entry.at("clip")));}
        else{
            fields(entry,{"id","kind","parameter","points","triangles"});require(kind=="blend1d"||kind=="blend2d","Graph node kind");node.kind=kind=="blend1d"?GraphNodeKind::Blend1D:GraphNodeKind::Blend2D;
            auto parameter=entry.at("parameter").get<std::string>();require(parameter=="speed"||parameter=="forward"||parameter=="lateral","Graph parameter");node.parameter=parameter=="speed"?GraphParameter::Speed:parameter=="forward"?GraphParameter::Forward:GraphParameter::Lateral;
            const auto& points=entry.at("points");require(points.is_array()&&points.size()>=2&&points.size()<=32,"Graph points count");
            for(const auto& point:points){fields(point,{"input","x","y"});node.points.push_back({integer(point.at("input")),scalar(point.at("x")),scalar(point.at("y"))});}
            const auto& triangles=entry.at("triangles");require(triangles.is_array()&&triangles.size()<=64,"Graph triangle count");
            for(const auto& triangle:triangles){require(triangle.is_array()&&triangle.size()==3,"Graph triangle shape");node.triangles.push_back({integer(triangle[0]),integer(triangle[1]),integer(triangle[2])});}
        }
        result.nodes.push_back(std::move(node));
    }
    require(!result.clips.empty(),"Graph clip catalogue required");return result;
}
std::shared_ptr<const AnimationGraphPlan> compile(const Json& data,const std::map<AssetId,std::shared_ptr<const AnimationClip>>& clips){
    auto source=decode(data);for(auto& node:source.nodes)if(node.kind==GraphNodeKind::Clip)node.clip=clips.at(source.clips.at(node.id));
    return std::make_shared<const AnimationGraphPlan>(sha256(data.dump()),source.root,std::move(source.nodes));
}
struct Registry {
    std::map<AssetId,Json> records;std::filesystem::path cas;
    Registry(const std::filesystem::path& path,const std::filesystem::path& cache):cas(cache){
        auto manifest=json(read(path,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&!manifest.at("assets").empty()&&manifest.at("assets").size()<=512,"Graph registry schema/count");
        for(const auto& record:manifest.at("assets"))require(records.emplace(id(record.at("id")),record).second,"Duplicate graph product identity");require(records.contains(id(manifest.at("root"))),"Graph registry root missing");
    }
    Json load(AssetId asset)const{
        const auto& record=records.at(asset);require(record.at("kind")=="graph"&&record.at("extension")=="graph.json","Graph product type");auto digest=record.at("sha256").get<std::string>();require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==digest.npos,"Graph product digest");auto bytes=read(cas/(digest+".graph.json"),65536);require(sha256(bytes)==digest,"Graph product hash mismatch");return json(bytes,65536);
    }
};
}
CookedGraph load_cooked_graph(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId asset){
    Registry files(registry,cas);auto data=files.load(asset);auto frozen=data.at("frozen");data.erase("frozen");auto source=decode(data);require(source.asset==asset&&frozen.is_object()&&!frozen.empty()&&frozen.size()<=128,"Graph frozen catalogue");
    // Fence every transitive product, including the rig archive, against the
    // parent's frozen closure before constructing any runtime plan.
    for(const auto& [key,digest]:frozen.items()){auto dependency=AssetId::parse(key);require(files.records.at(dependency).at("sha256")==digest,"Graph frozen dependency generation mismatch");}
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips;CookedRig rig;
    for(const auto& [node,clip_id]:source.clips){
        if(clips.contains(clip_id))continue;auto clip=load_cooked_clip(registry,cas,clip_id);
        for(auto dependency:{clip_id,clip.definition.runtime,clip.definition.skeleton})require(frozen.contains(dependency.text()),"Graph missing frozen dependency");
        auto current_rig=load_cooked_rig(registry,cas,clip.definition.skeleton);require(frozen.contains(current_rig.definition.runtime.text()),"Graph missing frozen rig archive");rig=std::move(current_rig);
        clips.emplace(clip_id,std::make_shared<const AnimationClip>(std::move(clip.definition),clip.archive));
    }
    // Use the same seed as the cooker, including frozen generations.
    auto plan_data=data;plan_data["frozen"]=frozen;auto graph_source=decode(data);for(auto& node:graph_source.nodes)if(node.kind==GraphNodeKind::Clip)node.clip=clips.at(graph_source.clips.at(node.id));
    return {asset,std::move(rig),std::make_shared<const AnimationGraphPlan>(sha256(plan_data.dump()),graph_source.root,std::move(graph_source.nodes))};
}
namespace assets_detail {
Import import_graph(const std::filesystem::path& root,const std::filesystem::path& path,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(path,65536);auto data=json(bytes,65536);auto sources=data.at("sources");data.erase("sources");auto graph=decode(data);require(sources.is_object()&&!sources.empty()&&sources.size()<=32,"Graph source clip locators");if(!inspect)require(ids.size()==1&&ids.at("$source")==graph.asset,"Graph source identity mismatch");
    Import result;result.inputs[path.lexically_relative(root).generic_string()]=sha256(bytes);std::map<AssetId,Product> products;std::set<AssetId> closure;std::set<AssetId> referenced;std::map<AssetId,std::shared_ptr<const AnimationClip>> clips;
    for(const auto& [node,clip]:graph.clips)referenced.insert(clip);require(referenced.size()==sources.size(),"Graph locator catalogue must match references");
    for(auto clip:referenced){
        auto clip_path=within(root,sources.at(clip.text()).get<std::string>());require(clip_path.extension()==".glb","Graph clip source type");auto sidecar=clip_path;sidecar+=".daimport";auto sidecar_bytes=read(sidecar,65536);auto metadata=json(sidecar_bytes,65536);
        require(metadata.at("schema")==1&&metadata.at("importer")=="clip-gltf-v1"&&id(metadata.at("id"))==clip&&metadata.at("subassets").is_object()&&metadata.at("subassets").size()==1,"Graph clip import identity/profile");
        auto runtime=id(metadata.at("subassets").at("runtime"));auto canonical=within(root,metadata.at("canonical_source").get<std::string>());require(canonical.extension()==".daskeleton","Graph canonical source type");auto rig=decode_rig_source(read(canonical,1024*1024));
        std::set<AssetId> distinct{clip,runtime,rig.id,rig.runtime};require(distinct.size()==4&&!distinct.contains(graph.asset),"Graph dependency identity conflict");closure.insert(distinct.begin(),distinct.end());
        auto imported=import_clip(root,clip_path,{{"$source",clip},{"runtime",runtime}},canonical,metadata.at("loop").get<bool>(),inspect);require(metadata.at("loop")==true,"Graph locomotion clip must loop");result.inputs.insert(imported.inputs.begin(),imported.inputs.end());result.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(sidecar_bytes);
        if(inspect)continue;
        for(auto& product:imported.products){auto [found,inserted]=products.emplace(product.id,product);require(inserted||(found->second.kind==product.kind&&found->second.extension==product.extension&&found->second.bytes==product.bytes&&found->second.required==product.required),"Graph conflicting shared dependency generation");}
        auto definition=decode_clip_manifest(products.at(clip).bytes);clips.emplace(clip,std::make_shared<const AnimationClip>(std::move(definition),products.at(runtime).bytes));
    }
    result.product_count=closure.size()+1;result.identities.assign(closure.begin(),closure.end());result.identities.push_back(graph.asset);if(inspect)return result;
    require(products.size()==closure.size(),"Graph closure product count");auto frozen=Json::object();std::vector<AssetId> dependencies;
    for(auto& [asset,product]:products){frozen[asset.text()]=sha256(product.bytes);dependencies.push_back(asset);result.products.push_back(std::move(product));}
    // Compile the actual clips and topology before publishing a catalog head.
    auto plan=compile(data,clips);data["frozen"]=std::move(frozen);result.products.push_back({graph.asset,"graph","graph.json",data.dump(),std::move(dependencies)});return result;
}
}
}
