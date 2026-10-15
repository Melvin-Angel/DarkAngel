#include <darkangel/scene_transition.hpp>
#include <darkangel/character_scene.hpp>
#include <algorithm>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
const char* transition_phase_text(TransitionPhase phase){
    switch(phase){case TransitionPhase::Idle:return "Idle";case TransitionPhase::Requested:return "Requested";case TransitionPhase::Preparing:return "Preparing";case TransitionPhase::Validating:return "Validating";case TransitionPhase::Capturing:return "Capturing";case TransitionPhase::Swapping:return "Swapping";case TransitionPhase::Placing:return "Placing";case TransitionPhase::AwaitingReady:return "Awaiting ready";case TransitionPhase::Resumed:return "Resumed";}
    return "Unknown";
}
struct SceneTransitionCoordinator::Impl {
    SceneTransitionCoordinator& owner;SceneProvider& provider;WorldLedger& ledger;std::vector<StableId> companions;
    // The running scene, and the entry the party last came in by (nil: the scene's own start).
    std::unique_ptr<PartyInventory> inventory;StableId local_profile;std::unique_ptr<PreparedScene> source,current;StableId source_entry;bool swap_started{},rollback{},initial{};std::string swap_failure;std::unique_ptr<CharacterSceneSession> session;StableId arrived_by;
    AssetId origin_scene;StableId origin_entry;std::vector<InventorySnapshot> restored;
    TransitionPhase phase{};TransitionRequest asked;std::unique_ptr<PreparedScene> pending;double elapsed{},progress{};std::string text;
    std::vector<TransitionPhase> log;std::optional<TransitionOutcome> outcome;bool fatal{},fail_swap{},loading{};
    Impl(SceneTransitionCoordinator& self,SceneProvider& source,WorldLedger& world,std::vector<StableId> others):owner(self),provider(source),ledger(world),companions(std::move(others)){require(companions.size()<scene_party_limit,"A party is at most four players");}
    void enter(TransitionPhase next){phase=next;log.push_back(next);text=transition_phase_text(next);}
    // The destination's content is tied to its scene asset, so its objects' persistent identities are.
    static void adopt(PreparedScene& scene){require(scene.scene!=AssetId{}&&bool(scene.player)&&!scene.objects.empty(),"A prepared scene names its asset, its player object and its objects");if(scene.combat)scene.combat->scene=scene.scene;}
    const SceneEntry* entry(const PreparedScene& scene,StableId id)const{for(const auto& known:scene.entries)if(known.id==id)return &known;return nullptr;}
    std::unique_ptr<CharacterSceneSession> build(PreparedScene& scene,StableId by){
        SceneStart start;start.ledger=&ledger;
        if(by){const auto* at=entry(scene,by);require(at!=nullptr,"The scene has no such entry point");start.arrival=SceneArrival{at->position,at->yaw};}
        // Without an entry point the first member stands where the scene's player object is; companions form up around it.
        else if(!companions.empty())for(const auto& object:scene.objects)if(object.id==scene.player)start.arrival=SceneArrival{{object.transform.x,object.transform.y,object.transform.z},object.transform.yaw};
        std::vector<ScenePlayer> others;for(const auto id:companions)others.push_back({id,{},0});
        // What resets on entering a scene is gone before its objects read the ledger.
        ledger.reset(LedgerReset::SceneEntry,scene.scene);
        if(!owner.build_session&&scene.combat&&scene.combat->inventory){
            if(!inventory){inventory=std::make_unique<PartyInventory>(scene.combat->inventory->catalog);local_profile=owner.inventory_profile?owner.inventory_profile:scene.player;if(!restored.empty()){inventory->restore(restored);restored.clear();}}
            start.inventory=inventory.get();start.profiles[scene.player]=local_profile;for(auto id:companions)start.profiles[id]=id;
        }
        if(owner.build_session){auto session=owner.build_session(scene,by,start,others);require(!session||session->hosting(),"Scene transition construction requires a host");return session;}
        return std::make_unique<CharacterSceneSession>(scene.hello,scene.objects,scene.player,scene.collision,scene.plan,scene.rig,scene.rig_archive,scene.combat,others,start);
    }
    void finish(TransitionOutcome result){source.reset();swap_started=rollback=initial=false;swap_failure.clear();outcome=std::move(result);pending.reset();loading=false;phase=TransitionPhase::Idle;progress=0;text=outcome->arrived?std::string():outcome->failure;}
    // A failure before the swap: the source session never stopped. Input returns and the reason is reported.
    void cancel(std::string reason){if(loading)provider.cancel();if(owner.cancelled)owner.cancelled(reason);finish({false,asked.scene,asked.entry,std::move(reason),false});}
    void swap(){
        if(!swap_started){if(owner.unloading)owner.unloading();session.reset();source=std::move(current);source_entry=arrived_by;swap_started=true;}
        try{
            if(rollback&&session){if(owner.ready&&!owner.ready(*session))return;if(owner.resumed)owner.resumed();const auto reason=swap_failure;finish({false,asked.scene,asked.entry,"The destination could not be entered ("+reason+"); the party is back in the scene it left",true});return;}
            if(fail_swap&&!rollback){fail_swap=false;throw std::runtime_error("Injected failure while building the destination");}
            auto& target=rollback?source:pending;require(bool(target),"Scene construction has no prepared scene");
            session=build(*target,rollback?source_entry:asked.entry);
            if(!session){text=owner.construction_text?owner.construction_text():"Waiting for players";return;}
            if(rollback){current=std::move(source);arrived_by=source_entry;if(owner.loaded)owner.loaded();
                if(owner.ready&&!owner.ready(*session))return;if(owner.resumed)owner.resumed();const auto reason=swap_failure;finish({false,asked.scene,asked.entry,"The destination could not be entered ("+reason+"); the party is back in the scene it left",true});return;}
            current=std::move(pending);arrived_by=asked.entry;if(owner.loaded)owner.loaded();enter(TransitionPhase::Placing);
        }catch(const std::exception& error){
            const auto reason=std::string(error.what());session.reset();current.reset();pending.reset();
            if(rollback||initial||!source){fatal=true;const auto first=swap_failure;finish({false,asked.scene,asked.entry,first.empty()?"Play could not start: "+reason:"The destination could not be entered ("+first+") and the scene the party left could not be built again ("+reason+"); Play cannot continue",false});return;}
            swap_failure=reason;rollback=true;progress=0;text="Rebuilding the source scene";
        }
    }
    void update(double seconds){
        if(phase==TransitionPhase::Idle){if(session)if(const auto raised=session->take_transition())owner.request({raised->scene==AssetId{}?origin_scene:raised->scene,raised->scene==AssetId{}?origin_entry:raised->entry,raised->reset?"party respawn or rest at "+raised->source.text():"scene transition trigger "+raised->source.text(),raised->reset,raised->save});return;}
        elapsed+=seconds;
        switch(phase){
        case TransitionPhase::Requested:
            try{provider.begin(asked.scene);loading=true;enter(TransitionPhase::Preparing);}catch(const std::exception& error){cancel(std::string("The destination scene could not be loaded: ")+error.what());}
            break;
        case TransitionPhase::Preparing:{
            SceneLoadStatus load;try{load=provider.poll();}catch(const std::exception& error){load={SceneLoad::Failed,0,error.what()};}
            progress=std::clamp(load.progress,0.,1.);if(!load.text.empty())text=load.text;
            if(load.state==SceneLoad::Failed){cancel("The destination scene could not be loaded: "+load.text);break;}
            if(load.state==SceneLoad::Ready){try{pending=provider.take();loading=false;require(pending!=nullptr,"The provider handed over no scene");adopt(*pending);enter(TransitionPhase::Validating);}catch(const std::exception& error){loading=false;cancel(std::string("The destination scene could not be loaded: ")+error.what());}}
            break;}
        case TransitionPhase::Validating:
            if(pending->scene!=asked.scene)cancel("The provider prepared another scene than the one asked for");
            else if(asked.entry&&!entry(*pending,asked.entry))cancel("The destination scene has no entry point "+asked.entry.text());
            else if(std::none_of(pending->objects.begin(),pending->objects.end(),[&](const ObjectData& object){return object.id==pending->player;}))cancel("The destination scene has no player start");
            else if((owner.party_size?owner.party_size():companions.size()+1)>1&&!pending->combat)cancel("The destination scene cannot hold a party of several players");
            else if(pending->combat&&scene_actor_demand(*pending->combat)+(owner.party_size?owner.party_size():companions.size()+1)>session_character_capacity)cancel("The party does not fit the destination scene's character capacity");
            else enter(TransitionPhase::Capturing);
            break;
        case TransitionPhase::Capturing:
            // A rest or a wipe resets every scene's records after the source has been captured, in a fixed trigger order.
            try{session->capture();for(const auto trigger:{LedgerReset::Rest,LedgerReset::PartyWipe})if(asked.reset&std::uint8_t(trigger))ledger.reset(trigger);enter(TransitionPhase::Swapping);}catch(const std::exception& error){cancel(std::string("The scene's state could not be captured: ")+error.what());}
            break;
        case TransitionPhase::Swapping:swap();break;
        case TransitionPhase::Placing:enter(TransitionPhase::AwaitingReady);break;
        case TransitionPhase::AwaitingReady:{
            // In-process clients are bootstrapped by the session's construction. A client in another process acknowledges here.
            try{bool ready=session!=nullptr;if(ready&&owner.ready)ready=owner.ready(*session);else if(ready)for(const auto player:session->players())ready&=session->client(player)!=nullptr;
                if(ready&&elapsed>=owner.minimum_seconds)enter(TransitionPhase::Resumed);
            }catch(const std::exception& error){
                if(owner.unloading)owner.unloading();session.reset();current.reset();
                if(initial||!source){fatal=true;finish({false,asked.scene,asked.entry,std::string("The scene could not become ready: ")+error.what(),false});}
                else{swap_failure=error.what();rollback=true;enter(TransitionPhase::Swapping);text="Rebuilding the source scene";}
            }
            break;}
        case TransitionPhase::Resumed:if(owner.resumed)owner.resumed();finish({true,asked.scene,asked.entry,{},false,asked.reset,asked.save});break;
        default:break;}
    }
};
SceneTransitionCoordinator::SceneTransitionCoordinator(SceneProvider& provider,WorldLedger& ledger,std::vector<StableId> companions):impl_(std::make_unique<Impl>(*this,provider,ledger,std::move(companions))){}
SceneTransitionCoordinator::~SceneTransitionCoordinator(){if(impl_->loading)impl_->provider.cancel();}
void SceneTransitionCoordinator::start(std::unique_ptr<PreparedScene> scene,StableId entry){auto& p=*impl_;require(!p.session&&p.phase==TransitionPhase::Idle&&scene!=nullptr,"Play starts once, with a prepared scene");Impl::adopt(*scene);p.fatal=false;p.outcome.reset();p.log.clear();p.arrived_by=entry;p.origin_scene=scene->scene;p.origin_entry=entry;p.session=p.build(*scene,entry);
    if(!p.session){require(bool(build_session),"Scene builder returned no session");p.asked={scene->scene,entry,"Play"};p.pending=std::move(scene);p.initial=p.swap_started=true;p.elapsed=p.progress=0;p.enter(TransitionPhase::Swapping);return;}
    p.current=std::move(scene);if(build_session){p.asked={p.current->scene,entry,"Play"};p.initial=true;p.elapsed=p.progress=0;p.enter(TransitionPhase::Placing);}}

