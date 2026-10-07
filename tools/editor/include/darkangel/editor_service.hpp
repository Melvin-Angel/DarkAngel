#pragma once
#include <darkangel/world.hpp>
#include <memory>
#include <span>
namespace darkangel {
struct PropertyChange { StableId object; TypeId type; PropertyId property; double value; };
struct PreparedEdit {std::uint64_t token,revision;};
// M2 command spike. UI/agent clients will share this service and its transactions.
class EditorService {
public:
    explicit EditorService(World&,std::size_t history_limit=128);
    ~EditorService();
    EditorService(const EditorService&)=delete;
    EditorService& operator=(const EditorService&)=delete;
    std::uint64_t revision() const;
    PreparedEdit prepare(std::uint64_t expected_revision,std::span<const PropertyChange>);
    void commit(PreparedEdit);
    void discard(PreparedEdit);
    void undo(std::uint64_t expected_revision);
    void redo(std::uint64_t expected_revision);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
