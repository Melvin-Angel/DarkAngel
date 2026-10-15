#include <darkangel/editor_document.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/animation.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/action.hpp>
#include <darkangel/input.hpp>
#include <darkangel/joint_mask.hpp>
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
Json parse(std::string_view text){require(text.size()<=scene_document_byte_limit,"Document byte limit");std::vector<std::set<std::string>> keys;std::size_t count{};return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32 && ++count<=scene_document_byte_limit/4,"Document JSON work limit");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate document key");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary|std::ios::ate);require(in.good() && in.tellg()>=0 && in.tellg()<=std::streamoff(scene_document_byte_limit),"Document missing/oversized");std::string text(static_cast<std::size_t>(in.tellg()),'\0');in.seekg(0);in.read(text.data(),text.size());require(in.good(),"Document read failed");return text;}
void validate_native_sources(const AssemblySources& sources){for(const auto& [id,text]:sources){auto source=parse(text);if(source.value("kind","")=="skeleton"){auto rig=decode_rig_source(text);require(rig.id==id,"Owned skeleton UUID mismatch");}else if(source.value("kind","")=="joint_mask"){auto mask=decode_joint_mask_source(text);require(mask.id==id&&sources.contains(mask.skeleton),"Owned mask UUID/skeleton missing");compile_joint_mask(mask,decode_rig_source(sources.at(mask.skeleton)));}else if(source.value("kind","")=="input"){require(decode_input_source(text).id==id,"Owned input UUID mismatch");}else if(source.value("kind","")=="action"){require(decode_action_source(text).id==id,"Owned action UUID mismatch");}else if(source.value("kind","")=="collision"){require(decode_collision_source(text).id==id,"Owned collision UUID mismatch");}}}

}
// The document keeps its scene's own source (the root) as a header and one record per entity, so an edit of one entity is a
// record edit: SceneAssembly resolves that entity alone and the authoring World updates that object in place. Edits of any other
// source, of mounts, or of a variant root resolve the whole scene again (SCENE_SCALE_PLAN.md phase 8).
struct EditorDocument::Impl {
    using Records=SceneAssembly::Records;
    AssetId root;StableId placement;std::size_t limit;
    // The root source: every field but its entities, the canonical text around its entities, and each entity's canonical JSON.
    Json header;std::string prefix,suffix;std::map<std::string,std::string> entities;std::size_t entity_bytes{};
    // Every other source as given, and its canonical digest and size.
    AssemblySources others;struct Canonical {std::string digest;std::size_t bytes;};std::map<AssetId,Canonical> canonical;
    std::unique_ptr<SceneAssembly> assembly;std::unique_ptr<World> world;std::uint64_t viewed{}; // the world generation the document wrote last
    std::uint64_t revision{},next{};std::string saved;
    struct Candidate {Records records;std::shared_ptr<const SceneAssembly::Staged> staged;std::map<AssetId,std::string> sources;std::unique_ptr<SceneAssembly> assembly;std::uint64_t revision;std::string summary;bool no_op;};
    // One history step: the entity records it changed, or the sources it replaced, before and after. head is the history head
    // to restore when the step is applied (undone or redone).
    struct History {Records before,after;std::map<AssetId,std::string> sources_before,sources_after;DocumentHistoryHead head;};
    DocumentHistoryHead head;std::map<std::uint64_t,Candidate> prepared;std::vector<History> undo,redo;
    Impl(AssetId id,StableId p,AssemblySources s,std::size_t l):root(id),placement(p),limit(l){require(l>0 && l<=1024,"Document history limit");
        validate_native_sources(s);assembly=std::make_unique<SceneAssembly>(root,placement,s);set_root(s.at(root));s.erase(root);for(const auto& [asset,text]:s)set_other(asset,text);others=std::move(s);rebuild_world();saved=fingerprint();}
    static std::string key(const std::string& local){return Json(local).dump();}
    void set_root(std::string_view text){auto graph=parse(text);entities.clear();entity_bytes=0;for(const auto& [local,entity]:graph.at("entities").items()){auto record=entity.dump();entity_bytes+=key(local).size()+1+record.size();entities.emplace(local,std::move(record));}
        graph.erase("entities");header=std::move(graph);Json before=Json::object(),after=Json::object();for(auto it=header.begin();it!=header.end();++it)(it.key()<"entities"?before:after)[it.key()]=it.value();
        prefix=before.empty()?"{":before.dump();if(!before.empty()){prefix.back()=',';}prefix+="\"entities\":{";suffix=after.empty()?"}}":"},"+after.dump().substr(1);}
    void set_entity(const std::string& local,const std::optional<std::string>& record){
        if(auto it=entities.find(local);it!=entities.end()){entity_bytes-=key(local).size()+1+it->second.size();entities.erase(it);}
        if(record){entity_bytes+=key(local).size()+1+record->size();entities.emplace(local,*record);}}
    void set_other(AssetId asset,const std::string& text){auto canonical_text=parse(text).dump();canonical[asset]={sha256(canonical_text),canonical_text.size()};}
    std::size_t root_bytes(std::size_t entity_bytes,std::size_t count) const{return prefix.size()+entity_bytes+(count?count-1:0)+suffix.size();}
    // The root source as canonical JSON text, the same as parsing it and dumping it again. Assembled once per revision.
    mutable std::uint64_t assembled=UINT64_MAX;mutable std::string root_cache;
    const std::string& root_text() const{if(assembled!=revision){std::string text;text.reserve(root_bytes(entity_bytes,entities.size()));text+=prefix;bool first=true;for(const auto& [local,record]:entities){if(!first)text+=",";first=false;text+=key(local);text+=":";text+=record;}text+=suffix;root_cache=std::move(text);assembled=revision;}return root_cache;}
    AssemblySources sources() const{auto all=others;all[root]=root_text();return all;}
    // The bytes of the whole closure as one JSON record of canonical sources, the document's byte bound, with some sources resized.
    std::size_t closure_bytes(std::size_t root_size,const std::map<AssetId,std::size_t>& resized={}) const{std::size_t bytes=2+root.text().size()+3+root_size;
        for(const auto& [asset,known]:canonical){auto size=resized.contains(asset)?resized.at(asset):known.bytes;bytes+=1+asset.text().size()+3+size;}return bytes;}
    // The document's content digest: the canonical digest of each source. Sources change only with the revision (commit, undo,
    // redo), so the digest is cached per revision: dirty() runs every editor frame.
    mutable std::uint64_t fingerprinted=UINT64_MAX;mutable std::string digest;
    std::string fingerprint() const{if(fingerprinted!=revision){std::string all=root.text()+":"+sha256(root_text())+"\n";for(const auto& [asset,known]:canonical)all+=asset.text()+":"+known.digest+"\n";digest=sha256(all);fingerprinted=revision;}return digest;}
    void rebuild_world(){auto next_world=std::make_unique<World>(WorldDomain::Authoring);next_world->load(assembly->plan().scene_json);world=std::move(next_world);viewed=world->generation();}
    void check(std::uint64_t expected) const{require(expected==revision,"Stale document revision");require(world->generation()==viewed,"Document view changed outside its service");}
    PreparedDocumentEdit add(Candidate candidate){auto token=++next;PreparedDocumentEdit edit{token,revision,candidate.summary};prepared.emplace(token,std::move(candidate));return edit;}
    // An edit of root entity records: canonical records, a byte check, and the staged resolve of the edited entities alone.
    PreparedDocumentEdit stage_records(const Records& edits,std::string summary){require(prepared.size()<limit,"Prepared document queue full");
        if(!assembly->editable()){auto graph=parse(root_text());for(const auto& [local,record]:edits){if(record)graph.at("entities")[local]=parse(*record);else require(graph.at("entities").erase(local)==1,"Removed document target");}return stage_sources({{root,graph.dump()}},std::move(summary));}
        Records records;bool no_op=true;auto bytes=entity_bytes;auto count=entities.size();
        for(const auto& [local,record]:edits){auto old=entities.find(local);if(old!=entities.end()){bytes-=key(local).size()+1+old->second.size();--count;}
            if(record){auto text=parse(*record).dump();if(old==entities.end()||old->second!=text)no_op=false;bytes+=key(local).size()+1+text.size();++count;records[local]=std::move(text);}
            else{require(old!=entities.end(),"Removed document target");no_op=false;records[local]=std::nullopt;}}
        require(closure_bytes(root_bytes(bytes,count))<=scene_document_byte_limit,"Document closure byte limit");
        auto staged=no_op?nullptr:assembly->stage(records);return add({std::move(records),std::move(staged),{},nullptr,revision,std::move(summary),no_op});}
    // An edit of whole sources (canonical text): every source is validated and the whole scene resolved again.
    PreparedDocumentEdit stage_sources(std::map<AssetId,std::string> changed,std::string summary){require(prepared.size()<limit,"Prepared document queue full");
        auto candidate=sources();std::map<AssetId,std::size_t> resized;auto root_size=root_text().size();bool no_op=true;
        for(const auto& [asset,text]:changed){if(asset==root){root_size=text.size();no_op=no_op&&text==root_text();}else{resized[asset]=text.size();no_op=no_op&&sha256(text)==canonical.at(asset).digest;}candidate[asset]=text;}
        require(closure_bytes(root_size,resized)<=scene_document_byte_limit,"Document closure byte limit");validate_native_sources(candidate);
        auto next_assembly=std::make_unique<SceneAssembly>(root,placement,candidate);return add({{},nullptr,std::move(changed),std::move(next_assembly),revision,std::move(summary),no_op});}
    // Applies root entity records staged against the scene as it is: the assembly and the authoring World change those objects only.
    void apply_records(const Records& records,const SceneAssembly::Staged& staged){
        for(const auto& [object,data]:assembly->apply(staged)){auto handle=world->find(object);if(!data)world->destroy(handle,Authority::Authoring);else if(world->valid(handle))world->replace(handle,*data,Authority::Authoring);else world->create(*data);}
        for(const auto& [local,record]:records)set_entity(local,record);viewed=world->generation();}
    void apply_sources(const std::map<AssetId,std::string>& changed,std::unique_ptr<SceneAssembly> next_assembly){
        for(const auto& [asset,text]:changed){if(asset==root)set_root(text);else{set_other(asset,text);others[asset]=text;}}assembly=std::move(next_assembly);rebuild_world();}
    // Applies one history step in either direction; the step's own records or sources say what changes.
    void restore(const Records& records,const std::map<AssetId,std::string>& changed){
        if(!records.empty()){auto staged=assembly->stage(records);apply_records(records,*staged);}
        else{auto candidate=sources();for(const auto& [asset,text]:changed)candidate[asset]=text;apply_sources(changed,std::make_unique<SceneAssembly>(root,placement,candidate));}}
    History step(const Records& after,const std::map<AssetId,std::string>& sources_after){History h{{},after,{},sources_after,head};
        for(const auto& [local,record]:after){auto old=entities.find(local);h.before[local]=old==entities.end()?std::nullopt:std::optional<std::string>(old->second);}
        for(const auto& [asset,text]:sources_after)h.sources_before[asset]=asset==root?root_text():others.at(asset);return h;}
};
EditorDocument::EditorDocument(AssetId root,StableId placement,AssemblySources sources,std::size_t limit):impl_(std::make_unique<Impl>(root,placement,std::move(sources),limit)){}
EditorDocument::~EditorDocument()=default;
World& EditorDocument::world(){impl_->world->domain();return *impl_->world;}
const SpawnPlan& EditorDocument::plan() const{impl_->world->domain();return impl_->assembly->plan();}
AssetId EditorDocument::root() const{impl_->world->domain();return impl_->root;}
std::uint64_t EditorDocument::revision() const{impl_->world->domain();return impl_->revision;}
bool EditorDocument::dirty() const{impl_->world->domain();return impl_->saved!=impl_->fingerprint();}
std::string EditorDocument::source(AssetId id) const{impl_->world->domain();return id==impl_->root?impl_->root_text():impl_->others.at(id);}
std::optional<std::string> EditorDocument::entity(const std::string& local) const{impl_->world->domain();auto found=impl_->entities.find(local);if(found==impl_->entities.end())return std::nullopt;return found->second;}
std::map<StableId,ScriptDefinition> EditorDocument::scripts() const{impl_->world->domain();return cook_script_definitions(impl_->sources());}
PreparedDocumentEdit EditorDocument::prepare(std::uint64_t revision,std::span<const PropertyChange> changes,EditScope scope){auto& p=*impl_;p.check(revision);require(!changes.empty() && changes.size()<=1024,"Document edit limit");
    const std::string summary=scope==EditScope::Definition?"Edit assembly definition; explicit promotion affects linked placements":"Edit scene placement; stable property override";
    struct Target {const ObjectOrigin* origin;std::string path;std::size_t slash;AssetId asset;};std::vector<Target> targets;bool own=p.assembly->editable();
    for(const auto& change:changes){const ObjectOrigin* origin{};for(const auto& o:p.assembly->plan().origins)if(o.object==change.object)origin=&o;require(origin!=nullptr,"Removed document target");
        auto path=origin->local_path.substr(p.placement.text().size()+1);auto slash=path.find('/');auto asset=scope==EditScope::Definition?origin->definition:p.root;
        own=own&&asset==p.root&&slash==path.npos;targets.push_back({origin,std::move(path),slash,asset});}
    // Fields of the scene's own entities: one record each.
    if(own){std::map<std::string,Json> edited;for(std::size_t index=0;index<changes.size();++index){const auto& change=changes[index];const auto& local=targets[index].path;auto it=edited.find(local);if(it==edited.end())it=edited.emplace(local,parse(p.entities.at(local))).first;
            it->second["types"][std::to_string(change.type)]["fields"][std::to_string(change.property)]=change.value;}
        Impl::Records records;for(const auto& [local,record]:edited)records[local]=record.dump();return p.stage_records(records,summary);}
    std::map<AssetId,Json> graphs;
    for(std::size_t index=0;index<changes.size();++index){const auto& change=changes[index];const auto& [origin,path,slash,asset]=targets[index];auto found=graphs.find(asset);if(found==graphs.end())found=graphs.emplace(asset,parse(asset==p.root?p.root_text():p.others.at(asset))).first;auto& graph=found->second;
        if(scope==EditScope::Definition || slash==path.npos){auto local=path.substr(path.rfind('/')==path.npos?0:path.rfind('/')+1);auto& record=graph.at("entities").at(local);record["types"][std::to_string(change.type)]["fields"][std::to_string(change.property)]=change.value;}
        else{require(path.find('/',slash+1)==path.npos,"Nested placement edit requires explicit scoped mount document");auto mount=path.substr(0,slash),local=path.substr(slash+1);auto& patches=graph.at("mounts").at(mount)["patches"];if(patches.is_null())patches=Json::array();
            bool replaced=false;for(auto& patch:patches)if(patch.value("op","")=="SetProperty" && patch.at("target")==local && patch.at("type")==change.type && patch.at("property")==change.property){patch["value"]=change.value;replaced=true;}
            if(!replaced)patches.push_back({{"op","SetProperty"},{"target",local},{"type",change.type},{"property",change.property},{"value",change.value}});
        }
    }std::map<AssetId,std::string> changed;for(const auto& [asset,graph]:graphs)changed[asset]=graph.dump();return p.stage_sources(std::move(changed),summary);
}
PreparedDocumentEdit EditorDocument::prepare_source(std::uint64_t revision,AssetId asset,std::string_view source){auto& p=*impl_;p.check(revision);require(asset==p.root || p.others.contains(asset),"Source asset not owned by document");auto graph=parse(source);const std::string summary="Structural/source transaction; preview restart required";
    // A new root source that changes only entities is a record edit of those entities.
    if(asset==p.root && p.assembly->editable() && graph.is_object() && graph.contains("entities") && graph.at("entities").is_object()){auto rest=graph;rest.erase("entities");if(rest==p.header){Impl::Records records;const auto& next=graph.at("entities");
        for(const auto& [local,entity]:next.items()){auto text=entity.dump();auto old=p.entities.find(local);if(old==p.entities.end()||old->second!=text)records[local]=std::move(text);}
        for(const auto& [local,record]:p.entities)if(!next.contains(local))records[local]=std::nullopt;
        if(records.empty()){require(p.prepared.size()<p.limit,"Prepared document queue full");return p.add({{},nullptr,{},nullptr,p.revision,summary,true});}
        return p.stage_records(records,summary);}}
    return p.stage_sources({{asset,graph.dump()}},summary);}
