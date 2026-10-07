#include <darkangel/script_package.hpp>
#include <darkangel/hash.hpp>
#include "script_state.hpp"
#include <Luau/Ast.h>
#include <Luau/Parser.h>
#include <Luau/Compiler.h>
#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
void path_check(std::string_view path){require(!path.empty() && path.size()<=256 && path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")==path.npos && !path.starts_with('/') && path.find("..") == path.npos && path.find("//")==path.npos,"Invalid canonical script path");}
std::string prelude(){auto s=luau_types();std::size_t i{};while((i=s.find("export type ",i))!=s.npos)s.erase(i,7);return s;}
struct Visitor : Luau::AstVisitor {
    std::vector<std::string> imports;std::size_t depth{};
    bool visit(Luau::AstExprFunction* f) override {auto old=depth;depth=f->functionDepth;f->body->visit(this);depth=old;return false;}
    void assignment(Luau::AstExpr* v){if(auto* local=v->as<Luau::AstExprLocal>())require(local->local->functionDepth>=depth,"Mutable closure/upvalue assignment is forbidden");}
    bool visit(Luau::AstStatAssign* a) override {for(auto* v:a->vars)assignment(v);return true;}
    bool visit(Luau::AstStatCompoundAssign* a) override {assignment(a->var);return true;}
    bool visit(Luau::AstExprGlobal* global) override {require(std::string_view(global->name.value)!="require","require must be a direct literal import");return true;}
    bool visit(Luau::AstExprCall* call) override {
        if(auto* global=call->func->as<Luau::AstExprGlobal>();global && std::string_view(global->name.value)=="require") {
            require(call->args.size==1,"require must have one literal path");auto* text=call->args.data[0]->as<Luau::AstExprConstantString>();
            require(text!=nullptr,"Computed require paths are forbidden");imports.emplace_back(text->value.data,text->value.size);
            return false;
        }
        return true;
    }
};
struct Resolver : Luau::FileResolver {
    std::map<std::string,std::string> sources;std::map<std::string,std::string> aliases;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        auto it=sources.find(name);if(it==sources.end())return std::nullopt;return Luau::SourceCode{it->second,Luau::SourceCode::Module};
    }
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context,Luau::AstExpr* expression,const Luau::TypeCheckLimits&) override {
        auto* s=expression->as<Luau::AstExprConstantString>();if(!context || !s)return std::nullopt;
        return Luau::ModuleInfo{resolve_script_import(context->name,std::string_view(s->value.data,s->value.size),aliases)};
    }
};
void graph(const CookedScriptPackage& package) {
    require(package.modules.size()>0 && package.modules.size()<=64,"Script module limit");path_check(package.entry);
    require(package.aliases.size()<=16,"Script alias limit");
    for(const auto& [alias,path]:package.aliases){path_check(alias);require(alias.find('/')==alias.npos,"Alias must be one segment");path_check(path);}
    std::map<std::string,const CookedScriptModule*> modules;std::set<StableId> identities;
    for(const auto& m:package.modules){path_check(m.path);require(static_cast<bool>(m.asset) && identities.insert(m.asset).second && modules.emplace(m.path,&m).second,"Duplicate module path/identity");require(m.dependencies.size()<=64,"Module dependency limit");}
    std::map<std::string,int> seen;std::vector<std::string> chain;
    std::function<void(const std::string&)> visit=[&](const std::string& path){
        require(modules.contains(path),"Missing declared script module");
        if(seen[path]==1){std::string message="Script dependency cycle: ";for(const auto& p:chain)message+=p+" -> ";throw std::runtime_error(message+path);}
        if(seen[path]==2)return;require(chain.size()<32,"Module depth limit");seen[path]=1;chain.push_back(path);
        for(const auto& dep:modules.at(path)->dependencies)visit(dep);chain.pop_back();seen[path]=2;
    };
    visit(package.entry);require(seen.size()==modules.size(),"Package includes modules outside the declared entry closure");
}
std::string hex(std::string_view bytes){constexpr char h[]="0123456789abcdef";std::string result;for(unsigned char c:bytes){result+=h[c>>4];result+=h[c&15];}return result;}
std::string unhex(std::string_view text){require(text.size()%2==0 && text.size()<=512*1024,"Bytecode size limit");std::string result;auto digit=[](char c){auto pos=std::string_view("0123456789abcdef").find(c);require(pos!=std::string_view::npos,"Invalid bytecode encoding");return static_cast<unsigned>(pos);};for(std::size_t i=0;i<text.size();i+=2)result+=static_cast<char>((digit(text[i])<<4)|digit(text[i+1]));return result;}
}
std::string resolve_script_import(std::string_view module,std::string_view request,const std::map<std::string,std::string>& aliases){
    require(!request.empty() && request.size()<=256 && request.find('\\')==request.npos,"Invalid script import");std::string target;
    if(request.starts_with('@')){auto slash=request.find('/');require(slash!=request.npos,"Alias import requires a module");auto it=aliases.find(std::string(request.substr(1,slash-1)));require(it!=aliases.end(),"Unknown script alias");target=it->second+"/"+std::string(request.substr(slash+1));}
    else {require(request.starts_with("./") || request.starts_with("../"),"Imports must be relative or use declared aliases");target=(std::filesystem::path(module).parent_path()/std::filesystem::path(request)).lexically_normal().generic_string();}
    if(target.ends_with(".luau"))target.resize(target.size()-5);path_check(target);return target;
}
CookedScriptPackage cook_scripts(const ScriptPackage& source){
    ScriptStateSchema schema(source.state_schema);
    ScriptStateSchema config(source.config_schema.empty()?default_script_config_schema():source.config_schema);
    auto config_type=config.luau_type();config_type.replace(config_type.find("BehaviorState"),13,"BehaviorConfig");
    CookedScriptPackage result{source.entry,sha256(luau_types()),{},source.aliases,schema.canonical(),config.canonical()};Resolver resolver;resolver.aliases=source.aliases;std::size_t bytes{};
    for(const auto& m:source.modules){
        bytes+=m.source.size();require(m.source.size()<=64*1024 && bytes<=1024*1024,"Script source closure limit");
        require(m.source.starts_with("--!strict") && m.source.find("--!nocheck")==m.source.npos && m.source.find("--!nonstrict")==m.source.npos,"Strict script declaration required");
        Luau::Allocator allocator;Luau::AstNameTable names(allocator);auto parsed=Luau::Parser::parse(m.source.data(),m.source.size(),names,allocator);
        require(parsed.errors.empty() && parsed.root,"Script parse failed");Visitor visitor;parsed.root->visit(&visitor);std::set<std::string> imports;
        for(const auto& i:visitor.imports)imports.insert(resolve_script_import(m.path,i,source.aliases));
        require(imports==std::set<std::string>(m.dependencies.begin(),m.dependencies.end()) && imports.size()==m.dependencies.size(),"Static imports differ from declared dependencies");
        auto checked=prelude()+schema.luau_type()+config_type+m.source;resolver.sources.emplace(m.path,checked);
        result.modules.push_back({m.asset,m.path,Luau::compile(checked),sha256(m.source),m.dependencies});
    }
    graph(result);Luau::NullConfigResolver analysis_config;analysis_config.defaultConfig.mode=Luau::Mode::Strict;Luau::FrontendOptions options;options.moduleTimeLimitSec=.25;
    Luau::Frontend frontend(&resolver,&analysis_config,options);Luau::registerBuiltinGlobals(frontend,frontend.globals);auto check=frontend.check(source.entry);
    require(check.timeoutHits.empty(),"Script analysis timeout");
    if(!check.errors.empty())throw std::runtime_error(check.errors.front().moduleName+":"+std::to_string(check.errors.front().location.begin.line+1)+": "+Luau::toString(check.errors.front()));
    std::sort(result.modules.begin(),result.modules.end(),[](const auto& a,const auto& b){return a.path<b.path;});return result;
}
std::string CookedScriptPackage::serialize() const {
    graph(*this);nlohmann::json payload={{"entry",entry},{"api",api_hash},{"state_schema",ScriptStateSchema(state_schema).canonical()},{"config_schema",ScriptStateSchema(config_schema.empty()?default_script_config_schema():config_schema).canonical()},{"aliases",aliases},{"modules",nlohmann::json::array()}};
    for(const auto& m:modules)payload["modules"].push_back({{"id",m.asset.text()},{"path",m.path},{"code",hex(m.bytecode)},{"source",m.source_hash},{"deps",m.dependencies}});
    auto body=payload.dump();require(body.size()<=4*1024*1024,"Cooked script package limit");return nlohmann::json{{"schema",1},{"luau","0.729"},{"sha256",sha256(body)},{"payload",payload}}.dump();
}
CookedScriptPackage CookedScriptPackage::deserialize(std::string_view text){
    require(text.size()<=4*1024*1024,"Cooked script package limit");
    std::vector<std::set<std::string>> keys;auto root=nlohmann::json::parse(text,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value){require(depth<=16,"Cooked script JSON depth");if(event==nlohmann::json::parse_event_t::object_start)keys.emplace_back();if(event==nlohmann::json::parse_event_t::key)require(keys.back().insert(value.get<std::string>()).second,"Duplicate cooked script key");if(event==nlohmann::json::parse_event_t::object_end)keys.pop_back();return true;});
    require(root.at("schema")==1 && root.at("luau")=="0.729","Cooked script ABI/schema mismatch");auto& p=root.at("payload");require(root.at("sha256")==sha256(p.dump()),"Corrupt script artifact");
    CookedScriptPackage result;result.entry=p.at("entry").get<std::string>();result.api_hash=p.at("api").get<std::string>();result.aliases=p.at("aliases").get<std::map<std::string,std::string>>();
    result.state_schema=ScriptStateSchema(p.at("state_schema").get<std::string>()).canonical();
    result.config_schema=ScriptStateSchema(p.at("config_schema").get<std::string>()).canonical();
    require(result.api_hash==sha256(luau_types()),"Cooked script API mismatch");
    for(const auto& m:p.at("modules")){require(result.modules.size()<64,"Cooked module limit");result.modules.push_back({StableId::parse(m.at("id").get<std::string>()),m.at("path").get<std::string>(),unhex(m.at("code").get<std::string>()),m.at("source").get<std::string>(),m.at("deps").get<std::vector<std::string>>()});}
    graph(result);return result;
}
}