void SceneTransitionCoordinator::stop(){auto& p=*impl_;if(p.loading)p.provider.cancel();p.loading=false;p.pending.reset();p.phase=TransitionPhase::Idle;if(p.session){p.session->capture();if(unloading)unloading();}p.session.reset();p.current.reset();p.source.reset();p.swap_started=p.rollback=p.initial=false;}
CharacterSceneSession* SceneTransitionCoordinator::session()const{return impl_->session.get();}
const PreparedScene* SceneTransitionCoordinator::scene()const{return impl_->current?impl_->current.get():impl_->source?impl_->source.get():impl_->pending.get();}
std::pair<AssetId,StableId> SceneTransitionCoordinator::origin()const{return {impl_->origin_scene,impl_->origin_entry};}
PartyInventory* SceneTransitionCoordinator::inventory()const{return impl_->inventory.get();}
void SceneTransitionCoordinator::restore_inventory(std::vector<InventorySnapshot> saved){auto& p=*impl_;require(!p.session&&!p.inventory,"Saved inventories are restored before Play starts");p.restored=std::move(saved);}
bool SceneTransitionCoordinator::request(TransitionRequest asked){auto& p=*impl_;if(p.phase!=TransitionPhase::Idle||!p.session||asked.scene==AssetId{})return false;p.asked=std::move(asked);p.log.clear();p.outcome.reset();p.elapsed=0;p.progress=0;p.enter(TransitionPhase::Requested);return true;}
void SceneTransitionCoordinator::update(double seconds){require(seconds>=0&&seconds<=10,"Transition clock delta bounds");impl_->update(seconds);}
bool SceneTransitionCoordinator::busy()const{return impl_->phase!=TransitionPhase::Idle;}
TransitionStatus SceneTransitionCoordinator::status()const{return {impl_->phase,impl_->progress,impl_->text};}
std::span<const TransitionPhase> SceneTransitionCoordinator::phases()const{return impl_->log;}
const std::optional<TransitionOutcome>& SceneTransitionCoordinator::outcome()const{return impl_->outcome;}
bool SceneTransitionCoordinator::fatal()const{return impl_->fatal;}
void SceneTransitionCoordinator::fail_next_swap(){impl_->fail_swap=true;}
}