PreparedDocumentEdit EditorDocument::prepare_entities(std::uint64_t revision,const std::map<std::string,std::optional<std::string>>& records){auto& p=*impl_;p.check(revision);require(!records.empty() && records.size()<=1024,"Document edit limit");return p.stage_records(records,"Structural/source transaction; preview restart required");}
DocumentHistoryHead EditorDocument::history_head() const{impl_->world->domain();return impl_->head;}
void EditorDocument::commit(PreparedDocumentEdit edit,std::string_view origin){require(!origin.empty() && origin.size()<=128,"History origin size limit");auto& p=*impl_;p.check(edit.revision);auto it=p.prepared.find(edit.token);require(it!=p.prepared.end() && it->second.revision==edit.revision,"Unknown/stale document token");if(it->second.no_op){p.prepared.erase(it);return;}
    auto& candidate=it->second;auto history=p.step(candidate.records,candidate.sources);
    if(candidate.staged)p.apply_records(candidate.records,*candidate.staged);else p.apply_sources(candidate.sources,std::move(candidate.assembly));
    p.undo.push_back(std::move(history));p.head={edit.token,candidate.summary,std::string(origin)};if(p.undo.size()>p.limit)p.undo.erase(p.undo.begin());p.redo.clear();p.prepared.clear();++p.revision;}
