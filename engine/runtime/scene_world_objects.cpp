#include "scene_world_objects.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel::scene_detail {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
// Mover boxes take identities far above authored collision, one per mover in authored order.
constexpr std::uint64_t mover_box_base=1ULL<<40;
// Pickup "collected" and mover "unlocked"/"open" through the one persistence mechanism.
struct PickupState final:PersistentObject {bool collected{};LedgerPolicy policy;
    std::string_view kind()const override{return "pickup";}
    void capture(LedgerWriter& out)const override{if(collected)out.set("collected",true,policy);else out.clear("collected");}
    void restore(const LedgerReader& in)override{collected=in.flag("collected").value_or(false);}};
struct MoverRecord final:PersistentObject {const MoverDefinition& definition;bool unlocked{},open{},has_unlocked{},has_open{};explicit MoverRecord(const MoverDefinition& d):definition(d){}
    std::string_view kind()const override{return "mover";}
    void capture(LedgerWriter& out)const override{
        // Only what differs from the authored state is recorded; a reset back to authored clears the property.
        if(definition.locked&&unlocked)out.set("unlocked",true,definition.unlocked);else out.clear("unlocked");
        if(open!=definition.open)out.set("open",open,definition.opened);else out.clear("open");}
    void restore(const LedgerReader& in)override{if(auto value=in.flag("unlocked")){unlocked=*value;has_unlocked=true;}if(auto value=in.flag("open")){open=*value;has_open=true;}}};
std::array<double,4> yaw_quaternion(double yaw){return {0,std::sin(yaw*.5),0,std::cos(yaw*.5)};}
}

SceneWorldObjects::SceneWorldObjects(std::shared_ptr<const CharacterSceneCombat> combat,PhysicsWorld& physics,WorldLedger* ledger,PartyInventory* inventory)
    :combat_(std::move(combat)),physics_(physics),ledger_(ledger),inventory_(inventory){
    require(combat_->pickups.size()<=64&&combat_->altars.size()<=16&&combat_->movers.size()<=16,"Scene world object limits");
    collected_.assign(combat_->pickups.size(),false);
    for(std::size_t index=0;index<combat_->pickups.size();++index){const auto& pickup=combat_->pickups[index];
        if(ledger_){PickupState state;state.policy=pickup.definition.collected;ledger_->restore(persistent(pickup.id),state);collected_[index]=state.collected;}}
    for(std::size_t index=0;index<combat_->movers.size();++index){const auto& config=combat_->movers[index];Mover mover{&config,MoverState(config.definition),mover_box_base+index};
        if(ledger_){MoverRecord record(config.definition);ledger_->restore(persistent(config.id),record);mover.state.restore(record.has_unlocked?record.unlocked:!config.definition.locked,record.has_open?record.open:config.definition.open);}
        // The box stands at whichever end its logical state says; it is never saved between them.
        const auto at=mover.state.position(config.start);CollisionBox box;box.id=mover.box;box.center=at;box.half=config.definition.half;box.moving=true;box.rotation=yaw_quaternion(config.yaw);
        physics_.add(box);mover.center=at;movers_.push_back(std::move(mover));}
}
void SceneWorldObjects::record_pickup(std::size_t index){if(!ledger_)return;PickupState state;state.collected=collected_[index];state.policy=combat_->pickups[index].definition.collected;ledger_->capture(persistent(combat_->pickups[index].id),state);}
void SceneWorldObjects::record_mover(std::size_t index){if(!ledger_)return;const auto& mover=movers_[index];MoverRecord record(mover.config->definition);record.unlocked=mover.state.unlocked();record.open=mover.state.open();ledger_->capture(persistent(mover.config->id),record);}
void SceneWorldObjects::capture(){for(std::size_t index=0;index<collected_.size();++index)record_pickup(index);for(std::size_t index=0;index<movers_.size();++index)record_mover(index);}

bool SceneWorldObjects::met(const Who& who,const Requirement& requirement,const Context& context)const{
    if(requirement.item!=AssetId{}){if(!inventory_||!inventory_->contains(who.profile)||inventory_->quantity(who.profile,requirement.item)<requirement.quantity)return false;}
    if(!requirement.tag.empty()&&!context.world.tag(requirement.tag))return false;
    if(requirement.encounter&&(!context.encounter_complete||!context.encounter_complete(requirement.encounter)))return false;
    return true;
}

