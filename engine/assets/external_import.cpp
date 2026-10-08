#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace darkangel {
using namespace assets_detail;
namespace {
constexpr std::size_t closure_limit=128*1024*1024;
struct Input {std::filesystem::path path;std::string hash;};
std::string lower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
void write(const std::filesystem::path& path,std::string_view bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();require(!out.fail(),"Import staging write failed");}
struct Scratch {
    std::filesystem::path root,base;
    explicit Scratch(const std::filesystem::path& parent):base(std::filesystem::weakly_canonical(parent)){
        std::filesystem::create_directories(base);root=within(base,".import-"+AssetId::random().text());require(std::filesystem::create_directory(root),"Import staging directory collision");
    }
    ~Scratch(){std::error_code ignored;if(root.parent_path()==base&&root.filename().string().starts_with(".import-"))std::filesystem::remove_all(root,ignored);}
};
std::uint32_t u32(std::string_view bytes,std::size_t at){require(at+4<=bytes.size(),"Truncated GLB header");std::uint32_t value{};for(unsigned i=0;i<4;++i)value|=std::uint32_t(static_cast<unsigned char>(bytes[at+i]))<<(8*i);return value;}
void append32(std::string& out,std::uint32_t value){for(unsigned i=0;i<4;++i)out+=static_cast<char>(value>>(8*i));}
struct Interchange {Json document;std::string tail;bool binary{};};
Interchange interchange(std::string_view bytes,bool binary){
    if(!binary)return {json(bytes,64*1024*1024),{},false};
    require(bytes.size()>=20&&u32(bytes,0)==0x46546c67&&u32(bytes,4)==2&&u32(bytes,8)==bytes.size()&&u32(bytes,16)==0x4e4f534a,"Unsupported GLB container");
    auto size=u32(bytes,12);require(size%4==0&&size<=bytes.size()-20,"GLB JSON chunk bounds");
    auto end=20+std::size_t(size);require(end==bytes.size()||(end+8<=bytes.size()&&u32(bytes,end+4)==0x004e4942&&u32(bytes,end)%4==0&&end+8+u32(bytes,end)==bytes.size()),"GLB binary chunk bounds/profile");
    return {json(bytes.substr(20,size),64*1024*1024),std::string(bytes.substr(end)),true};
}
std::string normalized(Interchange& source,AssetImportType type){
    if(type==AssetImportType::Model||type==AssetImportType::SkinnedMesh){
        for(auto category:{"meshes","materials"})if(source.document.contains(category)){
            auto& entries=source.document[category];require(entries.is_array()&&entries.size()<=128,"Import mesh/material limit");
            for(std::size_t i=0;i<entries.size();++i){auto& entry=entries[i];if(!entry.contains("extras"))entry["extras"]=Json::object();require(entry["extras"].is_object(),"Import extras must be an object");if(!entry["extras"].contains("darkangel_key"))entry["extras"]["darkangel_key"]=std::string(category==std::string_view("meshes")?"mesh/":"material/")+std::to_string(i);}
        }
    }
    auto bytes=source.document.dump();if(!source.binary)return bytes+"\n";
    while(bytes.size()%4)bytes+=' ';std::string result;append32(result,0x46546c67);append32(result,2);append32(result,static_cast<std::uint32_t>(20+bytes.size()+source.tail.size()));append32(result,static_cast<std::uint32_t>(bytes.size()));append32(result,0x4e4f534a);result+=bytes;result+=source.tail;return result;
}
Import convert(AssetImportType type,const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,const std::filesystem::path& canonical,bool loop,bool inspect){
    if(type==AssetImportType::Model)return import_gltf(root,source,ids,inspect);
    if(type==AssetImportType::Texture)return import_texture(root,source,ids,inspect);
#ifdef DAE_ANIMATION
    if(type==AssetImportType::SkinnedMesh)return import_human(root,source,ids,canonical,inspect,true);
    if(type==AssetImportType::Animation)return import_clip(root,source,ids,canonical,loop,inspect);
#endif
    throw std::runtime_error("Selected import type is unavailable in this build");
}
std::string importer(AssetImportType type){switch(type){case AssetImportType::Model:return "static-gltf-v1";case AssetImportType::SkinnedMesh:return "human-gltf-v2";case AssetImportType::Animation:return "clip-gltf-v1";case AssetImportType::Texture:return "texture-color-v1";default:throw std::runtime_error("Unsupported import type");}}
}
struct PreparedAssetImport::State {
    AssetId owner;std::filesystem::path project,directory;
    std::string relative,canonical;AssetId id;bool consumed{};
    std::vector<Input> inputs;std::map<std::string,std::string> files;
};
std::string PreparedAssetImport::destination() const{require(bool(state_),"Missing prepared import");return state_->relative;}
std::vector<std::string> PreparedAssetImport::files() const{require(bool(state_),"Missing prepared import");std::vector<std::string> result;for(const auto& [path,bytes]:state_->files)result.push_back(path);return result;}
AssetId PreparedAssetImport::id() const{require(bool(state_),"Missing prepared import");return state_->id;}
std::string_view asset_import_label(AssetImportType type){switch(type){case AssetImportType::Model:return "3D model";case AssetImportType::SkinnedMesh:return "Skinned mesh";case AssetImportType::Animation:return "Animation";case AssetImportType::Texture:return "Color texture";case AssetImportType::Vfx:return "VFX";case AssetImportType::Audio:return "Audio";case AssetImportType::Material:return "Material";case AssetImportType::UI:return "UI";}return "Unknown";}
std::string_view asset_import_folder(AssetImportType type){switch(type){case AssetImportType::Model:return "models";case AssetImportType::SkinnedMesh:return "characters";case AssetImportType::Animation:return "animation/clips";case AssetImportType::Texture:return "textures";case AssetImportType::Vfx:return "vfx";case AssetImportType::Audio:return "audio";case AssetImportType::Material:return "materials";case AssetImportType::UI:return "ui";}return {};}
std::string_view asset_import_support(AssetImportType type){switch(type){case AssetImportType::Model:case AssetImportType::Texture:return {};case AssetImportType::SkinnedMesh:case AssetImportType::Animation:
#ifdef DAE_ANIMATION
return {};
#else
return "Requires an animation-enabled editor build.";
#endif
case AssetImportType::Vfx:return "VFX import and preview are planned for M7.";case AssetImportType::Audio:return "Audio import and bank authoring are planned for M7.";case AssetImportType::Material:return "Standalone material authoring is planned; supported model materials import with their model.";case AssetImportType::UI:return "UI document import is planned with the native runtime UI workflow.";}return "Unknown import type.";}

