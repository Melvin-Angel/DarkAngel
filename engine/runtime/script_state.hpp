#pragma once
#include <nlohmann/json.hpp>
#include <lua.h>
#include <string>
namespace darkangel {
class World;
// Script-owned declared data, separate from native ECS components and VM internals.
class ScriptStateSchema {
public:
    using Json=nlohmann::json;
    explicit ScriptStateSchema(std::string_view declaration={});
    std::string canonical() const;
    std::string luau_type() const;
    Json defaults() const;
    void validate(const Json&) const;
    void push(lua_State*,const Json&) const;
    void sync(lua_State*,int index,const Json&) const; // preserve table identity across task waits
    Json read(lua_State*,int index) const;
    Json stable_fields(const Json&) const;
    void validate_references(const Json&,const World&) const;
private:
    Json schema_;
};
std::string default_script_config_schema();
ScriptStateSchema::Json parse_script_data(std::string_view,std::size_t byte_limit=128*1024);
}
