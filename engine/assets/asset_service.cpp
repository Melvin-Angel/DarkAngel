#include "assets_internal.hpp"
#include <darkangel/hash.hpp>
#include <sqlite3.h>
#include <DirectXTex.h>
#include <wincodec.h>
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <thread>
#include <functional>
#include <cstring>
#include <cctype>
#include <atomic>

namespace darkangel {
using namespace assets_detail;
namespace {
std::atomic_flag active_coordinator=ATOMIC_FLAG_INIT; // DirectXTex WIC factory is process-global
struct Statement {
    sqlite3_stmt* p{};
    Statement(sqlite3* db,const char* sql){require(sqlite3_prepare_v2(db,sql,-1,&p,nullptr)==SQLITE_OK,"Asset catalog prepare failed");}
    ~Statement(){sqlite3_finalize(p);}
    void id(int index,AssetId id){require(sqlite3_bind_blob(p,index,id.bytes.data(),16,SQLITE_TRANSIENT)==SQLITE_OK,"Asset ID bind failed");}
    void text(int index,std::string_view value){require(sqlite3_bind_text(p,index,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT)==SQLITE_OK,"Asset catalog bind failed");}
    bool row(){auto code=sqlite3_step(p);require(code==SQLITE_ROW || code==SQLITE_DONE,"Asset catalog step failed");return code==SQLITE_ROW;}
    AssetId id(int column) const{require(sqlite3_column_bytes(p,column)==16,"Invalid catalog AssetID");AssetId id;std::memcpy(id.bytes.data(),sqlite3_column_blob(p,column),16);AssetId::parse(id.text());return id;}
    std::string text(int column) const{const auto* value=sqlite3_column_text(p,column);require(value,"Missing catalog value");return reinterpret_cast<const char*>(value);}
};
void exec(sqlite3* db,const char* sql){require(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"Asset catalog transaction/schema failed");}
void atomic_write(const std::filesystem::path& path,std::string_view bytes,bool replace=true){
    std::filesystem::create_directories(path.parent_path());auto tmp=path;tmp+=".stage-"+AssetId::random().text();
    try{std::ofstream out(tmp,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();require(!out.fail(),"Asset staging write failed");require(MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))!=0,"Asset atomic publication failed");}
    catch(...){std::error_code ignored;std::filesystem::remove(tmp,ignored);throw;}
}
std::map<std::string,AssetId> metadata_ids(const Json& sidecar){
    require(sidecar.at("schema")==1 && (sidecar.at("importer")=="static-gltf-v1"||sidecar.at("importer")=="native-collision-v1"||sidecar.at("importer")=="native-skeleton-v1"||sidecar.at("importer")=="human-gltf-v1"),"Unsupported import schema/profile");require(sidecar.at("tags").is_array() && sidecar.at("tags").size()<=64,"Import tag limit");
    for(const auto& tag:sidecar.at("tags"))require(tag.is_string() && tag.get<std::string>().size()<=128,"Import tag invalid");
    require(sidecar.at("subassets").is_object() && sidecar.at("subassets").size()<=512,"Subasset mapping limit");std::map<std::string,AssetId> ids{{"$source",AssetId::parse(sidecar.at("id").get<std::string>())}};std::set<AssetId> unique{ids.at("$source")};
    for(const auto& [key,value]:sidecar.at("subassets").items()){auto id=AssetId::parse(value.get<std::string>());require(!key.empty() && key.size()<=128 && key!="$source" && unique.insert(id).second,"Duplicate/invalid source subasset ID");ids.emplace(key,id);}return ids;
}
std::filesystem::path metadata_path(std::filesystem::path source){if(source.extension()==".dacollision"||source.extension()==".daskeleton")return source;source+=".daimport";return source;}
Json source_metadata(const std::filesystem::path& source){auto data=json(read(metadata_path(source),1024*1024));if(source.extension()==".dacollision"){require(data.at("schema")==1&&data.at("kind")=="collision","Native collision metadata");return {{"schema",1},{"id",data.at("asset")},{"importer","native-collision-v1"},{"tags",Json::array()},{"subassets",Json::object()}};}if(source.extension()==".daskeleton"){require(data.at("schema")==1&&data.at("kind")=="skeleton","Native skeleton metadata");return {{"schema",1},{"id",data.at("asset")},{"importer","native-skeleton-v1"},{"tags",Json::array()},{"subassets",{{"runtime",data.at("runtime")}}}};}return data;}
Import import_source(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    if(source.extension()==".dacollision")return import_collision(root,source,ids,inspect);
#if defined(DAE_ANIMATION)
    if(source.extension()==".daskeleton")return import_skeleton(root,source,ids,inspect);
    if(!ids.empty()&&source_metadata(source).at("importer")=="human-gltf-v1")return import_human(root,source,ids,within(root,source_metadata(source).at("canonical_source").get<std::string>()),inspect);
#endif
    return import_gltf(root,source,ids,inspect);
}
std::string lowercase(std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;}
void digest_check(std::string_view digest){require(digest.size()==64 && digest.find_first_not_of("0123456789abcdef")==digest.npos,"Invalid artifact digest");}
}
struct AssetService::Impl {
    std::filesystem::path sources,cache,cas;sqlite3* db{};std::thread::id owner{std::this_thread::get_id()};bool com{};std::uint64_t conversions{};
    Impl(std::filesystem::path source,std::filesystem::path output):sources(std::filesystem::weakly_canonical(source)),cache(std::filesystem::weakly_canonical(output)),cas(cache/"cas"){
        require(std::filesystem::is_directory(sources),"Asset source mount missing");std::filesystem::create_directories(cas);
        require(!active_coordinator.test_and_set(),"Only one asset coordinator may own the WIC cooker at a time");
        try{
            auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);com=SUCCEEDED(result);require(com || result==RPC_E_CHANGED_MODE,"Asset COM initialization failed");
            // DirectXTex's default INIT_ONCE factory cannot survive COM teardown/reinit.
            // Supply a fresh scoped factory instead of retaining that process-global pointer.
            IWICImagingFactory2* factory{};require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"Asset WIC factory initialization failed");DirectX::SetWICFactory(factory);factory->Release();
            require(sqlite3_open_v2((cache/"catalog.sqlite").string().c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)==SQLITE_OK,"Asset catalog open failed");sqlite3_busy_timeout(db,2000);exec(db,"PRAGMA foreign_keys=ON;PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;");
            Statement version(db,"PRAGMA user_version");require(version.row() && sqlite3_column_int(version.p,0)<=1,"Unsupported asset catalog schema");
            exec(db,"CREATE TABLE IF NOT EXISTS assets(id BLOB PRIMARY KEY CHECK(length(id)=16),path TEXT UNIQUE NOT NULL);CREATE TABLE IF NOT EXISTS generations(root BLOB PRIMARY KEY,revision INTEGER NOT NULL,recipe TEXT NOT NULL);CREATE TABLE IF NOT EXISTS products(root BLOB NOT NULL,id BLOB NOT NULL,kind TEXT NOT NULL,hash TEXT NOT NULL,extension TEXT NOT NULL,PRIMARY KEY(root,id));CREATE TABLE IF NOT EXISTS dependencies(root BLOB NOT NULL,owner BLOB NOT NULL,target BLOB NOT NULL,kind INTEGER NOT NULL CHECK(kind=1),PRIMARY KEY(root,owner,target));PRAGMA user_version=1;PRAGMA application_id=1145128257;");
        }catch(...){if(db)sqlite3_close(db);DirectX::SetWICFactory(nullptr);if(com)CoUninitialize();active_coordinator.clear();throw;}
    }
    ~Impl(){sqlite3_close(db);DirectX::SetWICFactory(nullptr);if(com)CoUninitialize();active_coordinator.clear();}
    void thread() const{require(owner==std::this_thread::get_id(),"AssetService writer accessed from wrong thread");}
    std::filesystem::path source(std::string_view relative) const{auto p=within(sources,std::filesystem::path(relative));require(p.extension()==".glb" || p.extension()==".gltf" || p.extension()==".dacollision" || p.extension()==".daskeleton","Unsupported asset source extension");return p;}
};
AssetService::AssetService(std::filesystem::path source,std::filesystem::path cache):impl_(std::make_unique<Impl>(std::move(source),std::move(cache))){}
AssetService::~AssetService()=default;
AssetId AssetService::adopt(std::string_view relative){auto& p=*impl_;p.thread();auto source=p.source(relative),sidecar_path=metadata_path(source);
    if(source.extension()==".dacollision"||source.extension()==".daskeleton"){import_source(p.sources,source,{},true);auto id=AssetId::parse(source_metadata(source).at("id").get<std::string>());return id;}
    require(!std::filesystem::exists(sidecar_path),"Asset already has metadata; preserve its identity");auto imported=import_source(p.sources,source,{},true);auto id=AssetId::random();Json sidecar={{"schema",1},{"id",id.text()},{"importer",source.extension()==".dacollision"?"native-collision-v1":"static-gltf-v1"},{"tags",Json::array()},{"subassets",Json::object()}};
    for(const auto& key:imported.keys)sidecar["subassets"][key]=AssetId::random().text();for(const auto& [path,hash]:imported.inputs)require(file_sha256(within(p.sources,path))==hash,"Source changed during adoption");atomic_write(sidecar_path,sidecar.dump(2)+"\n",false);return id;
}
AssetId AssetService::adopt_human(std::string_view relative,std::string_view canonical){
#if defined(DAE_ANIMATION)
    auto& p=*impl_;p.thread();auto source=p.source(relative);require(source.extension()==".glb"||source.extension()==".gltf","Human interchange extension");require(!std::filesystem::exists(metadata_path(source)),"Preserve existing human metadata/UUIDs");auto rig=within(p.sources,std::filesystem::path(canonical));require(rig.extension()==".daskeleton","Human canonical source type");auto inspected=import_human(p.sources,source,{},rig,true);auto id=AssetId::random();Json sidecar={{"schema",1},{"id",id.text()},{"importer","human-gltf-v1"},{"canonical_source",std::string(canonical)},{"tags",Json::array()},{"subassets",Json::object()}};for(auto key:inspected.keys)sidecar["subassets"][key]=AssetId::random().text();for(auto [path,hash]:inspected.inputs)require(file_sha256(within(p.sources,path))==hash,"Human source changed during adoption");atomic_write(metadata_path(source),sidecar.dump(2)+"\n",false);return id;
#else
    throw std::runtime_error("Human import requires explicit animation tools build");
#endif
}
void AssetService::scan(){auto& p=*impl_;p.thread();std::vector<std::pair<AssetId,std::string>> sources;std::set<AssetId> ids;std::set<std::string> paths;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(p.sources))if(entry.is_regular_file()){
        auto extension=entry.path().extension();auto relative=entry.path().lexically_relative(p.sources).generic_string();
        require(paths.insert(lowercase(relative)).second,"Case-colliding asset source paths");
        if(extension==".daimport"){auto original=entry.path();original.replace_extension();require(std::filesystem::is_regular_file(original),"Orphan import metadata");}
        if(extension!=".glb" && extension!=".gltf" && extension!=".dacollision" && extension!=".daskeleton")continue;
        auto canonical=within(p.sources,relative);auto sidecar=metadata_path(canonical);require(std::filesystem::is_regular_file(sidecar),"Source has no sidecar; explicit adoption required");
        auto mapping=metadata_ids(source_metadata(canonical));for(const auto& [name,id]:mapping)require(ids.insert(id).second,"Duplicate UUID in asset source inventory");sources.push_back({mapping.at("$source"),relative});
    }
    require(sources.size()<=4096,"Asset inventory limit");exec(p.db,"BEGIN IMMEDIATE");try{exec(p.db,"DELETE FROM assets");for(const auto& [id,path]:sources){Statement insert(p.db,"INSERT INTO assets(id,path) VALUES(?1,?2)");insert.id(1,id);insert.text(2,path);insert.row();}exec(p.db,"COMMIT");}catch(...){exec(p.db,"ROLLBACK");throw;}
}
std::vector<AssetInfo> AssetService::assets() const{auto& p=*impl_;p.thread();Statement query(p.db,"SELECT a.id,a.path,COALESCE(g.revision,0) FROM assets a LEFT JOIN generations g ON a.id=g.root ORDER BY a.id");std::vector<AssetInfo> result;while(query.row()){auto revision=sqlite3_column_int64(query.p,2);require(revision>=0,"Invalid catalog revision");result.push_back({query.id(0),query.text(1),static_cast<std::uint64_t>(revision)});}return result;}
CookResult AssetService::cook(std::string_view relative){auto& p=*impl_;p.thread();scan();auto source=p.source(relative);auto metadata=read(metadata_path(source),1024*1024);auto sidecar=source_metadata(source);auto ids=metadata_ids(sidecar);auto root=ids.at("$source");
    auto inspected=import_source(p.sources,source,ids,true);Json recipe={{"schema",1},{"importer",sidecar.at("importer")},{"cgltf","1.15"},{"meshoptimizer","1.2"},{"DirectXTex","2026-05-07"},{"profile","Windows-x64-RGBA8-sRGB-CPU-mips"},{"build",DAE_COOKER_BUILD_HASH},{"inputs",inspected.inputs},{"mapping",sidecar.at("subassets")},{"id",root.text()}};
    auto fingerprint=sha256(recipe.dump());std::uint64_t revision{};std::string previous;
    {Statement query(p.db,"SELECT revision,recipe FROM generations WHERE root=?1");query.id(1,root);if(query.row()){revision=static_cast<std::uint64_t>(sqlite3_column_int64(query.p,0));previous=query.text(1);}}
    require(revision<0x7fffffffffffffffULL,"Asset generation limit");
    for(const auto& [path,hash]:inspected.inputs)require(file_sha256(within(p.sources,path))==hash,"Input changed during inspection; result superseded");require(read(metadata_path(source),1024*1024)==metadata,"Import settings changed during inspection");
    if(previous==fingerprint){bool valid=true;Statement products(p.db,"SELECT hash,extension FROM products WHERE root=?1");products.id(1,root);std::size_t count{};while(products.row()){++count;auto hash=products.text(0),extension=products.text(1);digest_check(hash);require(extension=="mesh" || extension=="dds" || extension=="model.json" || extension=="material.json" || extension=="collision.json" || extension=="skeleton.json" || extension=="ozz" || extension=="skin.json" || extension=="human.json","Invalid cached extension");auto path=p.cas/(hash+"."+extension);if(!std::filesystem::exists(path) || file_sha256(path)!=hash)valid=false;}if(valid && count==(inspected.product_count?inspected.product_count:inspected.keys.size()+1))return {root,revision,false};}
    ++p.conversions;auto imported=import_source(p.sources,source,ids,false);require(imported.inputs==inspected.inputs,"Input closure changed during conversion; result superseded");
    for(const auto& [path,hash]:imported.inputs)require(file_sha256(within(p.sources,path))==hash,"Input changed during conversion; result superseded");require(read(metadata_path(source),1024*1024)==metadata,"Import settings changed during conversion");
    std::vector<std::string> hashes;for(const auto& product:imported.products){require(product.bytes.size()<=64*1024*1024,"Cooked product limit");auto hash=sha256(product.bytes);hashes.push_back(hash);auto path=p.cas/(hash+"."+product.extension);if(!std::filesystem::exists(path) || file_sha256(path)!=hash)atomic_write(path,product.bytes);require(file_sha256(path)==hash,"CAS publication digest mismatch");}
    // Files are immutable before the catalog head advances. Failures leave orphan cache blobs, never a partial head.
    exec(p.db,"BEGIN IMMEDIATE");try{
        {Statement remove(p.db,"DELETE FROM products WHERE root=?1");remove.id(1,root);remove.row();}{Statement remove(p.db,"DELETE FROM dependencies WHERE root=?1");remove.id(1,root);remove.row();}
        for(std::size_t i=0;i<imported.products.size();++i){const auto& product=imported.products[i];Statement insert(p.db,"INSERT INTO products(root,id,kind,hash,extension) VALUES(?1,?2,?3,?4,?5)");insert.id(1,root);insert.id(2,product.id);insert.text(3,product.kind);insert.text(4,hashes[i]);insert.text(5,product.extension);insert.row();for(auto dependency:product.required){Statement edge(p.db,"INSERT INTO dependencies(root,owner,target,kind) VALUES(?1,?2,?3,1)");edge.id(1,root);edge.id(2,product.id);edge.id(3,dependency);edge.row();}}
        {Statement head(p.db,"INSERT INTO generations(root,revision,recipe) VALUES(?1,?2,?3) ON CONFLICT(root) DO UPDATE SET revision=excluded.revision,recipe=excluded.recipe");head.id(1,root);sqlite3_bind_int64(head.p,2,static_cast<sqlite3_int64>(revision+1));head.text(3,fingerprint);head.row();}
        exec(p.db,"COMMIT");
    }catch(...){exec(p.db,"ROLLBACK");throw;}
    return {root,revision+1,true};
}
void AssetService::package(AssetId root,const std::filesystem::path& output) const{auto& p=*impl_;p.thread();Statement query(p.db,"SELECT id,kind,hash,extension FROM products WHERE root=?1 ORDER BY id");query.id(1,root);std::map<AssetId,Json> products;
    while(query.row()){auto hash=query.text(2),extension=query.text(3);digest_check(hash);require(file_sha256(p.cas/(hash+"."+extension))==hash,"Missing/corrupt required cooked artifact");require(products.emplace(query.id(0),Json{{"id",query.id(0).text()},{"kind",query.text(1)},{"sha256",hash},{"extension",extension}}).second,"Duplicate cooked product UUID");}
    std::map<AssetId,std::vector<AssetId>> edges;Statement deps(p.db,"SELECT owner,target FROM dependencies WHERE root=?1");deps.id(1,root);while(deps.row())edges[deps.id(0)].push_back(deps.id(1));std::set<AssetId> visited;
    std::function<void(AssetId)> visit=[&](AssetId id){if(visited.contains(id))return;require(products.contains(id),"Missing required runtime dependency");visited.insert(id);for(auto dep:edges[id])visit(dep);};visit(root);Json manifest={{"schema",1},{"root",root.text()},{"assets",Json::array()}};
    for(auto id:visited)manifest["assets"].push_back(products.at(id));atomic_write(output,manifest.dump(2)+"\n");
}
std::filesystem::path AssetService::cas_path() const{impl_->thread();return impl_->cas;}
std::uint64_t AssetService::conversion_count() const{impl_->thread();return impl_->conversions;}
std::string load_cooked_collision(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("root")==id.text()&&manifest.at("assets").size()==1,"Collision registry mismatch");auto record=manifest.at("assets").at(0);require(record.at("id")==id.text()&&record.at("kind")=="collision"&&record.at("extension")=="collision.json","Collision product type mismatch");auto digest=record.at("sha256").get<std::string>();digest_check(digest);auto bytes=read(cas/(digest+".collision.json"),1024*1024);require(sha256(bytes)==digest,"Collision product hash mismatch");return bytes;}
RuntimeModel load_cooked_model(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId root){auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1 && manifest.at("root")==root.text() && manifest.at("assets").size()<=512,"Runtime registry mismatch/limit");std::map<AssetId,Json> records;
    for(const auto& record:manifest.at("assets")){auto id=AssetId::parse(record.at("id").get<std::string>());require(records.emplace(id,record).second,"Duplicate cooked registry UUID");}
    auto load=[&](AssetId id,const char* kind){require(records.contains(id),"Missing required cooked AssetID");const auto& record=records.at(id);require(record.at("kind")==kind,"Cooked AssetRef type mismatch");auto hash=record.at("sha256").get<std::string>();digest_check(hash);auto ext=record.at("extension").get<std::string>();require(ext=="model.json" || ext=="material.json" || ext=="mesh" || ext=="dds","Invalid cooked extension");auto bytes=read(cas/(hash+"."+ext));require(sha256(bytes)==hash,"Cooked artifact digest mismatch");return bytes;};
    auto model=json(load(root,"model"));require(model.at("schema")==1 && model.at("id")==root.text() && model.at("axes")=="right-handed-y-up" && model.at("units")=="metres","Cooked model contract mismatch");RuntimeModel result;result.id=root;std::set<AssetId> materials,textures;
    for(const auto& mesh:model.at("meshes")){auto decoded=decode_mesh(load(AssetId::parse(mesh.get<std::string>()),"mesh"));for(const auto& part:decoded.parts)materials.insert(part.material);result.meshes.push_back(std::move(decoded));}
    for(auto id:materials){auto fields=json(load(id,"material"));require(fields.at("schema")==1 && fields.at("id")==id.text(),"Cooked material version/ID mismatch");CookedMaterial material;material.id=id;material.color=fields.at("color").get<std::array<float,4>>();material.roughness=fields.at("roughness").get<float>();material.metallic=fields.at("metallic").get<float>();material.double_sided=fields.at("double_sided").get<bool>();if(!fields.at("texture").is_null()){material.texture=AssetId::parse(fields.at("texture").get<std::string>());material.has_texture=true;textures.insert(material.texture);}result.materials.push_back(material);}
    for(auto id:textures)result.textures.emplace_back(id,decode_texture(load(id,"texture")));return result;
}
ModelGeneration ModelStore::prepare(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id) const{require(retired_.size()<16,"Model retirement queue full; release pins before preparing");return {current_.number+1,std::make_shared<const RuntimeModel>(load_cooked_model(registry,cas,id))};}
void ModelStore::publish(ModelGeneration candidate){require(candidate.model && candidate.number==current_.number+1,"Stale/invalid resource generation");if(current_.model)retired_.push_back(current_.model);current_=std::move(candidate);}
std::size_t ModelStore::collect(){auto before=retired_.size();std::erase_if(retired_,[](const auto& model){return model.use_count()==1;});return before-retired_.size();}
}