std::optional<InteractionResult> SceneWorldObjects::interact(const Who& who,StableId object,const Context& context,std::optional<SceneTransitionRequest>& travel){
    for(std::size_t index=0;index<combat_->pickups.size();++index){const auto& pickup=combat_->pickups[index];if(pickup.id!=object)continue;
        if(collected_[index])return InteractionResult::UsedUp;if(!who.living)return InteractionResult::Dead;if(!reaches(who.position,pickup.position,pickup.definition.radius))return InteractionResult::OutOfReach;
        // The grant and the collected state change together in this tick; a grant that does not fit leaves it available.
        if(!inventory_||!inventory_->contains(who.profile))return InteractionResult::Unavailable;
        if(inventory_->grant(who.profile,pickup.definition.item,pickup.definition.quantity).failure!=InventoryFailure::None)return InteractionResult::Unavailable;
        collected_[index]=true;record_pickup(index);++revision_;return InteractionResult::Accepted;}
    for(const auto& altar:combat_->altars){if(altar.id!=object)continue;
        if(!who.living)return InteractionResult::Dead;if(!reaches(who.position,altar.position,altar.definition.radius))return InteractionResult::OutOfReach;
        if(ledger_&&!altar_discovered(*ledger_,combat_->scene,altar.id)){record_altar(*ledger_,combat_->scene,altar.id,altar.definition);if(altar.definition.checkpoint==AltarCheckpoint::Discover)write_checkpoint(*ledger_,{combat_->scene,altar.id,altar.definition.name});++revision_;}
        return InteractionResult::Accepted;}
    for(std::size_t index=0;index<movers_.size();++index){auto& mover=movers_[index];const auto& config=*mover.config;if(config.id!=object)continue;
        if(config.definition.activation!=MoverActivation::Interact)return InteractionResult::Unavailable;if(!who.living)return InteractionResult::Dead;
        // Reach is measured from the box as it stands now.
        if(!reaches(who.position,mover.center,config.definition.radius+std::max({config.definition.half.x,config.definition.half.z})))return InteractionResult::OutOfReach;
        if(!mover.state.unlocked()){const auto& requirement=config.definition.requirement;if(!met(who,requirement,context))return InteractionResult::Unavailable;
            // Consuming the key and unlocking are one change: a take that fails leaves the mover locked.
            if(requirement.item!=AssetId{}&&requirement.consume&&inventory_->take(who.profile,requirement.item,requirement.quantity).failure!=InventoryFailure::None)return InteractionResult::Unavailable;
            mover.state.unlock();}
        else if(!config.definition.requirement.empty()&&!config.definition.locked&&!met(who,config.definition.requirement,context))return InteractionResult::Unavailable;
        const bool moved=mover.state.activate();record_mover(index);++revision_;
        if(config.definition.travel&&mover.state.open()&&!travel)travel=SceneTransitionRequest{config.id,config.definition.travel->first,config.definition.travel->second};
        return moved||config.definition.travel?InteractionResult::Accepted:InteractionResult::Unavailable;}
    return std::nullopt;
}

InteractionResult SceneWorldObjects::request(const Who& who,const WorldRequest& asked,std::optional<SceneTransitionRequest>& travel){
    auto altar=std::find_if(combat_->altars.begin(),combat_->altars.end(),[&](const auto& a){return a.id==asked.altar;});
    if(altar==combat_->altars.end())return InteractionResult::Unknown;if(!who.living)return InteractionResult::Dead;
    // The menu stays open a little beyond the altar's reach.
    if(!reaches(who.position,altar->position,altar->definition.radius+2))return InteractionResult::OutOfReach;
    if(travel||!ledger_||!altar_discovered(*ledger_,combat_->scene,altar->id))return InteractionResult::Unavailable;
    if(asked.kind==WorldRequestKind::Rest){if(!altar->definition.rest)return InteractionResult::Unavailable;
        if(altar->definition.checkpoint!=AltarCheckpoint::Never)write_checkpoint(*ledger_,{combat_->scene,altar->id,altar->definition.name});
        SceneTransitionRequest rest{altar->id,combat_->scene,altar->id};rest.reset=std::uint8_t(LedgerReset::Rest);rest.save=altar->definition.save;travel=rest;++revision_;return InteractionResult::Accepted;}
    // Fast travel: the host chooses, from an altar that offers it, to a discovered destination. The checkpoint stays.
    if(!who.host||!altar->definition.fast_travel)return InteractionResult::Unavailable;
    const auto known=fast_travel_destinations(*ledger_);if(std::none_of(known.begin(),known.end(),[&](const auto& d){return d.scene==asked.destination.scene&&d.altar==asked.destination.altar;}))return InteractionResult::Unavailable;
    travel=SceneTransitionRequest{altar->id,asked.destination.scene,asked.destination.altar};return InteractionResult::Accepted;
}

void SceneWorldObjects::step(double seconds,const WorldEvents& world){
    for(std::size_t index=0;index<movers_.size();++index){auto& mover=movers_[index];const auto& definition=mover.config->definition;
        if(definition.activation==MoverActivation::Event&&world.happened(definition.event)&&mover.state.activate()){record_mover(index);++revision_;}
        const bool was=mover.state.open(),moving=mover.state.moving();const auto velocity=mover.state.step(seconds);
        if(mover.state.open()!=was){record_mover(index);++revision_;}
        if(moving||velocity.x!=0||velocity.y!=0||velocity.z!=0){physics_.set_platform(mover.box,velocity);mover.center=mover.state.position(mover.config->start);}
    }
}
void SceneWorldObjects::view(WorldStateView& out)const{
    for(std::size_t index=0;index<collected_.size();++index)if(collected_[index])out.collected.push_back(combat_->pickups[index].id);
    for(const auto& mover:movers_){if(mover.state.unlocked())out.unlocked.push_back(mover.config->id);if(mover.state.open())out.open.push_back(mover.config->id);}
    if(ledger_){for(const auto& altar:combat_->altars)if(altar_discovered(*ledger_,combat_->scene,altar.id))out.discovered.push_back(altar.id);out.destinations=fast_travel_destinations(*ledger_);out.checkpoint=read_checkpoint(*ledger_);}
}
SceneTransitionRequest SceneWorldObjects::respawn()const{
    SceneTransitionRequest result;result.reset=std::uint8_t(LedgerReset::PartyWipe);
    if(ledger_)if(const auto checkpoint=read_checkpoint(*ledger_)){result.scene=checkpoint->scene;result.entry=checkpoint->altar;}
    return result;
}
std::vector<std::pair<StableId,MotorVec>> SceneWorldObjects::movers()const{std::vector<std::pair<StableId,MotorVec>> result;for(const auto& mover:movers_)result.emplace_back(mover.config->id,mover.center);return result;}
}
