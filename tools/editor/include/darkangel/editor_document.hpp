#pragma once
#include <darkangel/assembly.hpp>
#include <darkangel/editor_service.hpp>
#include <filesystem>
#include <functional>
#include <optional>
namespace darkangel {
enum class EditScope {Placement,Definition};
struct PreparedDocumentEdit {std::uint64_t token,revision;std::string summary;};
struct DocumentHistoryHead {std::uint64_t token{};std::string summary,origin;};
class EditorDocument {
public:
    // linked: externally owned assembly sources (R4 prefabs) the scene mounts. They resolve with the scene but are never edited,
    // journaled or saved by it; the scene's manifest records only which ones it links.
    EditorDocument(AssetId root,StableId placement,AssemblySources,std::size_t history_limit=128,AssemblySources linked={});
    ~EditorDocument();
    World& world();
    const SpawnPlan& plan() const;
    // The scene's own assembly source: structural edits to its objects go here.
    AssetId root() const;
    // The scene placement every authored path starts with.
    StableId placement() const;
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
    // The current text of a linked source by AssetID; none when the project has no such source.
    using LinkSource=std::function<std::optional<std::string>(AssetId)>;
    // Opening resolves the listed links and, through the same source, the prefabs those prefabs mount (nested prefabs).
    static std::unique_ptr<EditorDocument> open(const std::filesystem::path&,const LinkSource& links={});
    // A link source over a project's source mount (R4): every .daprefab below `source_root`, by the AssetID in its source.
    // Tools without an AssetService (AgentHost, the cooker's collision bake) open linked scenes with it.
    static LinkSource project_links(const std::filesystem::path& source_root);
    // The project source mount a scene file belongs to: the nearest folder above it that holds a prefabs/ folder.
    static std::optional<std::filesystem::path> find_source_root(const std::filesystem::path& scene);
    // A linked prefab and every prefab it mounts or derives from (a variant's base), directly or nested, by AssetID. Throws when one is missing or cyclic.
    static AssemblySources link_closure(AssetId,const std::string& text,const LinkSource&);
    // Linked sources (R4): the published definitions this scene resolves against, and the ones its own sources mount.
    const AssemblySources& links() const;
    std::vector<AssetId> linked_references() const;
    // Adds or replaces linked definitions (for example after a prefab Save) and resolves the scene again. Owned sources,
    // history and dirty state are unchanged; prepared edits are invalidated. Throws, leaving the document unchanged, when
    // the scene no longer resolves against them.
    void link(const AssemblySources&);
    std::string source(AssetId) const;
    // One entity record of the scene's own source by its local identity, as JSON text; none when there is no such entity.
    std::optional<std::string> entity(const std::string& local) const;
    std::map<StableId,ScriptDefinition> scripts() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
