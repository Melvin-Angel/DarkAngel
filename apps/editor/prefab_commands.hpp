#pragma once
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace darkangel::editor_app {
// R4 scene-side prefab commands. Every command is one EditorDocument transaction (one Undo step) on the scene's own source;
// linked prefab definitions are added with EditorDocument::link and never edited here. The ImGui workspace and exercises
// share these commands.
struct PrefabPlacement {double x{},y{},z{},yaw{};};
struct PrefabInstanceRef {std::string mount,member;AssetId prefab;};
// The linked prefab instance a scene object belongs to: a member of one of the scene's own top-level mounts of a linked source.
std::optional<PrefabInstanceRef> prefab_instance_of(EditorDocument&,StableId object);
// Top-level instances of linked prefabs, by mount identity.
std::vector<PrefabInstanceRef> prefab_instances(const EditorDocument&);
PrefabPlacement instance_placement(const EditorDocument&,const std::string& mount);
// Links (or refreshes) the published prefab text and mounts a new instance with a fresh mount identity. Returns the mount.
std::string place_prefab(EditorDocument&,AssetId prefab,const std::string& prefab_text,const PrefabPlacement&);
void set_instance_placement(EditorDocument&,const std::string& mount,const PrefabPlacement&);
// A new mount of the same prefab with the same overrides, offset by `offset`. Retained identities are not copied, so the copy
// never shares world-ledger state with the original.
std::string duplicate_instance(EditorDocument&,const std::string& mount,const PrefabPlacement& offset);
void delete_instance(EditorDocument&,const std::string& mount);
// Instance overrides: one per overridden component field, added component or removed component of a member.
struct PrefabOverride {std::string member,component,field;nlohmann::json prefab_value,instance_value;enum Kind {Field,AddedComponent,RemovedComponent} kind{Field};};
std::vector<PrefabOverride> instance_overrides(const EditorDocument&,const std::string& mount);
// The member's components with this instance's overrides applied (the values Play uses), and the prefab's own values.
nlohmann::json instance_member(const EditorDocument&,const std::string& mount,const std::string& member);
nlohmann::json prefab_member(const EditorDocument&,AssetId prefab,const std::string& member);
// Sets one component field on this instance. Setting the prefab's own value removes the override instead.
void set_instance_field(EditorDocument&,const std::string& mount,const std::string& member,const std::string& component,const std::string& field,const nlohmann::json& value);
// Adds a component (with its defaults) to, or removes one from, this instance's member.
void add_instance_component(EditorDocument&,const std::string& mount,const std::string& member,const std::string& component,const nlohmann::json& defaults);
void remove_instance_component(EditorDocument&,const std::string& mount,const std::string& member,const std::string& component);
// Removes the selected overrides (an empty field selects a whole component override); the rest stay.
struct OverrideKey {std::string member,component,field;};
void revert_overrides(EditorDocument&,const std::string& mount,const std::vector<OverrideKey>&);
// Apply Selected, scene half: the prefab source with the selected overrides written into its definition. The caller publishes
// it (validated), links it, then calls revert_overrides for the same keys; until both finish, Play resolves the same values,
// because an override equal to the definition changes nothing.
std::string prefab_with_overrides(const EditorDocument&,const std::string& mount,const std::vector<OverrideKey>&,const std::string& prefab_text);
// Swap: replaces the prefab an instance links while keeping its mount, placement and the persistent identity of its gameplay
// member. Only single-member prefabs with the same gameplay contract (the same world-object components) are compatible.
struct SwapPreview {bool compatible{};std::string reason;std::vector<std::string> carried,discarded;};
SwapPreview prepare_swap(EditorDocument&,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_replacement_defaults);
void commit_swap(EditorDocument&,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_replacement_defaults);
// Create Prefab from Selection: a prefab source made from one of the scene's own objects (its pose becomes the instance
// placement), and the replacement of that object by a linked instance that retains its persistent identity.
std::string prefab_from_object(EditorDocument&,StableId object,AssetId prefab,const std::string& name);
std::string replace_with_instance(EditorDocument&,StableId object,AssetId prefab,const std::string& prefab_text);
}
