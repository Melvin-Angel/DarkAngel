#pragma once
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <array>
#include <functional>
#include <darkangel/action.hpp>
#include <darkangel/ability_assets.hpp>
namespace darkangel::editor_app {
ActionDefinition authoring_action(const nlohmann::json&);
struct NativeDraft {AssetInfo asset;std::string saved; nlohmann::json value;bool pending{};bool dirty()const{return pending||nlohmann::json::parse(saved)!=value;}};
struct NativeSourceComparison {AssetId asset;std::string path,baseline_sha,disk_sha;std::uint64_t revision{};bool pending{},exists{},matches{};};
struct NativeValidationSummary {std::uint64_t revision{};std::size_t sources{};AssetId selected;};
struct NativeHistoryEntry {std::string label;std::vector<AssetId> assets;bool creation{};};
struct NativeHistoryChange {AssetId asset;std::string operation,path,value;};
class NativeAuthoring {
public:
    NativeDraft& open(AssetService&,AssetId);
    NativeSourceComparison compare_source(AssetService&,AssetId);
    std::vector<AssetInfo> inventory(AssetService&)const;
    CookResult save(AssetService&,AssetId);
    AssetId create_character(AssetService&,AssetId skin,std::string_view name);
    AssetId create_player(AssetService&,AssetId character,AssetId kit,std::string_view name);
    AssetId duplicate(AssetService&,AssetId,std::string_view name,bool blank=false);
    AssetId duplicate_ability_with_action(AssetService&,AssetId,std::string_view name,bool blank=false);
    // Copies a Composer action as a presentation-only reaction (cue blocks only,
    // full-body, one loop, root policy none) and binds it to the effect.
    AssetId create_reaction_action(AssetService&,AssetId effect,AssetId template_action,std::string_view name);
    void bind_effect_reaction(AssetService&,AssetId effect,AssetId action);
    // Presentation-only copy of a Composer timeline, not yet referenced by anything.
    AssetId create_presentation_action(AssetService&,AssetId template_action,std::string_view name);
    // Optional kit death timeline; an empty action clears it.
    void bind_kit_death(AssetService&,AssetId kit,AssetId action);
    void assign_player_kit(AssetService&,AssetId player,AssetId kit);
    void assign(AssetService&,AssetId kit,std::size_t slot,AssetId ability);
    void bind(AssetService&,AssetId kit,AssetId ability,unsigned block,AssetId effect,double power);
    unsigned add_graph_blend(AssetService&,AssetId,bool two_dimensional,unsigned first,unsigned second,unsigned third=0);
    unsigned add_graph_tag_select(AssetService&,AssetId,AssetId registry,TagId,unsigned inactive,unsigned active);
    unsigned duplicate_graph_node(AssetService&,AssetId,unsigned node);
    void connect_graph_input(AssetService&,AssetId,unsigned node,unsigned point,unsigned input);
    void remove_graph_node(AssetService&,AssetId,unsigned node);
    void add_graph_point(AssetService&,AssetId,unsigned node,unsigned input,double x,double y);
    void remove_graph_point(AssetService&,AssetId,unsigned node,unsigned point);
    void add_graph_triangle(AssetService&,AssetId,unsigned node,std::array<unsigned,3>);
    void remove_graph_triangle(AssetService&,AssetId,unsigned node,unsigned triangle);
    unsigned add_action_block(AssetService&,AssetId,const ActionBlock&);
    void add_effect_modifier(AssetService&,AssetId,AttributeId);
    AttributeId add_attribute(AssetService&,AssetId,std::string_view name,AttributeKind);
    void remove_attribute(AssetService&,AssetId,AttributeId);
    void remove_action_block(AssetService&,AssetId,unsigned block);
    void set_action_block_times(AssetService&,AssetId,unsigned block,unsigned begin,unsigned end,bool continuous=false);
    void cancel_edit(AssetService&,std::uint64_t expected_revision,std::string_view label);
    bool dirty()const;
    NativeValidationSummary validate(AssetService&,AssetId selected={},std::span<const AssetId> scene_roots={});
    void save_all(AssetService&,std::span<const AssetId> scene_roots={});
    void apply(AssetService&,AssetId,std::uint64_t expected_revision,nlohmann::json,std::string_view label,bool continuous=false);
    void record_changes(AssetService&,std::string_view label,bool continuous=false);
    void undo(AssetService&);void redo(AssetService&);
    void close(AssetService&,AssetId);
    void reload(AssetService&,AssetId);void revert(AssetService&,AssetId);
    std::vector<NativeHistoryEntry> history(bool redo=false)const;
    std::vector<NativeHistoryChange> history_changes(bool redo,std::size_t newest_index)const;
    bool can_undo()const{return !undo_.empty();}bool can_redo()const{return !redo_.empty();}
    std::uint64_t revision()const{return revision_;}
    std::map<AssetId,NativeDraft> drafts;
private:
    using Values=std::map<AssetId,nlohmann::json>;
    struct Command {std::string label;Values before,after;std::map<AssetId,NativeDraft> creations;};
    Values observed_;std::vector<Command> undo_,redo_;std::uint64_t revision_{};bool continuous_{};
    std::map<AssetId,unsigned> graph_ids_;
    std::map<AssetId,unsigned> action_ids_;
    std::map<AssetId,AttributeId> attribute_ids_;
    AssetId stage_creation(AssetService&,std::string,nlohmann::json);
    void trim_history();
    void check_sources(AssetService&,const Values&)const;
    void travel(AssetService&,bool redo);
};
// Cook every selected scene root and its complete native closure into a candidate
// package. The caller validates runtime/GPU/session resources before switching.
void package_authoring(AssetService&,std::span<const AssetId>,const std::filesystem::path&);
}
