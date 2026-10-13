#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace darkangel {
// The world ledger: persistent progression as plain values under stable authored identities. It knows nothing about
// scenes being loaded, sessions or networking; the host owns one and it outlives every scene session. Gameplay writes a
// value in the tick it changes; a scene's objects read theirs back when the scene is built again. A property that is
// not in the ledger means "as authored".
//
// A persistent identity is the scene asset and the object's stable identity in it. Party-level facts (the checkpoint,
// inventories later) use a nil scene. Runtime entity and network identities never appear here.
struct PersistentId {AssetId scene;StableId object;auto operator<=>(const PersistentId&)const=default;};
// Where a property lives: for this Play only, or in the save file as well.
enum class LedgerScope:std::uint8_t{Session,Save};
// What takes a property back to "as authored". A property can name several, or none (it then stays for good).
enum class LedgerReset:std::uint8_t{SceneEntry=1,Rest=2,PartyWipe=4};
constexpr std::uint8_t operator|(LedgerReset a,LedgerReset b){return std::uint8_t(std::uint8_t(a)|std::uint8_t(b));}
constexpr std::uint8_t operator|(std::uint8_t a,LedgerReset b){return std::uint8_t(a|std::uint8_t(b));}
// The policy travels with the value, so a reset can run over the records of a scene that is not loaded.
struct LedgerPolicy {LedgerScope scope{LedgerScope::Save};std::uint8_t resets{};
    constexpr LedgerPolicy()=default;constexpr LedgerPolicy(LedgerScope where,std::uint8_t when=0):scope(where),resets(when){}constexpr LedgerPolicy(LedgerScope where,LedgerReset when):scope(where),resets(std::uint8_t(when)){}
    constexpr bool on(LedgerReset trigger)const{return (resets&std::uint8_t(trigger))!=0;}
    bool operator==(const LedgerPolicy&)const=default;};
struct LedgerTransform {double x{},y{},z{},yaw{};bool operator==(const LedgerTransform&)const=default;};
using LedgerValue=std::variant<bool,std::int64_t,double,std::string,LedgerTransform>;
struct LedgerProperty {LedgerValue value;LedgerPolicy policy;bool operator==(const LedgerProperty&)const=default;};
// One object's logical state. kind and schema say who wrote it and in which shape. foreign holds, verbatim, a record
// this build does not understand (an unknown kind or a newer schema): it is kept, reported and never rewritten.
struct LedgerRecord {PersistentId id;std::string kind;unsigned schema{1};std::map<std::string,LedgerProperty,std::less<>> properties;std::string foreign;};
// Bounds. Names (kinds and properties) are lowercase letters, digits and '_', 1 to 32 characters.
constexpr std::size_t ledger_record_limit=65536,ledger_property_limit=32,ledger_text_limit=256,ledger_document_limit=64*1024*1024;
bool ledger_name_valid(std::string_view);
class WorldLedger;
// What a persistent object writes when its state is captured, and reads when it is restored. Only named logical
// properties: no poses, no positions in mid-movement, no runtime identities.
class LedgerWriter {
public:
    void set(std::string_view name,LedgerValue value,LedgerPolicy policy);
    // Back to "as authored".
    void clear(std::string_view name);
private:friend class WorldLedger;LedgerWriter(WorldLedger& ledger,PersistentId id,std::string_view kind,unsigned schema):ledger_(ledger),id_(id),kind_(kind),schema_(schema){}
    WorldLedger& ledger_;PersistentId id_;std::string kind_;unsigned schema_{};
};
class LedgerReader {
public:
    const LedgerValue* find(std::string_view name)const;
    // Typed reads; empty when the property is absent or holds another type.
    std::optional<bool> flag(std::string_view name)const;
    std::optional<std::int64_t> integer(std::string_view name)const;
    std::optional<double> real(std::string_view name)const;
    const std::string* text(std::string_view name)const;
    std::optional<LedgerTransform> transform(std::string_view name)const;
private:friend class WorldLedger;explicit LedgerReader(const LedgerRecord* record):record_(record){}
    const LedgerRecord* record_{};
};
// The one mechanism by which a world object saves and restores its logical state. There is no per-type persistence path.
class PersistentObject {
public:
    virtual ~PersistentObject()=default;
    virtual std::string_view kind()const=0;
    virtual unsigned schema()const{return 1;}
    virtual void capture(LedgerWriter&)const=0;
    // Called with whatever the ledger holds, which may be nothing: an absent property means "as authored".
    virtual void restore(const LedgerReader&)=0;
};
// A kind this build can read, and the newest schema of it that it can.
struct LedgerKind {std::string name;unsigned schema{1};};
class WorldLedger {
public:
    // Version of the serialized container. A newer one is refused whole.
    static constexpr unsigned version=1;
    // Writes one property. The record takes the kind and schema given; a record held as foreign is never written.
    void set(PersistentId,std::string_view kind,unsigned schema,std::string_view property,LedgerValue,LedgerPolicy);
    // Removes one property, or a whole record. False when there was nothing to remove.
    bool erase(PersistentId,std::string_view property);
    bool erase(PersistentId);
    const LedgerRecord* find(PersistentId)const;
    const LedgerValue* value(PersistentId,std::string_view property)const;
    // An object's state into its record, and back. capture replaces what the object's kind wrote before; restore
    // returns false when the record is foreign and the object was left as authored.
    void capture(PersistentId,const PersistentObject&);
    bool restore(PersistentId,PersistentObject&)const;
    // Removes every property whose policy names the trigger and returns the records that changed, in the order the
    // reset ran: scene, kind, object, property name. With a scene, only that scene's records are touched (a scene
    // entry always names one). Running the same reset again changes nothing.
    std::vector<PersistentId> reset(LedgerReset trigger,std::optional<AssetId> scene={});
    const std::map<PersistentId,LedgerRecord>& records()const{return records_;}
    // Records kept verbatim because this build does not understand them.
    std::vector<PersistentId> foreign()const;
    // Counts every change, for owners that mirror or save it.
    std::uint64_t revision()const{return revision_;}
    // Versioned JSON, records sorted by identity. Session-scoped properties are left out unless asked for.
    std::string serialize(bool with_session=false)const;
    // Refuses a newer container and anything malformed. A record of a kind not in known, or of a newer schema than
    // known names, is kept as foreign.
    static WorldLedger deserialize(std::string_view,std::span<const LedgerKind> known);
private:
    std::map<PersistentId,LedgerRecord> records_;std::uint64_t revision_{};
};
}
