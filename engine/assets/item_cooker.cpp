#include "assets_internal.hpp"
#include <darkangel/item_assets.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel::assets_detail {
Import import_item(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,16384);auto data=json(bytes,16384);auto item=decode_item_definition(bytes);ItemCatalog catalog;catalog.insert(item);
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(item.id);
    std::map<AssetId,Product> products;std::vector<AssetId> dependencies;
    auto dependency=[&](const char* field,AssetId identity){
        if(identity==AssetId{})return;require(data.contains("sources")&&data.at("sources").contains(field),"Item asset reference needs a source locator");
        const auto path=within(root,data.at("sources").at(field).get<std::string>());const auto ext=path.extension();Import imported;
        if(std::string_view(field)=="mask"){
#ifdef DAE_ANIMATION
            require(ext==".dafacemask"&&AssetId::parse(json(read(path,65536)).at("asset").get<std::string>())==identity,"Item mask reference mismatch");imported=import_actor(root,path,{{"$source",identity}},inspect);
#else
            throw std::runtime_error("Mask item cooking requires animation tools");
#endif
        }else{
            auto sidecar=path;sidecar+=".daimport";const auto metadata_bytes=read(sidecar,1024*1024);const auto metadata=json(metadata_bytes);
            require(AssetId::parse(metadata.at("id").get<std::string>())==identity,"Item asset source identity mismatch");
            std::map<std::string,AssetId> mapping{{"$source",identity}};for(const auto& [key,value]:metadata.at("subassets").items())mapping.emplace(key,AssetId::parse(value.get<std::string>()));
            if(std::string_view(field)=="icon"){require(metadata.at("importer")=="texture-color-v1","Item image must be an imported color texture");imported=import_texture(root,path,mapping,inspect);}
            else {require(metadata.at("importer")=="static-gltf-v1","Item world model must be an imported static model");imported=import_gltf(root,path,mapping,inspect,static_model_collision(metadata));}
            imported.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(metadata_bytes);
        }
        dependencies.push_back(identity);for(auto id:imported.identities){require(id!=item.id,"Item dependency identity conflict");result.identities.push_back(id);}
        for(const auto& [path,hash]:imported.inputs){auto [at,added]=result.inputs.emplace(path,hash);require(added||at->second==hash,"Item dependency changed during preparation");}
        result.product_count+=imported.product_count?imported.product_count:imported.keys.size()+1;
        for(auto& product:imported.products){auto [at,added]=products.emplace(product.id,product);require(added||(at->second.bytes==product.bytes&&at->second.kind==product.kind&&at->second.extension==product.extension&&at->second.required==product.required),"Item conflicting dependency generations");}
    };
    dependency("icon",item.icon);dependency("model",item.model);dependency("mask",item.mask);++result.product_count;
    if(!inspect){require(ids.size()==1&&ids.at("$source")==item.id,"Item source identity mismatch");data.erase("sources");data["frozen"]=Json::object();for(auto& [id,product]:products){data["frozen"][id.text()]={{"sha256",sha256(product.bytes)},{"kind",product.kind},{"extension",product.extension}};result.products.push_back(std::move(product));}result.products.push_back({item.id,"item","item.json",data.dump(),dependencies});result.product_count=result.products.size();}
    return result;
}
}
namespace darkangel {
std::vector<CookedItem> load_cooked_items(const std::filesystem::path& registry,const std::filesystem::path& cas){
    using namespace assets_detail;auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array(),"Item registry schema");std::vector<CookedItem> items;
    for(const auto& record:manifest.at("assets"))if(record.at("kind")=="item"){require(items.size()<1024,"Item registry limit");items.push_back(load_cooked_item(registry,cas,AssetId::parse(record.at("id").get<std::string>())));}
    return items;
}
CookedItem load_cooked_item(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    using namespace assets_detail;const auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Item registry bound");std::map<AssetId,Json> records;
    for(const auto& record:manifest.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Item duplicate registry identity");
    auto found=records.find(id);require(found!=records.end()&&found->second.at("kind")=="item"&&found->second.at("extension")=="item.json","Item product missing/type mismatch");
    const auto digest=found->second.at("sha256").get<std::string>();require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==digest.npos,"Item digest shape");auto bytes=read(cas/(digest+".item.json"),65536);require(sha256(bytes)==digest,"Item product hash mismatch");auto data=json(bytes,65536);const auto frozen=data.at("frozen");data.erase("frozen");auto item=decode_item_definition(data.dump());ItemCatalog catalog;catalog.insert(item);require(item.id==id&&frozen.is_object()&&frozen.size()<=511,"Item frozen identity/bound");
    for(auto dependency:{item.icon,item.model,item.mask})if(dependency!=AssetId{})require(frozen.contains(dependency.text()),"Item frozen dependency missing");
    for(const auto& [key,value]:frozen.items()){auto record=records.find(AssetId::parse(key));require(record!=records.end()&&record->second.at("sha256")==value.at("sha256")&&record->second.at("kind")==value.at("kind")&&record->second.at("extension")==value.at("extension"),"Item mixed dependency generation");auto hash=value.at("sha256").get<std::string>(),extension=value.at("extension").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos&&extension.size()<=32&&extension.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")==extension.npos,"Item dependency digest/extension");require(file_sha256(cas/(hash+"."+extension))==hash,"Item dependency hash mismatch");}
    return {std::move(item),digest};
}
}
