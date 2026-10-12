#include <darkangel/cue_player.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// A sink that records what it was told, in order.
struct Recorder final:CueSink {
    std::vector<std::string> log;std::map<std::uint64_t,std::string> held;
    void start(const CueStart& begun)override{log.push_back("start "+begun.key+" on "+std::to_string(begun.network));check(held.emplace(begun.instance,begun.key).second,"A sink was given one instance twice");}
    void stop(std::uint64_t instance,unsigned tail)override{check(held.contains(instance),"A sink was told to stop an instance it does not hold");log.push_back("stop "+held.at(instance)+" tail "+std::to_string(tail));}
    void release(std::uint64_t instance)override{check(held.contains(instance),"A sink was told to release an instance it does not hold");log.push_back("release "+held.at(instance));held.erase(instance);}
};
const AssetId burn_cue=AssetId::random();
std::optional<CueDefinition> resolve(std::string_view key,AssetId cue){
    if(key=="burn.aura"&&cue==burn_cue)return CueDefinition{cue,"fx/burn","sfx/burn_loop",60,false};
    if(key=="swing")return CueDefinition{{},"fx/trail","sfx/swing",20,true};
    if(key=="hit.spark")return CueDefinition{{},"fx/spark","",10,false};
    if(key=="grunt")return CueDefinition{{},"","sfx/grunt",500,false};
    if(key=="silent")return CueDefinition{};
    return std::nullopt;
}
EffectCueUpdate effect(EffectCueEdge edge,std::uint64_t network,std::uint64_t handle,const char* key="burn.aura"){EffectCueUpdate update;update.edge=edge;update.state.id={7,network,1,handle,burn_cue};update.state.key=key;return update;}
ActionEvent block(ActionEdge edge,std::uint64_t activation,unsigned id,const char* key,ActionBlockKind kind=ActionBlockKind::Cue){ActionEvent event;event.activation=activation;event.block=id;event.edge=edge;event.kind=kind;event.key=key;return event;}
}
int main(){try{
    {
        // A persistent effect's cue: begins once, ends with its tail, and is released when the tail is over.
        Recorder visual,audio;CuePlayer player(resolve,&visual,&audio);player.effect(effect(EffectCueEdge::Begin,2,11),100);player.effect(effect(EffectCueEdge::Begin,2,11),101);player.effect(effect(EffectCueEdge::Update,2,11),102);
        check(player.live()==1&&visual.log==std::vector<std::string>{"start burn.aura on 2"}&&audio.log==visual.log,"An effect cue did not start exactly once on both sinks");
        player.effect(effect(EffectCueEdge::End,2,11),200);player.advance(259);check(player.live()==1&&visual.log.back()=="stop burn.aura tail 60","An ended effect cue did not fade by its tail");
        player.advance(260);check(player.live()==0&&visual.log.back()=="release burn.aura"&&audio.log.back()=="release burn.aura"&&visual.held.empty()&&audio.held.empty(),"A faded cue was not released from both sinks");
        // The same effect on another actor, and another instance of it on the same actor, are their own cues.
        player.effect(effect(EffectCueEdge::Begin,2,12),300);player.effect(effect(EffectCueEdge::Begin,3,12),300);player.effect(effect(EffectCueEdge::Begin,3,13),300);check(player.live()==3,"Cues of different effect instances or actors were merged");
        // An actor leaving releases its cues at once and nobody else's.
        player.retire(3);check(player.live()==1&&visual.held.size()==1&&visual.log.back()=="release burn.aura","Retiring an actor did not release exactly its cues");
        // A cue that begins again while it fades replaces the fading one.
        player.effect(effect(EffectCueEdge::End,2,12),310);player.effect(effect(EffectCueEdge::Begin,2,12),315);check(player.live()==1&&visual.held.size()==1&&!player.instances().front().stopping_until,"A cue restarted during its tail did not replace the fading instance");
    }
    {
        // Action cue blocks: a block with length starts and stops; a marker is a one-shot; other block kinds are not cues.
        Recorder visual,audio;CuePlayer player(resolve,&visual,&audio);player.action(1,1,block(ActionEdge::Begin,5,3,"swing"),10);player.action(1,1,block(ActionEdge::Begin,5,4,"hit",ActionBlockKind::HitWindow),10);
        check(player.live()==1&&visual.log.size()==1&&audio.log.size()==1,"A cue block did not start, or a hit window was taken for a cue");
        player.action(1,1,block(ActionEdge::Marker,5,6,"hit.spark"),14);check(player.live()==2&&visual.log.back()=="stop hit.spark tail 10"&&audio.log.size()==1,"A marker cue did not play once on the sink it names only");
        player.action(1,1,block(ActionEdge::Marker,5,7,"grunt"),14);check(audio.log.back()=="stop grunt tail 180"&&visual.log.back()=="stop hit.spark tail 10","A tail was not held to the player's limit, or an audio cue reached the visual sink");
        player.action(1,1,block(ActionEdge::End,5,3,"swing"),30);check(visual.log.back()=="stop swing tail 20","A cue block did not stop with its tail at its end");
        player.advance(1000);check(player.live()==0&&visual.held.empty()&&audio.held.empty(),"Action cues were not all released after their tails");
        // A rejected activation: its cues are cut with the short tail, a fading one included; another activation's cues are untouched.
        player.action(1,1,block(ActionEdge::Begin,8,3,"swing"),2000);player.action(1,1,block(ActionEdge::Marker,8,7,"grunt"),2000);player.action(1,1,block(ActionEdge::Begin,9,3,"swing"),2000);player.action(2,1,block(ActionEdge::Begin,8,3,"swing"),2000);
        player.rejected(1,8,2001);player.advance(2006);check(player.live()==4,"A rejected cue was released before its short tail");player.advance(2007);
        check(player.live()==2&&visual.log[visual.log.size()-2]=="stop swing tail 6","A rejected activation's cues were not cut with the rejection tail, or others were cut with them");
    }
    {
        // Bounds: a full player gives a fading instance's place to a new cue and drops a new cue when nothing is fading.
        Recorder visual;CueLimits small;small.live=3;CuePlayer player(resolve,&visual,nullptr,small);for(unsigned n=1;n<=3;++n)player.action(1,1,block(ActionEdge::Begin,n,3,"swing"),10);
        player.action(1,1,block(ActionEdge::Begin,4,3,"swing"),11);check(player.live()==3&&player.dropped()==1,"A full player did not drop the cue it has no room for");
        player.action(1,1,block(ActionEdge::End,2,3,"swing"),12);player.action(1,1,block(ActionEdge::Begin,5,3,"swing"),13);check(player.live()==3&&player.dropped()==1&&visual.held.size()==3,"A full player did not give a fading instance's place to a new cue");
        // A key with no presentation, and a cue that names nothing to play, are counted and hold nothing.
        player.retire(1);player.action(1,1,block(ActionEdge::Begin,6,3,"unknown.key"),20);player.action(1,1,block(ActionEdge::Begin,6,4,"silent"),20);check(player.live()==0&&player.unresolved()==2,"A cue without presentation was held");
    }
    {
        // Headless: no sinks. The lifecycle is the same and nothing is called.
        CuePlayer player(resolve,nullptr,nullptr);player.effect(effect(EffectCueEdge::Begin,2,11),1);player.action(1,1,block(ActionEdge::Marker,5,6,"hit.spark"),1);check(player.live()==2&&player.instances().size()==2&&player.instances()[0].key=="burn.aura"&&!player.instances()[0].action&&player.instances()[1].action,"A headless player does not track its instances");
        player.effect(effect(EffectCueEdge::End,2,11),2);player.advance(500);check(player.live()==0,"A headless player did not release its instances");
    }
    {
        // Destroying the player releases what the sinks still hold.
        Recorder visual,audio;{CuePlayer player(resolve,&visual,&audio);player.effect(effect(EffectCueEdge::Begin,2,11),1);player.action(1,1,block(ActionEdge::Begin,5,3,"swing"),1);}check(visual.held.empty()&&audio.held.empty(),"A destroyed player left instances in its sinks");
        bool refused=false;try{CueLimits bad;bad.live=0;CuePlayer player(resolve,nullptr,nullptr,bad);}catch(const std::exception&){refused=true;}check(refused,"A player without room for any cue was built");
    }
    {
        // Action cues derived from an action clock: each cue block once as the clock passes it, the last tick's cues when the action runs out.
        auto swing=std::make_shared<ActionDefinition>();swing->id=AssetId::random();swing->duration=10*action_tick_units;swing->loops=1;
        swing->blocks={{1,0,0,0,ActionBlockKind::Cue,"swing"},{2,1,5*action_tick_units,5*action_tick_units,ActionBlockKind::Cue,"hit.spark"},{3,2,9*action_tick_units+512,9*action_tick_units+512,ActionBlockKind::Cue,"grunt"},{4,3,2*action_tick_units,6*action_tick_units,ActionBlockKind::HitWindow,"Weapon.Right"}};
        const std::vector<std::shared_ptr<const ActionDefinition>> actions{swing};
        const auto at=[&](std::uint64_t activation,std::uint64_t ticks){ActionState state;state.activation=activation;state.clock=ticks*action_tick_units;state.entered=true;return std::optional<ActionState>(state);};
        const auto keys=[](const std::vector<ActionCueUpdate>& cues){std::string all;for(const auto& cue:cues){check(cue.event.kind==ActionBlockKind::Cue&&cue.event.edge==ActionEdge::Marker,"A derived cue is not a cue marker");all+=cue.event.key+" ";}return all;};
        {ActionCueTracker tracker(actions);std::string heard;for(std::uint64_t tick=0;tick<10;++tick){tracker.observe(5,1,CueTiming::Observed,swing->id,at(3,tick));const auto cues=tracker.drain_cues();for(const auto& cue:cues)check(cue.network==5&&cue.avatar_epoch==1&&cue.event.activation==3&&cue.timing==CueTiming::Observed,"A derived cue lost its actor, activation or timing");heard+=keys(cues);if(tick==0)check(heard=="swing ","The cue at the start of an action was not reported on its first tick");if(tick==4)check(heard=="swing ","A cue was reported before the clock reached it");if(tick==5)check(heard=="swing hit.spark ","A cue was not reported on the tick the clock passed it");}
            tracker.observe(5,1,CueTiming::Observed,{},std::nullopt);heard+=keys(tracker.drain_cues());check(heard=="swing hit.spark grunt "&&tracker.drain_cuts().empty(),"An action that ran out lost the cues of its last tick or was reported as cut");
            tracker.observe(5,1,CueTiming::Observed,{},std::nullopt);check(tracker.drain_cues().empty()&&tracker.drain_cuts().empty(),"An ended action reported again");}
        {// Interrupted before its time: a cut, and no cue it never reached.
            ActionCueTracker tracker(actions);for(std::uint64_t tick=0;tick<4;++tick)tracker.observe(5,1,CueTiming::OwnerPredicted,swing->id,at(3,tick));tracker.drain_cues();tracker.observe(5,1,CueTiming::OwnerPredicted,{},std::nullopt);const auto cuts=tracker.drain_cuts();check(tracker.drain_cues().empty()&&cuts.size()==1&&cuts.front().network==5&&cuts.front().activation==3,"An action that stopped early was not reported as cut exactly once");
            // Replaced by another activation: the old one is cut and the new one starts its own cues. A cancelled state counts as no action.
            tracker.observe(5,1,CueTiming::OwnerPredicted,swing->id,at(4,2));tracker.drain_cues();tracker.observe(5,1,CueTiming::OwnerPredicted,swing->id,at(5,0));check(keys(tracker.drain_cues())=="swing "&&tracker.drain_cuts().size()==1,"A replaced action was not cut or its successor lost its first cue");
            auto cancelled=at(5,1);cancelled->phase=ActionPhase::Cancelled;tracker.observe(5,1,CueTiming::OwnerPredicted,swing->id,cancelled);check(tracker.drain_cuts().size()==1,"A cancelled action was not cut");}
        {// A clock that goes back and forward again (a replayed prediction) reports nothing twice.
            ActionCueTracker tracker(actions);std::string heard;for(std::uint64_t tick:{0,1,2,3,2,3,4,5,6,5,6,7}){tracker.observe(5,1,CueTiming::OwnerPredicted,swing->id,at(3,tick));heard+=keys(tracker.drain_cues());}check(heard=="swing hit.spark "&&tracker.drain_cuts().empty(),"A replayed clock repeated or lost a cue");}
        {// First seen well into an action: its earlier cues are not replayed. Streams of one actor are separate.
            ActionCueTracker tracker(actions);tracker.observe(5,1,CueTiming::Observed,swing->id,at(3,6));check(tracker.drain_cues().empty(),"An action first seen late replayed its past cues");tracker.observe(5,1,CueTiming::OwnerConfirmed,swing->id,at(3,0));check(keys(tracker.drain_cues())=="swing ","A second stream of the same actor was not tracked on its own");
            // An actor that leaves, a new avatar and an action nobody prepared report nothing.
            tracker.retire(5);tracker.observe(5,1,CueTiming::Observed,{},std::nullopt);check(tracker.drain_cuts().empty(),"A retired actor's action was reported as cut");
            tracker.observe(6,1,CueTiming::Observed,swing->id,at(8,1));tracker.drain_cues();tracker.observe(6,2,CueTiming::Observed,{},std::nullopt);check(tracker.drain_cuts().empty(),"A new avatar's old action was reported as cut");
            tracker.observe(7,1,CueTiming::Observed,AssetId::random(),at(9,0));check(tracker.drain_cues().empty(),"An unprepared action produced cues");}
    }
    {
        // The owner's action is fed from prediction and from confirmation; each cue plays from exactly one of them.
        Recorder visual,audio;CuePlayer player(resolve,&visual,&audio);
        player.action(4,1,block(ActionEdge::Marker,9,1,"swing"),10,CueTiming::OwnerPredicted);player.action(4,1,block(ActionEdge::Marker,9,2,"hit.spark"),10,CueTiming::OwnerPredicted);player.action(4,1,block(ActionEdge::Marker,9,3,"nothing"),10,CueTiming::OwnerPredicted);
        check(audio.log==std::vector<std::string>{"start swing on 4","stop swing tail 20"}&&player.unresolved()==0,"The owner's prediction did not play exactly its predictable cues");
        player.action(4,1,block(ActionEdge::Marker,9,1,"swing"),12,CueTiming::OwnerConfirmed);player.action(4,1,block(ActionEdge::Marker,9,2,"hit.spark"),12,CueTiming::OwnerConfirmed);player.action(4,1,block(ActionEdge::Marker,9,3,"nothing"),12,CueTiming::OwnerConfirmed);
        check(audio.log.size()==2&&visual.log.size()==4&&visual.log[2]=="start hit.spark on 4"&&player.unresolved()==1,"The owner's confirmation repeated a predicted cue or lost a confirmed one");
        // The predicted swing is rejected: it is cut short.
        player.rejected(4,9,13);check(audio.log.back()=="stop swing tail 6","A rejected predicted cue was not cut");
    }
    {
        // The cue source.
        const std::string source=R"({"schema":1,"kind":"cue","asset":"0058d474-74c0-45b9-a4a6-b5d81963ab7e","name":"Swing","key":"Attack.Start","visual":"","audio":"royal_district/audio/swing","tail_ticks":12,"predictable":true})";
        const auto cue=decode_cue(source);check(cue.id.text()=="0058d474-74c0-45b9-a4a6-b5d81963ab7e"&&cue.name=="Swing"&&cue.key=="Attack.Start"&&cue.visual.empty()&&cue.audio=="royal_district/audio/swing"&&cue.tail_ticks==12&&cue.predictable,"A cue source did not decode to its fields");
        const auto refused=[&](std::string_view from,std::string_view to){auto changed=source;const auto found=changed.find(from);check(found!=changed.npos,"Cue test edit missing");changed.replace(found,from.size(),to);try{decode_cue(changed);}catch(const std::exception&){return true;}return false;};
        check(refused("\"royal_district/audio/swing\"","\"\""),"A cue with neither a visual nor a sound was accepted");check(refused("Attack.Start","Attack Start"),"A cue key with a space was accepted");check(refused(":12,",":601,"),"A cue tail beyond the bound was accepted");
        check(refused("\"schema\":1","\"schema\":2"),"An unknown cue schema was accepted");check(refused(",\"predictable\":true",""),"A cue source without a field was accepted");check(refused("\"predictable\":true","\"predictable\":true,\"extra\":1"),"A cue source with an unknown field was accepted");check(refused("\"kind\":\"cue\"","\"kind\":\"effect\""),"A source of another kind was accepted as a cue");
    }
    std::cout<<"Cue player: effect cues (once, tail, release, per instance and actor, restart during tail), action cue blocks (length, marker, other kinds ignored, per-sink routing, tail limit), rejected activations, actor retirement, bounds, cues without presentation, headless operation and release on destruction; action cues derived from a clock (once each, last tick, cut, replay, late first sight, retirement), owner predicted and confirmed streams, the cue source passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
