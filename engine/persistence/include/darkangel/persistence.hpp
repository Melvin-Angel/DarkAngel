#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace darkangel {
// Save slots (decision DAE-020). One SQLite file per slot holds snapshots of an opaque, versioned JSON payload; the
// service knows nothing of gameplay. A snapshot and the head pointer commit in one transaction; the previous snapshot is
// kept, so a damaged head recovers to it. Writes run on one worker per slot that only ever sees an immutable capture.
//
// Requests carry an operation identity and the revision the caller believes is current: a stale revision is refused and
// a repeated operation is answered once. Only Committed means saved.
enum class SaveOutcome:std::uint8_t{Captured,Writing,Committed,Failed};
const char* save_outcome_text(SaveOutcome);
struct SaveRequest {std::uint64_t operation{},expected_revision{};std::string reason,payload;};
struct SaveReceipt {std::uint64_t operation{},revision{};SaveOutcome outcome{};std::string error;};
// One stored snapshot. digest is SHA-256 of the payload; valid says it matched when read.
struct SaveSnapshot {std::uint64_t revision{};std::string reason,created,digest,payload;bool valid{};};
struct SaveSlotInfo {std::string slot,name;std::uint64_t head{};unsigned container{};std::vector<SaveSnapshot> snapshots;};
// What loading found: the head snapshot, or the previous one when the head is damaged (recovered says so).
struct LoadedSave {SaveSnapshot snapshot;bool recovered{};std::string warning;};

// A payload migration: the step from version-1 to version. Steps run in order on a copy; a failing step leaves the
// stored slot untouched and the load fails with its reason.
struct SaveMigration {unsigned version{};std::function<std::string(const std::string&)> step;};

class PersistenceService {
public:
    // Opens (or creates) the slot file. name labels a new slot.
    explicit PersistenceService(std::filesystem::path slot,std::string name="Save");
    ~PersistenceService();
    PersistenceService(const PersistenceService&)=delete;
    // Host only: the capture is copied and queued; the receipt says Captured. The worker writes it.
    SaveReceipt submit(SaveRequest);
    // The latest state of an operation submitted here.
    std::optional<SaveReceipt> receipt(std::uint64_t operation)const;
    // Blocks until every queued write has finished.
    void flush();
    // The committed head revision as the worker last saw it.
    std::uint64_t revision()const;
    // Fault injection for tests and gates: the next write fails inside its transaction, after the snapshot row.
    void fail_next_write();
    const std::filesystem::path& path()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Reading needs no service and writes nothing. An absent or empty slot is empty, not an error.
std::optional<LoadedSave> load_save(const std::filesystem::path& slot,std::span<const SaveMigration> migrations={},unsigned version=1);
SaveSlotInfo inspect_save(const std::filesystem::path& slot);
// The payload's "version" field (1 when absent); migrations bring it to the newest.
std::string migrate_save(std::string payload,std::span<const SaveMigration>,unsigned version);
// Makes a consistent copy of a slot through SQLite's backup API.
void backup_save(const std::filesystem::path& slot,const std::filesystem::path& copy);
}
