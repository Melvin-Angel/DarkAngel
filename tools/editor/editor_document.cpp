#include <darkangel/editor_document.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/animation.hpp>
#include <darkangel/collision_asset.hpp>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
Json parse(std::string_view text){require(text.size()<=1024*1024,"Document byte limit");std::vector<std::set<std::string>> keys;std::size_t count{};return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32 && ++count<=65536,"Document JSON work limit");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate document key");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary|std::ios::ate);require(in.good() && in.tellg()>=0 && in.tellg()<=1024*1024,"Document missing/oversized");std::string text(static_cast<std::size_t>(in.tellg()),'\0');in.seekg(0);in.read(text.data(),text.size());require(in.good(),"Document read failed");return text;}
void validate_native_sources(const AssemblySources& sources){for(const auto& [id,text]:sources){auto source=parse(text);if(source.value("kind","")=="skeleton"){auto rig=decode_rig_source(text);require(rig.id==id,"Owned skeleton UUID mismatch");}else if(source.value("kind","")=="collision"){require(decode_collision_source(text).id==id,"Owned collision UUID mismatch");}}}

}
struct EditorDocument::Impl {
    AssetId root;StableId placement;AssemblySources sources;SpawnPlan plan;std::unique_ptr<World> world;
    std::size_t limit;std::uint64_t revision{},next{};std::string saved,baseline;
    struct Candidate {AssemblySources sources;SpawnPlan plan;std::uint64_t revision;std::string summary;bool no_op;};
    struct History {AssemblySources sources;DocumentHistoryHead head;};DocumentHistoryHead head;std::map<std::uint64_t,Candidate> prepared;std::vector<History> undo,redo;
    Impl(AssetId id,StableId p,AssemblySources s,std::size_t l):root(id),placement(p),sources(std::move(s)),limit(l){require(l>0 && l<=1024,"Document history limit");materialize();saved=fingerprint();}
    std::string fingerprint() const{Json records=Json::object();for(const auto& [id,text]:sources)records[id.text()]=parse(text);return sha256(records.dump());}
    void materialize(){validate_native_sources(sources);auto candidate=resolve_assembly(root,placement,sources);auto next_world=std::make_unique<World>(WorldDomain::Authoring);next_world->load(candidate.scene_json);plan=std::move(candidate);world=std::move(next_world);baseline=world->serialize();}
    void check(std::uint64_t expected) const{require(expected==revision,"Stale document revision");require(world->serialize()==baseline,"Document view changed outside its service");}
    PreparedDocumentEdit stage(AssemblySources candidate,std::string summary){require(prepared.size()<limit,"Prepared document queue full");Json records=Json::object();for(const auto& [id,text]:candidate)records[id.text()]=parse(text);require(records.dump().size()<=1024*1024,"Document closure byte limit");bool no_op=sha256(records.dump())==fingerprint();validate_native_sources(candidate);auto resolved=resolve_assembly(root,placement,candidate);auto token=++next;prepared.emplace(token,Candidate{std::move(candidate),std::move(resolved),revision,summary,no_op});return {token,revision,std::move(summary)};}
};
EditorDocument::EditorDocument(AssetId root,StableId placement,AssemblySources sources,std::size_t limit):impl_(std::make_unique<Impl>(root,placement,std::move(sources),limit)){}
EditorDocument::~EditorDocument()=default;
World& EditorDocument::world(){impl_->world->domain();return *impl_->world;}
const SpawnPlan& EditorDocument::plan() const{impl_->world->domain();return impl_->plan;}
std::uint64_t EditorDocument::revision() const{impl_->world->domain();return impl_->revision;}
bool EditorDocument::dirty() const{impl_->world->domain();return impl_->saved!=impl_->fingerprint();}
std::string EditorDocument::source(AssetId id) const{impl_->world->domain();return impl_->sources.at(id);}
std::map<StableId,ScriptDefinition> EditorDocument::scripts() const{impl_->world->domain();return cook_script_definitions(impl_->sources);}
PreparedDocumentEdit EditorDocument::prepare(std::uint64_t revision,std::span<const PropertyChange> changes,EditScope scope){auto& p=*impl_;p.check(revision);require(!changes.empty() && changes.size()<=1024,"Document edit limit");auto sources=p.sources;
    for(const auto& change:changes){const ObjectOrigin* origin{};for(const auto& o:p.plan.origins)if(o.object==change.object)origin=&o;require(origin!=nullptr,"Removed document target");
        auto path=origin->local_path.substr(p.placement.text().size()+1);auto slash=path.find('/');
        auto asset=scope==EditScope::Definition?origin->definition:p.root;auto graph=parse(sources.at(asset));
        if(scope==EditScope::Definition || slash==path.npos){auto local=path.substr(path.rfind('/')==path.npos?0:path.rfind('/')+1);auto& record=graph.at("entities").at(local);record["types"][std::to_string(change.type)]["fields"][std::to_string(change.property)]=change.value;}
        else{require(path.find('/',slash+1)==path.npos,"Nested placement edit requires explicit scoped mount document");auto mount=path.substr(0,slash),local=path.substr(slash+1);auto& patches=graph.at("mounts").at(mount)["patches"];if(patches.is_null())patches=Json::array();
            bool replaced=false;for(auto& patch:patches)if(patch.value("op","")=="SetProperty" && patch.at("target")==local && patch.at("type")==change.type && patch.at("property")==change.property){patch["value"]=change.value;replaced=true;}
            if(!replaced)patches.push_back({{"op","SetProperty"},{"target",local},{"type",change.type},{"property",change.property},{"value",change.value}});
        }sources[asset]=graph.dump();
    }return p.stage(std::move(sources),scope==EditScope::Definition?"Edit assembly definition; explicit promotion affects linked placements":"Edit scene placement; stable property override");
}
PreparedDocumentEdit EditorDocument::prepare_source(std::uint64_t revision,AssetId asset,std::string_view source){auto& p=*impl_;p.check(revision);require(p.sources.contains(asset),"Source asset not owned by document");auto sources=p.sources;sources[asset]=parse(source).dump();return p.stage(std::move(sources),"Structural/source transaction; preview restart required");}
DocumentHistoryHead EditorDocument::history_head() const{impl_->world->domain();return impl_->head;}
void EditorDocument::commit(PreparedDocumentEdit edit,std::string_view origin){require(!origin.empty() && origin.size()<=128,"History origin size limit");auto& p=*impl_;p.check(edit.revision);auto it=p.prepared.find(edit.token);require(it!=p.prepared.end() && it->second.revision==edit.revision,"Unknown/stale document token");if(it->second.no_op){p.prepared.erase(it);return;}auto next=std::make_unique<World>(WorldDomain::Authoring);next->load(it->second.plan.scene_json);DocumentHistoryHead head{edit.token,it->second.summary,std::string(origin)};p.undo.push_back({p.sources,p.head});p.head=std::move(head);if(p.undo.size()>p.limit)p.undo.erase(p.undo.begin());p.sources=std::move(it->second.sources);p.plan=std::move(it->second.plan);p.world=std::move(next);p.baseline=p.world->serialize();p.redo.clear();p.prepared.clear();++p.revision;}
void EditorDocument::discard(PreparedDocumentEdit edit){impl_->check(edit.revision);require(impl_->prepared.erase(edit.token)==1,"Unknown document token");}
void EditorDocument::undo(std::uint64_t revision){auto& p=*impl_;p.check(revision);require(!p.undo.empty(),"Nothing to undo");auto history=p.undo.back();auto sources=history.sources;auto plan=resolve_assembly(p.root,p.placement,sources);auto world=std::make_unique<World>(WorldDomain::Authoring);world->load(plan.scene_json);p.redo.push_back({p.sources,p.head});p.head=std::move(history.head);p.sources=std::move(sources);p.plan=std::move(plan);p.world=std::move(world);p.undo.pop_back();p.baseline=p.world->serialize();p.prepared.clear();++p.revision;}
void EditorDocument::redo(std::uint64_t revision){auto& p=*impl_;p.check(revision);require(!p.redo.empty(),"Nothing to redo");auto history=p.redo.back();auto sources=history.sources;auto plan=resolve_assembly(p.root,p.placement,sources);auto world=std::make_unique<World>(WorldDomain::Authoring);world->load(plan.scene_json);p.undo.push_back({p.sources,p.head});p.head=std::move(history.head);p.sources=std::move(sources);p.plan=std::move(plan);p.world=std::move(world);p.redo.pop_back();p.baseline=p.world->serialize();p.prepared.clear();++p.revision;}
void EditorDocument::save(const std::filesystem::path& path){auto& p=*impl_;p.check(p.revision);auto directory=path.parent_path();if(directory.empty())directory=".";std::filesystem::create_directories(directory);auto records_name=path.filename().string()+".records";auto records=directory/records_name;std::filesystem::create_directories(records);
    auto store=[&](const Json& value){auto bytes=value.dump();auto hash=sha256(bytes);auto destination=records/(hash+".json");if(std::filesystem::exists(destination))require(read(destination)==bytes,"Corrupt document record");else{auto pending=destination;pending+=".pending";std::ofstream out(pending,std::ios::binary);out<<bytes;out.close();require(!out.fail(),"Document record staging failed");require(MoveFileExW(pending.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Document record publication failed");}return hash;};
    Json bundle={{"schema",2},{"root",p.root.text()},{"placement",p.placement.text()},{"records",records_name},{"assets",Json::object()}};
    for(const auto& [id,text]:p.sources){auto graph=parse(text);if(id==p.root){Json record={{"header",graph},{"entities",Json::object()},{"mounts",Json::object()}};record["header"].erase("entities");record["header"].erase("mounts");for(const auto& [local,entity]:graph.at("entities").items())record["entities"][local]=store(entity);auto mounts=graph.value("mounts",Json::object());for(const auto& [local,mount]:mounts.items())record["mounts"][local]=store(mount);bundle["assets"][id.text()]=record;}else bundle["assets"][id.text()]={{graph.value("kind","")=="skeleton"?"source":"assembly",store(graph)}};}
    auto text=bundle.dump(2)+"\n";require(text.size()<=1024*1024,"Document manifest limit");auto pending=path;pending+=".pending";{std::ofstream out(pending,std::ios::binary);out<<text;out.close();require(!out.fail(),"Document journal write failed");}require(MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Document head publication failed; pending manifest retained");p.saved=p.fingerprint();
}
std::unique_ptr<EditorDocument> EditorDocument::open(const std::filesystem::path& path){auto root=parse(read(path));AssemblySources sources;
    if(root.at("schema")==1){for(const auto& [id,value]:root.at("sources").items())sources.emplace(AssetId::parse(id),value.dump());}
    else{require(root.at("schema")==2,"Document manifest schema mismatch");auto folder=root.at("records").get<std::string>();require(!folder.empty() && folder.find_first_of("/\\:")==folder.npos && folder!=".." && folder.ends_with(".records"),"Unsafe document records directory");
        auto load=[&](const Json& digest){auto hash=digest.get<std::string>();require(hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==hash.npos,"Invalid document record hash");auto text=read(path.parent_path()/folder/(hash+".json"));require(sha256(text)==hash,"Partial/corrupt document submission");return parse(text);};
        for(const auto& [id,record]:root.at("assets").items()){Json graph;if(record.contains("source"))graph=load(record.at("source"));else if(record.contains("assembly"))graph=load(record.at("assembly"));else{graph=record.at("header");graph["entities"]=Json::object();graph["mounts"]=Json::object();for(const auto& [local,digest]:record.at("entities").items())graph["entities"][local]=load(digest);for(const auto& [local,digest]:record.at("mounts").items())graph["mounts"][local]=load(digest);}sources.emplace(AssetId::parse(id),graph.dump());}
    }return std::make_unique<EditorDocument>(AssetId::parse(root.at("root").get<std::string>()),StableId::parse(root.at("placement").get<std::string>()),std::move(sources));
}
}