void EditorDocument::discard(PreparedDocumentEdit edit){impl_->check(edit.revision);require(impl_->prepared.erase(edit.token)==1,"Unknown document token");}
void EditorDocument::undo(std::uint64_t revision){auto& p=*impl_;p.check(revision);require(!p.undo.empty(),"Nothing to undo");auto history=p.undo.back();p.restore(history.before,history.sources_before);
    std::swap(history.head,p.head);p.redo.push_back(std::move(history));p.undo.pop_back();p.prepared.clear();++p.revision;}
void EditorDocument::redo(std::uint64_t revision){auto& p=*impl_;p.check(revision);require(!p.redo.empty(),"Nothing to redo");auto history=p.redo.back();p.restore(history.after,history.sources_after);
    std::swap(history.head,p.head);p.undo.push_back(std::move(history));p.redo.pop_back();p.prepared.clear();++p.revision;}
void EditorDocument::save(const std::filesystem::path& path){auto& p=*impl_;p.check(p.revision);auto directory=path.parent_path();if(directory.empty())directory=".";std::filesystem::create_directories(directory);auto records_name=path.filename().string()+".records";auto records=directory/records_name;std::filesystem::create_directories(records);
    auto store=[&](const std::string& bytes){auto hash=sha256(bytes);auto destination=records/(hash+".json");if(std::filesystem::exists(destination))require(read(destination)==bytes,"Corrupt document record");else{auto pending=destination;pending+=".pending";std::ofstream out(pending,std::ios::binary);out<<bytes;out.close();require(!out.fail(),"Document record staging failed");require(MoveFileExW(pending.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Document record publication failed");}return hash;};
    Json bundle={{"schema",2},{"root",p.root.text()},{"placement",p.placement.text()},{"records",records_name},{"assets",Json::object()}};
    Json record={{"header",p.header},{"entities",Json::object()},{"mounts",Json::object()}};record["header"].erase("mounts");for(const auto& [local,entity]:p.entities)record["entities"][local]=store(entity);
    auto mounts=p.header.value("mounts",Json::object());for(const auto& [local,mount]:mounts.items())record["mounts"][local]=store(mount.dump());bundle["assets"][p.root.text()]=record;
    for(const auto& [id,text]:p.others){auto graph=parse(text);bundle["assets"][id.text()]={{graph.value("kind","")=="skeleton"?"source":"assembly",store(graph.dump())}};}
    auto text=bundle.dump(2)+"\n";require(text.size()<=scene_document_byte_limit,"Document manifest limit");auto pending=path;pending+=".pending";{std::ofstream out(pending,std::ios::binary);out<<text;out.close();require(!out.fail(),"Document journal write failed");}require(MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Document head publication failed; pending manifest retained");p.saved=p.fingerprint();
}
std::unique_ptr<EditorDocument> EditorDocument::open(const std::filesystem::path& path){auto root=parse(read(path));AssemblySources sources;
    if(root.at("schema")==1){for(const auto& [id,value]:root.at("sources").items())sources.emplace(AssetId::parse(id),value.dump());}
    else{require(root.at("schema")==2,"Document manifest schema mismatch");auto folder=root.at("records").get<std::string>();require(!folder.empty() && folder.find_first_of("/\\:")==folder.npos && folder!=".." && folder.ends_with(".records"),"Unsafe document records directory");
        auto load=[&](const Json& digest){auto hash=digest.get<std::string>();require(hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==hash.npos,"Invalid document record hash");auto text=read(path.parent_path()/folder/(hash+".json"));require(sha256(text)==hash,"Partial/corrupt document submission");return parse(text);};
        for(const auto& [id,record]:root.at("assets").items()){Json graph;if(record.contains("source"))graph=load(record.at("source"));else if(record.contains("assembly"))graph=load(record.at("assembly"));else{graph=record.at("header");graph["entities"]=Json::object();graph["mounts"]=Json::object();for(const auto& [local,digest]:record.at("entities").items())graph["entities"][local]=load(digest);for(const auto& [local,digest]:record.at("mounts").items())graph["mounts"][local]=load(digest);}sources.emplace(AssetId::parse(id),graph.dump());}
    }return std::make_unique<EditorDocument>(AssetId::parse(root.at("root").get<std::string>()),StableId::parse(root.at("placement").get<std::string>()),std::move(sources));
}
}
