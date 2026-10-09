#pragma once
#include <cstdint>
#include <compare>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Explicit schema identities; never derive these from names, offsets or Flecs IDs.
using TypeId = std::uint32_t;
using PropertyId = std::uint32_t;
struct StableId {
    std::uint64_t high{}, low{};
    auto operator<=>(const StableId&) const = default;
    std::string text() const;
    static StableId parse(std::string_view);
    explicit operator bool() const { return high || low; }
};
struct EntityHandle {
    std::uint64_t domain{}, epoch{}, runtime{}; // runtime includes Flecs generation
    auto operator<=>(const EntityHandle&) const = default;
};
enum class WorldDomain { Authoring, Server, ClientPresentation, Preview };
enum class Authority { Authoring, Server, Presentation };
enum class Phase { Idle, Script, Commit, Publish };
enum PropertyFlags : unsigned { Editable=1, Replicated=2, SaveGame=4, ReadOnly=8, AgentWritable=16, ScriptVisible=32 };
enum class ValueKind { Number, ExactUnsigned };
struct PropertyMetadata {
    PropertyId id;
    std::string_view name;
    ValueKind kind;
    unsigned flags;
    double minimum, maximum;
    std::size_t offset;
};
struct TypeMetadata {
    TypeId id;
    unsigned version;
    std::string_view name;
    std::span<const PropertyMetadata> properties;
};
struct Transform { double yaw{},x{},y{},z{},pitch{},roll{},scale{1}; };
struct Health { double maximum{100}, current{100}; };
struct NetworkIdentity { std::uint64_t value{}; };
struct ScriptBinding {StableId id,asset;std::string config_json;bool enabled{true};};
struct ScriptBindings {std::uint64_t count{};std::vector<ScriptBinding> records;};
struct ObjectData {
    StableId id;
    Transform transform;
    Health health;
    NetworkIdentity network;
    StableId target; // durable reference; remapped only after population
    std::string optional_json; // opaque optional authoring data, preserved across native edits
    ScriptBindings scripts;
};
struct NumberEdit { EntityHandle entity; TypeId type; PropertyId property; double value; };
std::span<const TypeMetadata> metadata();
std::string luau_types();

class WorldSession;
class World {
public:
    explicit World(WorldDomain, std::size_t capacity=1024, std::size_t command_limit=1024);
    ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    WorldDomain domain() const;
    Phase phase() const;
    bool valid(EntityHandle) const;
    EntityHandle create(const ObjectData&);
    ObjectData read(EntityHandle) const; // returns values, no borrowed ECS pointers
    EntityHandle find(StableId) const;
    EntityHandle target(EntityHandle) const;
    void destroy(EntityHandle, Authority);
    void reset();
    void begin_scripts();
    void stage_yaw(EntityHandle, double, Authority);
    void commit();
    void publish();
    void edit_number(EntityHandle, TypeId, PropertyId, double, Authority);
    void set_transform(EntityHandle,const Transform&,Authority);
    void apply_edits(std::span<const NumberEdit>, Authority);
    std::string serialize() const;
    // Validate the complete scene before creating entities. Destination must be empty.
    std::vector<EntityHandle> load(std::string_view);
    bool reflection_backed() const;
private:
    friend class ScriptRuntime;
    void claim_script_runtime();
    void release_script_runtime();
    std::size_t script_checkpoint() const;
    void rollback_script(std::size_t);
    friend class WorldSession;
    void apply_replica_transform(EntityHandle,const Transform&);
    void apply_server_health(EntityHandle,const Health&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
