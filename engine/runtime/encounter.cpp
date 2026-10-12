#include <darkangel/encounter.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void fields(const Json& value,std::initializer_list<const char*> required,std::initializer_list<const char*> optional={}){
    require(value.is_object(),"Encounter field type");std::size_t found=0;for(auto key:required){require(value.contains(key),"Encounter is missing a field");++found;}
    for(auto key:optional)if(value.contains(key))++found;require(value.size()==found,"Encounter has an unknown field");
}
std::string name(const Json& value){require(value.is_string(),"World event or tag name type");auto text=value.get<std::string>();require(world_name_valid(text),"World event and tag names are lowercase letters, digits, '_' and '.', up to 48 characters");return text;}
unsigned count(const Json& value,unsigned minimum,unsigned maximum,const char* error){require(value.is_number_unsigned()&&value.get<std::uint64_t>()>=minimum&&value.get<std::uint64_t>()<=maximum,error);return value.get<unsigned>();}
}
bool world_name_valid(std::string_view text){return !text.empty()&&text.size()<=48&&text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")==text.npos&&text.front()!='.'&&text.back()!='.';}
void WorldEvents::emit(std::string text){require(world_name_valid(text),"World event name");require(queued_.size()<world_event_limit,"Too many world events in one tick");queued_.push_back(std::move(text));}
void WorldEvents::begin_tick(std::uint64_t tick){delivered_.swap(queued_);queued_.clear();for(const auto& event:delivered_){log_.push_back({tick,event});while(log_.size()>64)log_.pop_front();}}
bool WorldEvents::happened(std::string_view text)const{return std::find(delivered_.begin(),delivered_.end(),text)!=delivered_.end();}
void WorldEvents::set_tag(const std::string& text,bool on){require(world_name_valid(text),"World tag name");if(on){require(tags_.size()<256||tags_.contains(text),"Too many world tags");tags_.insert(text);}else tags_.erase(text);}

unsigned EncounterDefinition::enemies()const{if(mode==EncounterMode::Timed)return total;unsigned sum=0;for(const auto& wave:waves)sum+=wave.count;return sum;}
unsigned EncounterDefinition::concurrent()const{if(mode==EncounterMode::Timed)return std::min(max_alive,total);unsigned most=0;for(const auto& wave:waves)most=std::max(most,wave.count);return most;}
std::vector<AssetId> EncounterDefinition::npcs()const{std::vector<AssetId> result;auto add=[&](AssetId id){if(std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);};if(mode==EncounterMode::Timed)add(timed_npc);else for(const auto& wave:waves)add(wave.npc);return result;}
void validate_encounter(const EncounterDefinition& encounter){
    require(encounter.trigger_enter!=!encounter.trigger_event.empty(),"An encounter starts on a world event or when the player enters its bounds, not both or neither");
    require(encounter.trigger_event.empty()||world_name_valid(encounter.trigger_event),"Encounter trigger event name");
    for(const auto* text:{&encounter.started,&encounter.wave_cleared,&encounter.completed,&encounter.tag})require(text->empty()||world_name_valid(*text),"Encounter event or tag name");
    if(encounter.mode==EncounterMode::Waves){require(!encounter.waves.empty()&&encounter.waves.size()<=encounter_wave_limit,"An encounter has one to eight waves");for(const auto& wave:encounter.waves)require(wave.npc!=AssetId{}&&wave.count>=1&&wave.count<=16,"A wave spawns 1 to 16 enemies of one NPC definition");}
    else{require(encounter.mode==EncounterMode::Timed&&encounter.timed_npc!=AssetId{}&&std::isfinite(encounter.interval_seconds)&&encounter.interval_seconds>=.1&&encounter.interval_seconds<=600&&encounter.max_alive>=1&&encounter.max_alive<=16&&encounter.total>=1,"A timed encounter needs an NPC definition, 0.1 to 600 s between spawns, 1 to 16 alive at most and a total");}
    require(encounter.enemies()<=encounter_enemy_limit,"An encounter spawns at most 64 enemies in total");
    require(encounter.placement==EncounterPlacement::Points||encounter.placement==EncounterPlacement::Random,"Encounter placement");
    require(std::isfinite(encounter.corpse_seconds)&&encounter.corpse_seconds>=0&&encounter.corpse_seconds<=600,"Encounter corpse time is 0 to 600 s");
}
EncounterDefinition decode_encounter(std::string_view text){
    require(text.size()<=16384,"Encounter component size bound");const auto value=Json::parse(text);EncounterDefinition encounter;
    const bool timed=value.is_object()&&value.contains("mode")&&value.at("mode")=="timed";
    fields(value,{"trigger","mode",timed?"timed":"waves","placement"},{"emits","tag","corpse_seconds","audio_profile"});
    const auto& trigger=value.at("trigger");require(trigger.is_object()&&trigger.size()==1,"Encounter trigger is {\"event\": name} or {\"enter\": true}");
    if(trigger.contains("event"))encounter.trigger_event=name(trigger.at("event"));else{require(trigger.contains("enter")&&trigger.at("enter").is_boolean()&&trigger.at("enter").get<bool>(),"Encounter trigger is {\"event\": name} or {\"enter\": true}");encounter.trigger_enter=true;}
    require(value.at("mode").is_string()&&(timed||value.at("mode")=="waves"),"Encounter mode is \"waves\" or \"timed\"");
    if(timed){encounter.mode=EncounterMode::Timed;const auto& timing=value.at("timed");fields(timing,{"npc","interval_seconds","max_alive","total"});require(timing.at("npc").is_string()&&timing.at("interval_seconds").is_number(),"Timed encounter field types");encounter.timed_npc=AssetId::parse(timing.at("npc").get<std::string>());encounter.interval_seconds=timing.at("interval_seconds").get<double>();encounter.max_alive=count(timing.at("max_alive"),1,16,"Timed encounter alive cap is 1 to 16");encounter.total=count(timing.at("total"),1,encounter_enemy_limit,"Timed encounter total is 1 to 64");}
    else{encounter.mode=EncounterMode::Waves;const auto& waves=value.at("waves");require(waves.is_array()&&!waves.empty()&&waves.size()<=encounter_wave_limit,"An encounter has one to eight waves");for(const auto& entry:waves){fields(entry,{"npc","count"});require(entry.at("npc").is_string(),"Wave NPC type");encounter.waves.push_back({AssetId::parse(entry.at("npc").get<std::string>()),count(entry.at("count"),1,16,"A wave spawns 1 to 16 enemies")});}}
    require(value.at("placement").is_string()&&(value.at("placement")=="points"||value.at("placement")=="random"),"Encounter placement is \"points\" or \"random\"");encounter.placement=value.at("placement")=="points"?EncounterPlacement::Points:EncounterPlacement::Random;
    if(value.contains("emits")){const auto& emits=value.at("emits");fields(emits,{},{"started","wave_cleared","completed"});if(emits.contains("started"))encounter.started=name(emits.at("started"));if(emits.contains("wave_cleared"))encounter.wave_cleared=name(emits.at("wave_cleared"));if(emits.contains("completed"))encounter.completed=name(emits.at("completed"));}
    if(value.contains("tag"))encounter.tag=name(value.at("tag"));
    if(value.contains("corpse_seconds")){require(value.at("corpse_seconds").is_number(),"Encounter corpse time type");encounter.corpse_seconds=value.at("corpse_seconds").get<double>();}
    if(value.contains("audio_profile")){require(value.at("audio_profile").is_string(),"Encounter audio profile type");encounter.audio_profile=AssetId::parse(value.at("audio_profile").get<std::string>());require(encounter.audio_profile!=AssetId{},"Encounter audio profile identity");}
    validate_encounter(encounter);return encounter;
}
TriggerDefinition decode_trigger(std::string_view text){
    require(text.size()<=4096,"Trigger component size bound");const auto value=Json::parse(text);fields(value,{"event","half"},{"once"});TriggerDefinition trigger;trigger.event=name(value.at("event"));
    if(value.contains("once")){require(value.at("once").is_boolean(),"Trigger once type");trigger.once=value.at("once").get<bool>();}
    const auto& half=value.at("half");require(half.is_array()&&half.size()==3&&half[0].is_number()&&half[1].is_number()&&half[2].is_number(),"Trigger half extents are three numbers");trigger.half={half[0].get<double>(),half[1].get<double>(),half[2].get<double>()};
    require(trigger.half.x>0&&trigger.half.y>0&&trigger.half.z>0&&trigger.half.x<=200&&trigger.half.y<=200&&trigger.half.z<=200,"Trigger half extents are above 0, up to 200 m");return trigger;
}
WorldTrigger::WorldTrigger(TriggerDefinition definition,MotorVec center,double yaw):definition_(std::move(definition)),volume_{center,yaw,definition_.half}{
    require(world_name_valid(definition_.event)&&std::isfinite(yaw)&&definition_.half.x>0&&definition_.half.y>0&&definition_.half.z>0,"Trigger volume event and bounds");
}
bool WorldTrigger::step(WorldEvents& world,MotorVec player,bool player_alive){
    const bool now=player_alive&&encounter_contains({&volume_,1},player);const bool entered=now&&!inside_;inside_=now;
    if(!entered||(definition_.once&&fired_))return false;world.emit(definition_.event);++fired_;return true;
}
bool encounter_contains(std::span<const EncounterVolume> volumes,MotorVec point){
    for(const auto& volume:volumes){const double dx=point.x-volume.center.x,dy=point.y-volume.center.y,dz=point.z-volume.center.z,c=std::cos(volume.yaw),s=std::sin(volume.yaw);
        // Into the box's own frame: its forward is (sin yaw, cos yaw).
        const double right=c*dx-s*dz,forward=s*dx+c*dz;if(std::abs(right)<=volume.half.x&&std::abs(dy)<=volume.half.y&&std::abs(forward)<=volume.half.z)return true;}
    return false;
}
EncounterDirector::EncounterDirector(EncounterDefinition definition,std::vector<EncounterVolume> volumes,std::vector<EncounterPoint> points,std::uint64_t seed):definition_(std::move(definition)),volumes_(std::move(volumes)),points_(std::move(points)),random_(seed|1){
    validate_encounter(definition_);require(volumes_.size()<=encounter_volume_limit&&points_.size()<=encounter_point_limit,"An encounter has at most 16 volumes and 32 spawn points");
    for(const auto& volume:volumes_)require(std::isfinite(volume.yaw)&&volume.half.x>0&&volume.half.y>0&&volume.half.z>0&&volume.half.x<=200&&volume.half.y<=200&&volume.half.z<=200,"Encounter volume half extents are above 0, up to 200 m");
    require(!definition_.trigger_enter||!volumes_.empty(),"An encounter that starts when the player enters needs at least one volume");
    require(definition_.placement!=EncounterPlacement::Random||!volumes_.empty(),"An encounter that spawns at random points needs at least one volume");
    require(definition_.placement!=EncounterPlacement::Points||!points_.empty(),"An encounter that spawns at its spawn points needs at least one spawn point that names it");
}
EncounterSpawn EncounterDirector::place(AssetId npc,const Ground& ground,const Occupied& occupied,std::span<const EncounterSpawn> placed){
    EncounterSpawn spawn;spawn.npc=npc;spawn.index=spawned_;
    if(definition_.placement==EncounterPlacement::Points){
        // Round robin over the points; a further pass around the same points steps aside so bodies do not start inside each other.
        const auto& point=points_[spawned_%points_.size()];const unsigned first=unsigned(spawned_/points_.size())%7;spawn.yaw=point.yaw;
        // The point itself, then six places around it, starting with this pass's own: the first one nobody stands on.
        for(unsigned attempt=0;attempt<7;++attempt){const unsigned lap=(first+attempt)%7;spawn.position=point.position;if(lap){const double angle=point.yaw+1.0471975511965976*lap;spawn.position.x+=std::sin(angle)*.9;spawn.position.z+=std::cos(angle)*.9;}
            bool crowded=occupied&&occupied(spawn.position);for(const auto& other:placed)crowded|=std::hypot(other.position.x-spawn.position.x,other.position.z-spawn.position.z)<.7;if(!crowded)break;}
        return spawn;
    }
    auto next=[&]{random_+=0x9e3779b97f4a7c15ull;auto z=random_;z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;z=(z^(z>>27))*0x94d049bb133111ebull;return double((z^(z>>31))>>11)/9007199254740992.;};
    double area=0;for(const auto& volume:volumes_)area+=volume.half.x*volume.half.z;std::optional<EncounterSpawn> fallback;
    for(unsigned attempt=0;attempt<8;++attempt){
        double pick=next()*area;const EncounterVolume* chosen=&volumes_.back();for(const auto& volume:volumes_){pick-=volume.half.x*volume.half.z;if(pick<=0){chosen=&volume;break;}}
        const double right=(next()*2-1)*chosen->half.x,forward=(next()*2-1)*chosen->half.z,c=std::cos(chosen->yaw),s=std::sin(chosen->yaw),facing=next()*6.283185307179586-3.141592653589793;
        MotorVec point{chosen->center.x+c*right+s*forward,chosen->center.y,chosen->center.z-s*right+c*forward};
        const auto height=ground?ground(point.x,chosen->center.y+chosen->half.y,point.z):std::optional<double>{};if(!height||*height<chosen->center.y-chosen->half.y-.5||*height>chosen->center.y+chosen->half.y)continue;
        point.y=*height;spawn.position=point;spawn.yaw=facing;if(!fallback)fallback=spawn;
        bool crowded=occupied&&occupied(point);for(const auto& other:placed)crowded|=std::hypot(other.position.x-point.x,other.position.z-point.z)<1.;
        if(!crowded)return spawn;
    }
    require(fallback.has_value(),"An encounter found no ground inside its volumes to spawn on");return *fallback;
}
std::vector<EncounterSpawn> EncounterDirector::wave(const Ground& ground,const Occupied& occupied){
    std::vector<EncounterSpawn> result;const auto& current=definition_.waves[wave_];for(unsigned index=0;index<current.count;++index){result.push_back(place(current.npc,ground,occupied,result));++spawned_;}return result;
}
unsigned EncounterDirector::need()const{
    if(phase_==EncounterPhase::Complete)return 0;
    if(definition_.mode==EncounterMode::Timed)return spawned_<definition_.total?1u:0u;
    if(phase_==EncounterPhase::Waiting||armed_)return definition_.waves[wave_].count;
    return wave_+1<definition_.waves.size()?definition_.waves[wave_+1].count:0u;
}
std::vector<EncounterSpawn> EncounterDirector::step(std::uint64_t tick,WorldEvents& world,MotorVec player,unsigned alive,const Ground& ground,const Occupied& occupied,unsigned room){
    std::vector<EncounterSpawn> result;alive_=alive;
    if(phase_==EncounterPhase::Waiting){
        if(!(definition_.trigger_enter?encounter_contains(volumes_,player):world.happened(definition_.trigger_event)))return result;
        phase_=EncounterPhase::Running;if(!definition_.started.empty())world.emit(definition_.started);if(!definition_.tag.empty())world.set_tag(definition_.tag,true);
        if(definition_.mode==EncounterMode::Waves)armed_=true;else next_spawn_=tick;
    }
    if(phase_!=EncounterPhase::Running)return result;
    const auto finish=[&]{phase_=EncounterPhase::Complete;if(!definition_.completed.empty())world.emit(definition_.completed);if(!definition_.tag.empty())world.set_tag(definition_.tag,false);};
    if(definition_.mode==EncounterMode::Waves){
        // The wave spawned on an earlier tick is dead: the next one is armed, or the encounter ends.
        if(!armed_&&tick>spawned_tick_&&alive==0){if(wave_+1==definition_.waves.size()){finish();return result;}if(!definition_.wave_cleared.empty())world.emit(definition_.wave_cleared);++wave_;armed_=true;}
        // An armed wave enters whole, once the session has room for all of it.
        if(armed_&&room>=definition_.waves[wave_].count){result=wave(ground,occupied);armed_=false;spawned_tick_=tick;}
    }else{
        if(spawned_<definition_.total&&alive<definition_.max_alive&&tick>=next_spawn_&&room>=1){result.push_back(place(definition_.timed_npc,ground,occupied,{}));++spawned_;spawned_tick_=tick;next_spawn_=tick+std::uint64_t(std::llround(definition_.interval_seconds*60));}
        else if(spawned_==definition_.total&&alive==0&&tick>spawned_tick_)finish();
    }
    alive_=alive+unsigned(result.size());return result;
}
EncounterStatus EncounterDirector::status()const{return {phase_,definition_.mode==EncounterMode::Waves?wave_+1:0u,definition_.mode==EncounterMode::Waves?unsigned(definition_.waves.size()):0u,spawned_,definition_.enemies(),alive_};}
}
