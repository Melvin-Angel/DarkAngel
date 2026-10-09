#include "editor_controller.hpp"
#include <nlohmann/json.hpp>
namespace darkangel::editor_app {
using Json=nlohmann::json;
Controller::Controller(AssetId asset,double r):model(asset),radius(r){demo();}
void Controller::configure_assets(const std::filesystem::path& source,const std::filesystem::path& cache){auto candidate=std::make_unique<AssetService>(source,cache);candidate->scan();auto inventory=candidate->assets();assets=std::move(candidate);asset_inventory=std::move(inventory);}
void Controller::refresh_assets(){if(!assets)return;assets->scan();asset_inventory=assets->assets();}
CookResult Controller::import_asset(const PreparedAssetImport& prepared){if(!assets)throw std::runtime_error("Open the editor with a project source mount to import assets");auto result=assets->commit_import(prepared);asset_inventory=assets->assets();log("Imported "+prepared.destination()+" into the project. External source preserved.");return result;}
void Controller::log(std::string message,ConsoleSeverity severity){console.push_back({std::move(message),severity});if(console.size()>128)console.erase(console.begin());}
void Controller::demo(){stop();World defaults(WorldDomain::Authoring);ObjectData object;object.id={0,1};defaults.create(object);auto entity=Json::parse(defaults.serialize()).at("objects")[0];entity.erase("id");entity["name"]="Spinner";entity["model"]=model.text();entity["scripts"]={{StableId{0,9}.text(),{{"asset","33333333-3333-4333-8333-333333333333"},{"config",{{"speed",.5}}},{"enabled",true}}}};
    auto root=AssetId::parse("22222222-2222-4222-8222-222222222222"),assembly=AssetId::parse("11111111-1111-4111-8111-111111111111"),script=AssetId::parse("33333333-3333-4333-8333-333333333333");
    Json definition={{"schema",1},{"asset",assembly.text()},{"entities",{{StableId{0,1}.text(),entity}}}};
    Json scene={{"schema",1},{"asset",root.text()},{"entities",Json::object()},{"mounts",Json::object()}};
    for(unsigned i=2;i<=3;++i){scene["mounts"][StableId{0,i}.text()]={{"asset",assembly.text()},{"patches",Json::array({{{"op","SetProperty"},{"target",StableId{0,1}.text()},{"type",1},{"property",2},{"value",i==2?-radius*1.35:radius*1.35}},{{"op","SetProperty"},{"target",StableId{0,1}.text()},{"type",1},{"property",7},{"value",.8}}})}};}
    Json behavior={{"schema",1},{"asset",script.text()},{"kind","script"},{"domain","presentation"},{"source","--!strict\nreturn {create=function(ctx:Context,config:BehaviorConfig):BehaviorState return {angle=ctx:get_yaw()} end,update=function(ctx:Context,state:BehaviorState,config:BehaviorConfig,dt:number) state.angle += config.speed*dt;ctx:set_yaw(state.angle) end}"}};
    document=std::make_unique<EditorDocument>(root,StableId{0,1},AssemblySources{{root,scene.dump()},{assembly,definition.dump()},{script,behavior.dump()}});selected=document->plan().origins.front().object;log("Created two linked Spinner placements; authoring is idle.");
}
void Controller::open(const std::filesystem::path& file){auto candidate=EditorDocument::open(file);candidate->scripts();for(const auto& [object,asset]:candidate->plan().models){if(prepare_model)prepare_model(asset);else if(asset!=model)throw std::runtime_error("Scene requires a model outside the loaded cooked registry");}stop();document=std::move(candidate);path=file;selected=document->plan().origins.empty()?StableId{}:document->plan().origins.front().object;log("Opened "+file.string());}
void Controller::save(const std::filesystem::path& file){document->save(file);path=file;log("Saved scene manifest and stable placement records.");}
void Controller::play(){if(authoring.dirty())throw std::runtime_error("Save gameplay changes in Ability before Play");if(prepare_play_resources)prepare_play_resources();auto scripts=document->scripts();auto prepared=preview.prepare(document->plan(),scripts,[&](AssetId id){if(prepare_model)prepare_model(id);else if(id!=model)throw std::runtime_error("Model is outside the loaded cooked registry");});if(prepare_gameplay)prepare_gameplay(prepared->plan,*prepared->world);preview.activate(std::move(prepared));paused=false;log("Play activated an isolated Preview world; authoring remains unchanged.");}
void Controller::stop(){if(stop_gameplay)stop_gameplay();preview.retire();paused=false;}
void Controller::step(){if(!preview.active())play();paused=true;if(step_gameplay)step_gameplay();else preview.step(1./60);}
void Controller::update(double seconds){if(preview.active() && !paused){try{if(update_gameplay)update_gameplay(seconds);else preview.step(seconds);}catch(const std::exception& error){stop();log(error.what(),ConsoleSeverity::Error);}}}
const World& Controller::displayed(){if(preview.active()&&gameplay_view){if(auto view=gameplay_view())return *view;}return preview.active()?*preview.active()->world:document->world();}
void Controller::reload_scripts(){auto definitions=document->scripts();if(preview.active())for(const auto& [id,definition]:definitions)if(!preview.active()->scripts->load_cooked(definition.descriptor,definition.cooked))throw std::runtime_error(preview.active()->scripts->diagnostic());log("Script reload committed; prior state preserved and old tasks cancelled.");}
}
