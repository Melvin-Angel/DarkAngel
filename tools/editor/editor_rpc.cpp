#include <darkangel/editor_rpc.hpp>
#include <nlohmann/json.hpp>
#include <darkangel/hash.hpp>
#include <charconv>
#include <chrono>
#include <set>
#include <map>
#include <stdexcept>
namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void keys(const Json& j,std::initializer_list<std::string_view> names){require(j.is_object() && j.size()==names.size(),"Unknown/missing fields");for(auto n:names)require(j.contains(std::string(n)),"Missing field");}
std::uint64_t exact(const Json& j){require(j.is_string(),"Revision/ID must be a decimal string");auto text=j.get<std::string>();std::uint64_t value{};auto result=std::from_chars(text.data(),text.data()+text.size(),value);require(!text.empty() && text.size()<=20 && result.ec==std::errc{} && result.ptr==text.data()+text.size() && std::to_string(value)==text,"Invalid exact unsigned value");return value;}
Json number_schema(){return Json{{"type","string"},{"pattern","^(0|[1-9][0-9]{0,19})$"}};}
Json schema(Json properties){Json required=Json::array();for(auto it=properties.begin();it!=properties.end();++it)required.push_back(it.key());return Json{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
Json text_schema(){return {{"type","string"},{"maxLength",256}};}
}
struct EditorRpc::Impl {
    EditorDocument& document;AgentAuthorization auth;bool active{true};
    struct Plan{PreparedDocumentEdit edit;std::string digest;std::chrono::steady_clock::time_point expiry;};std::map<std::string,Plan> plans;
    struct Operation{std::string request;Json result;};std::map<std::string,Operation> operations;
    Impl(EditorDocument& d,AgentAuthorization a):document(d),auth(std::move(a)){require(!auth.project.empty() && !auth.host.empty() && auth.credential.size()>=32,"Invalid agent authorization");}
    void expire(){auto now=std::chrono::steady_clock::now();for(auto it=plans.begin();it!=plans.end();)if(it->second.expiry<now){document.discard(it->second.edit);it=plans.erase(it);}else ++it;}
    Json envelope(Json data){return {{"project",auth.project},{"host",auth.host},{"domain","Authoring"},{"revision",std::to_string(document.revision())},{"data",std::move(data)}};}
    Json catalog(){Json tools=Json::array();auto add=[&](const char* name,const char* description,Json fields,bool mutation){tools.push_back({{"name",name},{"description",description},{"inputSchema",schema(fields)},{"authoring",mutation}});};
        add("describe","Describe project, capabilities and stable property schema",Json::object(),false);
        add("inspect","Inspect exact authored entities at an expected revision (maximum 64)",{{"revision",number_schema()},{"offset",number_schema()}},false);
        auto change=schema({{"object",text_schema()},{"type",{{"type","integer"},{"minimum",1},{"maximum",2}}},{"property",{{"type","integer"},{"minimum",1},{"maximum",7}}},{"value",{{"type","number"}}}});
        add("prepare","Prepare a non-mutating typed property transaction",{{"revision",number_schema()},{"scope",{{"type","string"},{"enum",{"placement","definition"}}}},{"changes",{{"type","array"},{"minItems",1},{"maxItems",64},{"items",change}}}},true);
        add("commit","Commit a prepared digest once using an operation UUID",{{"plan",number_schema()},{"digest",text_schema()},{"operation",text_schema()}},true);
        add("discard","Discard a prepared plan",{{"plan",number_schema()}},true);
        add("undo","Undo current history head at an expected revision",{{"revision",number_schema()},{"operation",text_schema()}},true);
        add("redo","Redo at an expected revision",{{"revision",number_schema()},{"operation",text_schema()}},true);
        add("operation","Inspect a known operation after uncertain delivery",{{"operation",text_schema()}},false);return tools;
    }
    Json run(const std::string& method,const Json& p){
        expire();if(method=="describe"){keys(p,{});Json types=Json::array();for(const auto& t:metadata()){Json properties=Json::array();for(const auto& f:t.properties)properties.push_back({{"id",f.id},{"name",f.name},{"minimum",f.minimum},{"maximum",f.maximum},{"agentWritable",bool(f.flags&AgentWritable)}});types.push_back({{"id",t.id},{"name",t.name},{"version",t.version},{"properties",properties}});}return {{"catalogVersion",1},{"tools",catalog()},{"types",types},{"authoring",auth.authoring}};}
        if(method=="inspect"){keys(p,{"revision","offset"});require(exact(p.at("revision"))==document.revision(),"Stale inspection revision");auto offset=exact(p.at("offset"));const auto& origins=document.plan().origins;require(offset<=origins.size(),"Invalid page offset");Json rows=Json::array();for(auto i=offset;i<std::min<std::uint64_t>(offset+64,origins.size());++i){const auto& origin=origins[static_cast<std::size_t>(i)];auto o=document.world().read(document.world().find(origin.object));rows.push_back({{"object",o.id.text()},{"name",origin.name},{"definition",origin.definition.text()},{"localPath",origin.local_path},{"layer",origin.layer},{"transform",{{"yaw",o.transform.yaw},{"x",o.transform.x},{"y",o.transform.y},{"z",o.transform.z},{"pitch",o.transform.pitch},{"roll",o.transform.roll},{"scale",o.transform.scale}}},{"health",{{"maximum",o.health.maximum},{"current",o.health.current}}}});}return {{"entities",rows},{"nextOffset",offset+rows.size()<origins.size()?Json(std::to_string(offset+rows.size())):Json(nullptr)}};}
        if(method=="operation"){keys(p,{"operation"});auto key=StableId::parse(p.at("operation").get<std::string>()).text();auto it=operations.find(key);require(it!=operations.end(),"Unknown operation; outcome uncertain outside this host/session");return it->second.result;}
        require(auth.authoring,"Inspection-only authorization");
        if(method=="prepare"){keys(p,{"revision","scope","changes"});require(plans.size()<8,"Prepared plan capacity");auto scope=p.at("scope").get<std::string>();require(scope=="placement" || scope=="definition","Invalid edit scope");const auto& rows=p.at("changes");require(rows.is_array() && !rows.empty() && rows.size()<=64,"Edit batch work limit");std::vector<PropertyChange> changes;for(const auto& row:rows){keys(row,{"object","type","property","value"});auto type=row.at("type").get<TypeId>(),property=row.at("property").get<PropertyId>();bool allowed=false;for(const auto& t:metadata())if(t.id==type)for(const auto& f:t.properties)if(f.id==property && (f.flags&AgentWritable))allowed=true;require(allowed,"Property outside agent capability");changes.push_back({StableId::parse(row.at("object").get<std::string>()),type,property,row.at("value").get<double>()});}auto edit=document.prepare(exact(p.at("revision")),changes,scope=="placement"?EditScope::Placement:EditScope::Definition);auto digest=sha256(p.dump());plans.emplace(std::to_string(edit.token),Plan{edit,digest,std::chrono::steady_clock::now()+std::chrono::minutes(2)});return {{"plan",std::to_string(edit.token)},{"digest",digest},{"revision",std::to_string(edit.revision)},{"summary",edit.summary},{"changes",rows}};}
        if(method=="discard"){keys(p,{"plan"});auto key=std::to_string(exact(p.at("plan")));auto it=plans.find(key);require(it!=plans.end(),"Unknown/expired plan");document.discard(it->second.edit);plans.erase(it);return {{"status","Discarded"}};}
        require(method=="commit" || method=="undo" || method=="redo","Unknown curated method");if(method=="commit")keys(p,{"plan","digest","operation"});else keys(p,{"revision","operation"});auto operation=StableId::parse(p.at("operation").get<std::string>()).text();require(bool(StableId::parse(operation)),"Zero operation UUID");auto request=method+":"+p.dump();if(auto it=operations.find(operation);it!=operations.end()){require(it->second.request==request,"Operation reused for different command");return it->second.result;}require(operations.size()<128,"Operation retention full; reconnect/re-authorize explicitly");
        // Reserve retry receipt storage before mutating, so routine response loss
        // does not cause the same transaction to run twice.
        auto [record,inserted]=operations.emplace(operation,Operation{request,Json()});
        try{if(method=="commit"){auto key=std::to_string(exact(p.at("plan")));auto it=plans.find(key);require(it!=plans.end() && p.at("digest")==it->second.digest,"Unknown/expired plan or digest mismatch");document.commit(it->second.edit);plans.erase(it);}else if(method=="undo")document.undo(exact(p.at("revision")));else document.redo(exact(p.at("revision")));record->second.result={{"status","CommittedUnsaved"},{"operation",operation},{"revision",std::to_string(document.revision())}};return record->second.result;}catch(...){operations.erase(record);throw;}
    }
};
EditorRpc::EditorRpc(EditorDocument& d,AgentAuthorization a):impl_(std::make_unique<Impl>(d,std::move(a))){}
EditorRpc::~EditorRpc()=default;
void EditorRpc::revoke(){impl_->active=false;for(const auto& [id,plan]:impl_->plans)impl_->document.discard(plan.edit);impl_->plans.clear();}
std::string EditorRpc::dispatch(std::string_view bytes){Json id=nullptr;try{require(bytes.size()<=65536,"RPC frame size limit");std::vector<std::set<std::string>> stack;unsigned work{};auto request=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=16 && ++work<=8192,"RPC parse work limit");if(event==Json::parse_event_t::object_start)stack.emplace_back();if(event==Json::parse_event_t::key)require(stack.back().insert(value.get<std::string>()).second,"Duplicate RPC field");if(event==Json::parse_event_t::object_end)stack.pop_back();return true;});keys(request,{"jsonrpc","id","method","params"});id=request.at("id");require(request.at("jsonrpc")=="2.0" && id.is_string() && id.get_ref<const std::string&>().size()<=64,"Only bounded string request IDs supported; no notification/batch mutations");auto params=request.at("params");keys(params,{"authorization","arguments"});const auto& auth=params.at("authorization");keys(auth,{"project","host","credential"});require(impl_->active && auth.at("project")==impl_->auth.project && auth.at("host")==impl_->auth.host && auth.at("credential")==impl_->auth.credential,"Authorization/project/host mismatch or revoked");auto result=impl_->envelope(impl_->run(request.at("method").get<std::string>(),params.at("arguments")));return Json{{"jsonrpc","2.0"},{"id",id},{"result",result}}.dump();}catch(const std::exception& e){return Json{{"jsonrpc","2.0"},{"id",id},{"error",{{"code",-32000},{"message",e.what()}}}}.dump();}}
}
