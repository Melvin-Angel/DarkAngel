#pragma once
#include <darkangel/assembly.hpp>
#include <darkangel/editor_service.hpp>
#include <filesystem>
#include <optional>
namespace darkangel {
enum class EditScope {Placement,Definition};
struct PreparedDocumentEdit {std::uint64_t token,revision;std::string summary;};
struct DocumentHistoryHead {std::uint64_t token{};std::string summary,origin;};
class EditorDocument {
public:
    EditorDocument(AssetId root,StableId placement,AssemblySources,std::size_t history_limit=128);
    ~EditorDocument();
    World& world();
    const SpawnPlan& plan() const;
    // The scene's own assembly source: structural edits to its objects go here.
    AssetId root() const;
    std::uint64_t revision() const;
    bool dirty() const;
    DocumentHistoryHead history_head() const;
    PreparedDocumentEdit prepare(std::uint64_t,std::span<const PropertyChange>,EditScope=EditScope::Placement);
    PreparedDocumentEdit prepare_source(std::uint64_t,AssetId,std::string_view); // structural/script edits use the same transaction
    // Adds, replaces or removes (no record) entities of the scene's own source (root()) in one transaction. Only the edited
    // objects are resolved again (SCENE_SCALE_PLAN.md phase 8); a source edit of the whole root resolves the whole scene.
    PreparedDocumentEdit prepare_entities(std::uint64_t,const std::map<std::string,std::optional<std::string>>&);
    void commit(PreparedDocumentEdit,std::string_view origin="UI");
    void discard(PreparedDocumentEdit);
    void undo(std::uint64_t);
    void redo(std::uint64_t);
    void save(const std::filesystem::path&); // one journaled bundle, atomic replacement
    static std::unique_ptr<EditorDocument> open(const std::filesystem::path&);
    std::string source(AssetId) const;
    // One entity record of the scene's own source by its local identity, as JSON text; none when there is no such entity.
    std::optional<std::string> entity(const std::string& local) const;
    std::map<StableId,ScriptDefinition> scripts() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
