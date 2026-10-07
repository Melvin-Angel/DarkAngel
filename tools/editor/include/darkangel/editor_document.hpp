#pragma once
#include <darkangel/assembly.hpp>
#include <darkangel/editor_service.hpp>
#include <filesystem>
namespace darkangel {
enum class EditScope {Placement,Definition};
struct PreparedDocumentEdit {std::uint64_t token,revision;std::string summary;};
class EditorDocument {
public:
    EditorDocument(AssetId root,StableId placement,AssemblySources,std::size_t history_limit=128);
    ~EditorDocument();
    World& world();
    const SpawnPlan& plan() const;
    std::uint64_t revision() const;
    bool dirty() const;
    PreparedDocumentEdit prepare(std::uint64_t,std::span<const PropertyChange>,EditScope=EditScope::Placement);
    PreparedDocumentEdit prepare_source(std::uint64_t,AssetId,std::string_view); // structural/script edits use the same transaction
    void commit(PreparedDocumentEdit);
    void discard(PreparedDocumentEdit);
    void undo(std::uint64_t);
    void redo(std::uint64_t);
    void save(const std::filesystem::path&); // one journaled bundle, atomic replacement
    static std::unique_ptr<EditorDocument> open(const std::filesystem::path&);
    std::string source(AssetId) const;
    std::map<StableId,ScriptDefinition> scripts() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
