#pragma once
#include <set>
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace darkangel::editor_app {
// R4 scene-side prefab commands. Every command is one EditorDocument transaction (one Undo step) on the scene's own source;
// linked prefab definitions are added with EditorDocument::link and never edited here. The ImGui workspace and exercises
// share these commands.
struct PrefabPlacement {double x{},y{},z{},yaw{};};
// mount: the scene's top-level mount of a linked prefab. member: the member's local key in that prefab, or for a member of a
// nested prefab its path inside the instance ("inner-mount/member", nested true): those are edited in the Prefabs workspace.
struct PrefabInstanceRef {std::string mount,member;AssetId prefab;bool nested{};};
// The linked prefab instance a scene object belongs to: a member of one of the scene's own top-level mounts of a linked source.
std::optional<PrefabInstanceRef> prefab_instance_of(EditorDocument&,StableId object);
// Top-level instances of linked prefabs, by mount identity.
std::vector<PrefabInstanceRef> prefab_instances(const EditorDocument&);
PrefabPlacement instance_placement(const EditorDocument&,const std::string& mount);
// Links (or refreshes) the published prefab text, and the prefabs it mounts or derives from (`dependencies`, see
// EditorDocument::link_closure), and mounts a new instance with a fresh mount identity. Returns the mount.
std::string place_prefab(EditorDocument&,AssetId prefab,const std::string& prefab_text,const PrefabPlacement&,const AssemblySources& dependencies={});
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
// Render-only stable slot assignment. Empty material explicitly selects the model default; Revert restores prefab inheritance.
void set_instance_material(EditorDocument&,const std::string& mount,const std::string& member,AssetId slot,AssetId material);
// A scene-owned object's assignment; no prefab definition or imported model metadata is changed.
void set_object_material(EditorDocument&,StableId object,AssetId slot,AssetId material);
// Adds a component (with its defaults) to, or removes one from, this instance's member.
void add_instance_component(EditorDocument&,const std::string& mount,const std::string& member,const std::string& component,const nlohmann::json& defaults);
void remove_instance_component(EditorDocument&,const std::string& mount,const std::string& member,const std::string& component);
// Removes the selected overrides (an empty field selects a whole component override); the rest stay.
struct OverrideKey {std::string member,component,field;};
void revert_overrides(EditorDocument&,const std::string& mount,const std::vector<OverrideKey>&);
// Apply Selected, scene half: the prefab source with the selected overrides written into its definition. The caller publishes
// it (validated), links it, then calls revert_overrides for the same keys; until both finish, Play resolves the same values,
// because an override equal to the definition changes nothing. A variant takes them as patches of its own.
std::string prefab_with_overrides(const EditorDocument&,const std::string& mount,const std::vector<OverrideKey>&,const std::string& prefab_text);
// Swap: replaces the prefab an instance links while keeping its mount, placement and the persistent identity of its gameplay
// member. Only single-member prefabs with the same gameplay contract (the same world-object components) are compatible.
struct SwapPreview {bool compatible{};std::string reason;std::vector<std::string> carried,discarded;};
struct MaterialSwap {std::map<AssetId,AssetId> remap;std::set<AssetId> replacement_slots;};
SwapPreview prepare_swap(EditorDocument&,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_replacement_defaults,const AssemblySources& dependencies={},const MaterialSwap& materials={});
void commit_swap(EditorDocument&,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_replacement_defaults,const AssemblySources& dependencies={},const MaterialSwap& materials={});
// Create Prefab from Selection: a prefab source made from scene objects of the scene's own source (the first object's pose is
// the pivot and becomes the instance placement; the others keep their pose relative to it), and the replacement of those
// objects by one linked instance whose members retain their persistent identities. References among the captured objects stay
// local; scene objects that reference a captured object address it through an export of the prefab, rewritten on replacement.
// Captured objects that reference other scene objects are refused (a prefab cannot address the scene around it).
std::string prefab_from_objects(EditorDocument&,const std::vector<StableId>& objects,AssetId prefab,const std::string& name);
std::string replace_with_instance(EditorDocument&,const std::vector<StableId>& objects,AssetId prefab,const std::string& prefab_text);
std::string prefab_from_object(EditorDocument&,StableId object,AssetId prefab,const std::string& name);
std::string replace_with_instance(EditorDocument&,StableId object,AssetId prefab,const std::string& prefab_text);
// Unpack: replaces a linked instance by scene-owned objects that keep their persistent identities (the scene's retained identity
// map), their resolved values (definition plus overrides) and their poses. References into the instance are rewritten to the
// new objects. Returns the new local keys by member. An instance of a prefab that mounts other prefabs is refused: their
// members' identities would need a deeper retained map than one scene level.
std::map<std::string,std::string> unpack_instance(EditorDocument&,const std::string& mount);
// Prefab definition helpers shared by the Prefabs workspace (no scene document): the resolved member entities of a prefab
// (a variant's base with its patches applied), a variant made from a base, and the explicit rebase of a variant onto its
// base's current text (every patch must still apply; the fingerprint is renewed).
nlohmann::json prefab_entities(AssetId prefab,const AssemblySources& sources);
std::string prefab_fingerprint(const std::string& base_text);
nlohmann::json make_variant(AssetId variant,const std::string& name,AssetId base,const std::string& base_text);
std::vector<std::string> variant_conflicts(const nlohmann::json& variant,const std::string& base_text);
nlohmann::json rebase_variant(const nlohmann::json& variant,const std::string& base_text);
// Sets (or, when it equals the base's value, removes) a component-field patch of a variant prefab.
void set_variant_field(nlohmann::json& variant,const std::string& base_text,const std::string& member,const std::string& component,const std::string& field,const nlohmann::json& value);
// Sources that use a prefab: scenes that link it and prefabs that mount it or derive from it, by source-relative path.
struct PrefabConsumer {std::string source,kind;};
std::vector<PrefabConsumer> prefab_consumers(const std::filesystem::path& source_root,AssetId prefab);
}
