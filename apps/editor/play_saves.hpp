#pragma once
#include <darkangel/persistence.hpp>
#include <darkangel/party_inventory.hpp>
#include <darkangel/world_ledger.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace darkangel::editor_app {
// What a save holds: the world ledger's save-scoped records, every party profile's inventory, and where the party is
// (scene and the entry or altar it arrived by). Versioned JSON; the persistence service stores it opaquely.
struct PlaySave {WorldLedger ledger;std::vector<InventorySnapshot> inventories;AssetId scene;StableId entry;};
constexpr unsigned play_save_version=1;
std::string encode_play_save(const WorldLedger&,const PartyInventory*,AssetId scene,StableId entry);
PlaySave decode_play_save(std::string_view);
// Ledger kinds this build reads; any other is kept verbatim as foreign.
std::span<const LedgerKind> play_ledger_kinds();

// The project's one save slot (approved for M8: one editor-selected slot per project) and Tools > Saves. Host only:
// guests never construct a capture. Saves happen at safe points: resting at an altar that saves, or Save now.
class PlaySaves {
public:
    explicit PlaySaves(std::filesystem::path slot);
    ~PlaySaves();
    const std::filesystem::path& slot()const{return slot_;}
    // Queues a save of an immutable capture. False with a reason when one is already being written.
    bool save(std::string payload,std::string reason);
    // Once per frame: reports finished writes to the log text.
    void poll();
    std::string status()const{return status_;}
    // Play from slot: when set, Play starts from the slot's head (or recovered previous) snapshot.
    bool play_from_slot{};
    std::optional<PlaySave> load(std::string& warning)const;
    // Tools > Saves. playing_host: Play is running on this host, so Save now is offered through save_now.
    void draw(bool playing_host,const std::function<void()>& save_now);
private:
    std::filesystem::path slot_;std::unique_ptr<PersistenceService> service_;std::uint64_t operation_{},pending_{};std::string status_;
    std::optional<SaveSlotInfo> inspected_;std::size_t selected_{};
    PersistenceService& service();
};
}