Import assets_detail::import_texture(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,32*1024*1024);auto cooked=cook_color_texture(bytes);Import result;result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    if(!inspect){require(ids.size()==1&&ids.contains("$source"),"Texture UUID mapping mismatch");result.products.push_back({ids.at("$source"),"texture","dds",std::move(cooked),{}});}return result;
}
PreparedAssetImport AssetService::prepare_import(const AssetImportRequest& request){
    auto project=source_root();require(asset_import_support(request.type).empty(),"Selected asset import category is not implemented");
    auto external=std::filesystem::canonical(request.file);require(std::filesystem::is_regular_file(external),"Choose a regular source file");
    auto external_name=lower(external.generic_string()),project_name=lower(project.generic_string())+"/";require(!external_name.starts_with(project_name),"File is already in this project; use its existing AssetID");
    auto extension=lower(external.extension().string());const bool texture=request.type==AssetImportType::Texture;
    require(texture?(extension==".png"||extension==".jpg"||extension==".jpeg"):(extension==".gltf"||extension==".glb"),"Unsupported source format: use glTF/GLB or PNG/JPEG color textures. FBX needs offline conversion.");
    require(request.type!=AssetImportType::SkinnedMesh||extension==".glb","Renderable canonical skins require GLB with an embedded atlas");
    auto state=std::make_shared<PreparedAssetImport::State>();state->owner=import_identity();state->project=project;state->id=AssetId::random();
    std::string slug;for(unsigned char c:external.stem().string())slug+=std::isalnum(c)&&c<128?static_cast<char>(c):'-';require(!slug.empty()&&slug.size()<=96,"Import name length limit");
    state->directory=std::filesystem::path(asset_import_folder(request.type))/(slug+"-"+state->id.text().substr(0,8));
    auto filename=slug+extension;state->relative=(state->directory/filename).generic_string();require(!std::filesystem::exists(within(project,state->directory)),"Import destination already exists");
    std::size_t total{};auto snapshot=[&](const std::filesystem::path& path){auto bytes=read(path);total+=bytes.size();require(total<=closure_limit&&state->inputs.size()<256,"Import closure size/file limit");state->inputs.push_back({path,sha256(bytes)});return bytes;};
    auto bytes=snapshot(external);std::map<std::string,std::string> staged;
    if(!texture){auto data=interchange(bytes,extension==".glb");std::set<std::string> dependencies;
        for(auto category:{"buffers","images"})if(data.document.contains(category)){const auto& entries=data.document[category];require(entries.is_array()&&entries.size()<=128,"Import dependency count limit");for(const auto& entry:entries)if(entry.contains("uri")){
            auto uri=entry.at("uri").get<std::string>();require(!uri.empty()&&uri.find_first_of("%:\\")==uri.npos&&uri.front()!='/',"Only safe local glTF dependency files are supported");
            auto dependency=within(external.parent_path(),uri);auto relative=dependency.lexically_relative(external.parent_path()).generic_string();require(lower(relative)!=lower(filename),"Import dependency conflicts with source filename");
            if(dependencies.insert(lower(relative)).second){auto content=snapshot(dependency);require(staged.emplace(relative,std::move(content)).second,"Duplicate import dependency");}
        }}bytes=normalized(data,request.type);
    }
    staged[filename]=std::move(bytes);
    const bool canonical=request.type==AssetImportType::SkinnedMesh||request.type==AssetImportType::Animation;
    Scratch scratch(cas_path().parent_path()/"import-validation");
    if(canonical){auto rig=within(project,request.canonical_skeleton);require(rig.extension()==".daskeleton","Choose a native canonical skeleton in this project");state->canonical=rig.lexically_relative(project).generic_string();write(within(scratch.root,state->canonical),snapshot(rig));}
    for(const auto& [path,content]:staged)write(within(scratch.root,state->directory/path),content);
    auto candidate=within(scratch.root,state->relative);auto rig=canonical?within(scratch.root,state->canonical):std::filesystem::path{};
    auto inspected=convert(request.type,scratch.root,candidate,{},rig,request.loop,true);std::map<std::string,AssetId> ids{{"$source",state->id}};
    Json metadata={{"schema",1},{"id",state->id.text()},{"importer",importer(request.type)},{"tags",Json::array()},{"subassets",Json::object()}};
    for(const auto& key:inspected.keys){auto id=AssetId::random();require(ids.emplace(key,id).second,"Duplicate imported subasset key");metadata["subassets"][key]=id.text();}
    if(canonical)metadata["canonical_source"]=state->canonical;if(request.type==AssetImportType::Animation)metadata["loop"]=request.loop;
    auto converted=convert(request.type,scratch.root,candidate,ids,rig,request.loop,false);require(!converted.products.empty(),"Import produced no cooked products");for(const auto& product:converted.products)require(product.bytes.size()<=64*1024*1024,"Imported product size limit");
    staged[filename+".daimport"]=metadata.dump(2)+"\n";state->files=std::move(staged);
    for(const auto& input:state->inputs)require(file_sha256(input.path)==input.hash,"Source changed during import validation; validate again");
    PreparedAssetImport prepared;prepared.state_=std::move(state);return prepared;
}
CookResult AssetService::commit_import(const PreparedAssetImport& prepared){
    auto project=source_root();require(bool(prepared.state_),"Validate an import before committing");auto& state=*prepared.state_;require(state.owner==import_identity()&&state.project==project&&!state.consumed,"Stale or foreign prepared import");
    auto destination=within(project,state.directory);require(!std::filesystem::exists(destination),"Import destination changed; validate again");
    for(const auto& input:state.inputs)require(file_sha256(input.path)==input.hash,"Source changed since validation; validate again");
    scan();auto base=within(project,state.directory.parent_path());Scratch scratch(base);
    for(const auto& [path,bytes]:state.files)write(within(scratch.root,path),bytes);
    bool published=false;try{
        for(const auto& input:state.inputs)require(file_sha256(input.path)==input.hash,"Source changed during import; validate again");
        std::filesystem::rename(scratch.root,destination);published=true;auto result=cook(state.relative);state.consumed=true;return result;
    }catch(...){if(published){std::error_code ignored;require(destination.parent_path()==base,"Unsafe import rollback path");std::filesystem::remove_all(destination,ignored);scan();}throw;}
}
CookedTexture load_cooked_texture(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Texture registry contract");
    std::set<AssetId> ids;const Json* selected{};for(const auto& record:manifest.at("assets")){auto asset=AssetId::parse(record.at("id").get<std::string>());require(ids.insert(asset).second,"Duplicate texture registry AssetID");if(asset==id)selected=&record;}
    require(selected&&selected->at("kind")=="texture"&&selected->at("extension")=="dds","Texture registry type mismatch");auto hash=selected->at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Invalid texture digest");auto bytes=read(cas/(hash+".dds"));require(sha256(bytes)==hash,"Texture digest mismatch");return decode_texture(bytes);
}
}
