#pragma once
#include <darkangel/world.hpp>
#include <map>
#include <string>
#include <vector>
namespace darkangel {
struct ScriptModule {
    StableId asset;
    std::string path,source;
    std::vector<std::string> dependencies; // canonical module paths, explicit author declaration
};
struct ScriptPackage {
    std::string entry;
    std::vector<ScriptModule> modules;
    std::map<std::string,std::string> aliases;
    std::string state_schema; // bounded JSON declaration; omitted means the angle fixture schema
    std::string config_schema;
};
struct CookedScriptModule {
    StableId asset;
    std::string path,bytecode,source_hash;
    std::vector<std::string> dependencies;
};
struct CookedScriptPackage {
    std::string entry,api_hash;
    std::vector<CookedScriptModule> modules;
    std::map<std::string,std::string> aliases;
    std::string state_schema;
    std::string config_schema;
    std::string serialize() const;
    static CookedScriptPackage deserialize(std::string_view);
};
// Compiler, analyzer and runtime use this identical resolver and declared closure.
std::string resolve_script_import(std::string_view module,std::string_view request,const std::map<std::string,std::string>& aliases);
CookedScriptPackage cook_scripts(const ScriptPackage&);
}
