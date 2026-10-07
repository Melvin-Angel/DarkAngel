#include "script_state.hpp"
#include <darkangel/world.hpp>
#include <charconv>
#include <cctype>
#include <vector>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
std::string type(const Json& node){return node.at("type").get<std::string>();}
void declaration(const Json& node,unsigned depth,std::size_t& count){
    require(depth<=16 && ++count<=1024 && node.is_object(),"State schema depth/node limit");auto kind=type(node);
    require(kind=="record" || kind=="array" || kind=="number" || kind=="boolean" || kind=="string" || kind=="entity" || kind=="asset" || kind=="uint64","Unsupported declared state type");
    if(kind=="record"){
        require(node.at("fields").is_object() && node.at("fields").size()<=128,"State record field limit");std::set<PropertyId> ids;
        for(const auto& [name,field]:node.at("fields").items()){
            require(!name.empty() && name.size()<=64 && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==name.npos && !std::isdigit(static_cast<unsigned char>(name[0])),"State field must be a Luau identifier");
            require(name!="end" && name!="function" && name!="return" && name!="local" && name!="type" && name!="if" && name!="then" && name!="else","Reserved state field name");
            require(field.at("id").is_number_unsigned() && field.at("id").get<std::uint64_t>()>0 && field.at("id").get<std::uint64_t>()<=0xffffffffULL && ids.insert(field.at("id").get<PropertyId>()).second,"State fields need unique stable PropertyIds");declaration(field,depth+1,count);
        }
    }else if(kind=="array"){require(node.value("max_count",128u)<=1024,"State array limit");declaration(node.at("element"),depth+1,count);auto element=type(node.at("element"));require((element!="entity" && element!="asset") || node.at("element").value("required",false),"Nullable reference array elements are unsupported");}
    else if(kind=="string")require(node.value("max_bytes",1024u)<=16384,"State string limit");
}
Json default_value(const Json& node){if(node.contains("default"))return node.at("default");auto kind=type(node);if(kind=="number")return 0.;if(kind=="boolean")return false;if(kind=="string")return "";if(kind=="uint64")return "0";if(kind=="entity" || kind=="asset")return nullptr;if(kind=="array")return Json::array();Json result=Json::object();for(const auto& [name,field]:node.at("fields").items())result[name]=default_value(field);return result;}
void validate_value(const Json& value,const Json& node,unsigned depth,std::size_t& count){
    require(depth<=16 && ++count<=4096,"State data depth/work limit");auto kind=type(node);
    if(kind=="record"){require(value.is_object() && value.size()==node.at("fields").size(),"Missing or undeclared state field");for(const auto& [name,field]:node.at("fields").items()){require(value.contains(name),"Missing state field");validate_value(value.at(name),field,depth+1,count);}return;}
    if(kind=="array"){require(value.is_array() && value.size()<=node.value("max_count",128u),"State array count/type mismatch");for(const auto& item:value)validate_value(item,node.at("element"),depth+1,count);return;}
    if(kind=="number"){require(value.is_number(),"State must contain only declared numeric angle/value");auto n=value.get<double>();require(std::isfinite(n) && n>=node.value("min",-1e12) && n<=node.value("max",1e12),"State number out of bounds");return;}
    if(kind=="boolean"){require(value.is_boolean(),"Expected declared boolean state");return;}
    if(value.is_null() && (kind=="entity" || kind=="asset")){require(!node.value("required",false),"Required state reference is unresolved");return;}
    require(value.is_string(),"Expected declared exact string/reference state");auto s=value.get<std::string>();
    if(kind=="string"){require(s.size()<=node.value("max_bytes",1024u),"State string size limit");return;}
    if(kind=="entity"){StableId::parse(s);return;}
    if(kind=="asset"){require(s.size()==36 && s[8]=='-' && s[13]=='-' && s[18]=='-' && s[23]=='-' && s[14]=='4' && std::string_view("89ab").find(s[19])!=std::string_view::npos,"Invalid UUIDv4 state AssetRef");auto hex=s;hex.erase(23,1);hex.erase(18,1);hex.erase(13,1);hex.erase(8,1);require(hex.find_first_not_of("0123456789abcdef")==hex.npos,"Non-canonical state AssetRef");return;}
    require(!s.empty() && s.size()<=20 && (s=="0" || s[0]!='0'),"Invalid canonical uint64 state");std::uint64_t n{};auto parsed=std::from_chars(s.data(),s.data()+s.size(),n);require(parsed.ec==std::errc{} && parsed.ptr==s.data()+s.size(),"State uint64 overflow/encoding");
}
void push_value(lua_State* state,const Json& value){if(value.is_null()){lua_pushnil(state);return;}if(value.is_boolean()){lua_pushboolean(state,value.get<bool>());return;}if(value.is_number()){lua_pushnumber(state,value.get<double>());return;}if(value.is_string()){const auto& text=value.get_ref<const std::string&>();lua_pushlstring(state,text.data(),text.size());return;}lua_newtable(state);if(value.is_array()){int at=1;for(const auto& item:value){push_value(state,item);lua_rawseti(state,-2,at++);}}else for(const auto& [name,item]:value.items()){push_value(state,item);lua_setfield(state,-2,name.c_str());}}
void sync_value(lua_State* state,int index,const Json& value){
    index=lua_absindex(state,index);
    // Existing tables have already passed declared-state validation at the last yield.
    if(value.is_array()){
        auto old=lua_objlen(state,index);
        for(std::size_t i=0;i<value.size();++i){const auto& item=value[i];lua_rawgeti(state,index,static_cast<int>(i+1));
            if((item.is_object() || item.is_array()) && lua_istable(state,-1))sync_value(state,-1,item);
            else{lua_pop(state,1);push_value(state,item);}lua_rawseti(state,index,static_cast<int>(i+1));}
        for(auto i=value.size()+1;i<=static_cast<std::size_t>(old);++i){lua_pushnil(state);lua_rawseti(state,index,static_cast<int>(i));}
    }else for(const auto& [name,item]:value.items()){
        lua_getfield(state,index,name.c_str());if((item.is_object() || item.is_array()) && lua_istable(state,-1))sync_value(state,-1,item);
        else{lua_pop(state,1);push_value(state,item);}lua_setfield(state,index,name.c_str());
    }
}
Json read_value(lua_State* state,int index,const Json& node,unsigned depth,std::size_t& count,std::set<const void*>& path){
    require(depth<=16 && ++count<=4096,"State table depth/work limit");index=lua_absindex(state,index);auto kind=type(node);
    if(kind!="record" && kind!="array"){
        if(lua_isnil(state,index))return nullptr;
        if(kind=="number"){require(lua_type(state,index)==LUA_TNUMBER,"State must contain only declared numeric angle/value");return lua_tonumber(state,index);}
        if(kind=="boolean"){require(lua_type(state,index)==LUA_TBOOLEAN,"Expected boolean state");return lua_toboolean(state,index)!=0;}
        require(lua_type(state,index)==LUA_TSTRING,"Functions/threads/userdata are not declared state");std::size_t length{};const char* value=lua_tolstring(state,index,&length);require(length<=16384,"State string limit");return std::string(value,length);
    }
    require(lua_istable(state,index),"Declared state must be a table");require(!lua_getmetatable(state,index),"State metatables are unsupported");auto pointer=lua_topointer(state,index);require(path.insert(pointer).second,"Cyclic script state rejected");
    Json result=kind=="record"?Json::object():Json::array();std::size_t fields{};auto length=kind=="array"?static_cast<std::size_t>(lua_objlen(state,index)):0;
    require(length<=node.value("max_count",128u),"State array limit");lua_pushnil(state);
    while(lua_next(state,index)){
        if(kind=="record"){require(lua_type(state,-2)==LUA_TSTRING,"Record state keys must be named");auto name=std::string(lua_tostring(state,-2));require(node.at("fields").contains(name),"Undeclared state field");}
        else{require(lua_type(state,-2)==LUA_TNUMBER,"Array state must use integer indices");auto key=lua_tonumber(state,-2);require(key>=1 && key<=static_cast<double>(length) && std::floor(key)==key,"Sparse/unexpected array state key");}
        ++fields;lua_pop(state,1);
    }
    if(kind=="record")for(const auto& [name,field]:node.at("fields").items()){lua_getfield(state,index,name.c_str());result[name]=read_value(state,-1,field,depth+1,count,path);lua_pop(state,1);}
    else {require(fields==length,"Sparse state arrays are unsupported");for(std::size_t i=1;i<=length;++i){lua_rawgeti(state,index,static_cast<int>(i));result.push_back(read_value(state,-1,node.at("element"),depth+1,count,path));lua_pop(state,1);}}
    path.erase(pointer);return result;
}
std::string lua_type(const Json& node){auto kind=type(node);if(kind=="number" || kind=="boolean" || kind=="string")return kind;if(kind=="array")return "{"+lua_type(node.at("element"))+"}";if(kind=="record"){std::string result="{";for(const auto& [name,field]:node.at("fields").items())result+=name+":"+lua_type(field)+",";return result+"}";}return "string"+std::string((kind=="entity" || kind=="asset") && !node.value("required",false)?"?":"");}
Json stable(const Json& value,const Json& node){auto kind=type(node);if(kind=="record"){Json result=Json::object();for(const auto& [name,field]:node.at("fields").items())result[std::to_string(field.at("id").get<PropertyId>())]=stable(value.at(name),field);return result;}if(kind=="array"){Json result=Json::array();for(const auto& item:value)result.push_back(stable(item,node.at("element")));return result;}return value;}
void references(const Json& value,const Json& node,const World& world){auto kind=type(node);if(kind=="entity" && !value.is_null())require(world.valid(world.find(StableId::parse(value.get<std::string>()))),"Unresolved injected EntityRef");else if(kind=="record")for(const auto& [name,field]:node.at("fields").items())references(value.at(name),field,world);else if(kind=="array")for(const auto& item:value)references(item,node.at("element"),world);}
}
ScriptStateSchema::ScriptStateSchema(std::string_view declaration_text){
    if(declaration_text.empty())schema_={{"type","record"},{"fields",{{"angle",{{"id",1u},{"type","number"},{"default",0.}}}}}};
    else{require(declaration_text.size()<=64*1024,"State schema source limit");std::vector<std::set<std::string>> keys;schema_=Json::parse(declaration_text,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32,"State schema JSON depth");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate state schema key");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
    std::size_t count{};declaration(schema_,0,count);require(type(schema_)=="record","Instance state root must be a declared record");validate(defaults());
}
std::string ScriptStateSchema::canonical() const{return schema_.dump();}
std::string ScriptStateSchema::luau_type() const{return "type BehaviorState = "+lua_type(schema_)+"\n";}
ScriptStateSchema::Json ScriptStateSchema::defaults() const{return default_value(schema_);}
void ScriptStateSchema::validate(const Json& value) const{std::size_t count{};validate_value(value,schema_,0,count);require(value.dump().size()<=128*1024,"Declared state byte limit");}
void ScriptStateSchema::push(lua_State* state,const Json& value) const{push_value(state,value);}
void ScriptStateSchema::sync(lua_State* state,int index,const Json& value) const{validate(value);sync_value(state,index,value);}
ScriptStateSchema::Json ScriptStateSchema::read(lua_State* state,int index) const{std::size_t count{};std::set<const void*> path;auto result=read_value(state,index,schema_,0,count,path);validate(result);return result;}
ScriptStateSchema::Json ScriptStateSchema::stable_fields(const Json& value) const{validate(value);return stable(value,schema_);}
void ScriptStateSchema::validate_references(const Json& value,const World& world) const{validate(value);references(value,schema_,world);}
std::string default_script_config_schema(){return R"({"type":"record","fields":{"speed":{"id":1,"type":"number","min":-1000000,"max":1000000,"default":1}}})";}
ScriptStateSchema::Json parse_script_data(std::string_view text){require(text.size()<=128*1024,"Script data byte limit");std::vector<std::set<std::string>> keys;std::size_t count{};return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){require(depth<=32 && ++count<=16384,"Script data work/depth limit");if(event==Json::parse_event_t::object_start)keys.emplace_back();if(event==Json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate script data key");if(event==Json::parse_event_t::object_end)keys.pop_back();return true;});}
}
