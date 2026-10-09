#pragma once
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <functional>
#include <darkangel/action.hpp>
#include <darkangel/ability_assets.hpp>
namespace darkangel::editor_app {
ActionDefinition authoring_action(const nlohmann::json&);
struct NativeDraft {AssetInfo asset;std::string saved; nlohmann::json value;bool dirty()const{return nlohmann::json::parse(saved)!=value;}};
class NativeAuthoring {
public:
    NativeDraft& open(AssetService&,AssetId);
    CookResult save(AssetService&,AssetId);
    AssetId duplicate(AssetService&,AssetId,std::string_view name,bool blank=false);
    void assign(AssetService&,AssetId kit,std::size_t slot,AssetId ability);
    void bind(AssetService&,AssetId kit,AssetId ability,unsigned block,AssetId effect,double power);
    unsigned add_action_block(AssetService&,AssetId,const ActionBlock&);
    AttributeId add_attribute(AssetService&,AssetId,std::string_view name,AttributeKind);
    void remove_attribute(AssetService&,AssetId,AttributeId);
    void remove_action_block(AssetService&,AssetId,unsigned block);
    void set_action_block_times(AssetService&,AssetId,unsigned block,unsigned begin,unsigned end,bool continuous=false);
    void cancel_edit(AssetService&,std::uint64_t expected_revision,std::string_view label);
    bool dirty()const;
    void save_all(AssetService&,std::span<const AssetId> scene_roots={});
    void apply(AssetService&,AssetId,std::uint64_t expected_revision,nlohmann::json,std::string_view label,bool continuous=false);
    void record_changes(AssetService&,std::string_view label,bool continuous=false);
    void undo(AssetService&);void redo(AssetService&);
    void reload(AssetService&,AssetId);void revert(AssetService&,AssetId);
    bool can_undo()const{return !undo_.empty();}bool can_redo()const{return !redo_.empty();}
    std::uint64_t revision()const{return revision_;}
    std::map<AssetId,NativeDraft> drafts;
private:
    using Values=std::map<AssetId,nlohmann::json>;
    struct Command {std::string label;Values before,after;};
    Values observed_;std::vector<Command> undo_,redo_;std::uint64_t revision_{};bool continuous_{};
    std::map<AssetId,unsigned> action_ids_;
    std::map<AssetId,AttributeId> attribute_ids_;
    void check_sources(AssetService&,const Values&)const;
    void travel(AssetService&,bool redo);
};
// Cook every selected scene root and its complete native closure into a candidate
// package. The caller validates runtime/GPU/session resources before switching.
void package_authoring(AssetService&,std::span<const AssetId>,const std::filesystem::path&);
}
