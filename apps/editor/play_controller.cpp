#define NOMINMAX
#define UNICODE
#include <darkangel/model_collision.hpp>
#include <Windows.h>
#include <darkangel/scene_authority.hpp>
#include "play_controller.hpp"
#ifdef DAE_HUD
#include "play_ui.hpp"
#include <ashen_roots/royal_ui_assets.hpp>
#include "frontend_ui.hpp"
#include <darkangel/ui_assets.hpp>
#endif
#include "editor_options.hpp"
#include "editor_ui.hpp"
#include "package_file.hpp"
#include "vfx_workspace.hpp"
#include "play_saves.hpp"
#include <darkangel/static_collision.hpp>
#include "render/gpu_resources.hpp"
#include "exercises/play_exercises.hpp"
#include "exercises/synthetic_input.hpp"
#include "exercises/world_place_exercise.hpp"
#ifdef DAE_NAVIGATION
#include "navigation_tool.hpp"
#endif
#ifdef DAE_BEHAVIOR
#include <darkangel/behavior_tree.hpp>
#endif
#ifdef DAE_EDITOR_GNS
#include "network_play.hpp"
#endif
#ifdef DAE_HUD
#include "play_hud.hpp"
#include "runtime_inventory.hpp"
#include "runtime_altar.hpp"
#include "world_ui_presenter.hpp"
#include <ashen_roots/royal_hud.hpp>
#endif
#ifdef DAE_AUDIO
#include <darkangel/audio_director.hpp>
#endif
#include <darkangel/check.hpp>
#include <darkangel/cue_assets.hpp>
#include <darkangel/encounter.hpp>
#include <darkangel/primitive_assets.hpp>
#include <darkangel/vfx.hpp>
#include <darkangel/vfx_assets.hpp>
#include <darkangel/world_objects.hpp>
#include <DirectXMath.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace darkangel::editor_app {
PlayController::PlayController(PlayContext context)
    : cue_primitives{&context.recorders.visual}, cue_effects{&cue_primitives}, companion_script(context.options.input_script),
      owner_script(context.options.player_script), args(context.options), editor(context.editor), shell(context.shell),
      vfx(context.vfx.service), vfx_failed(context.vfx.failed),
#ifdef DAE_AUDIO
      audio_workspace(*context.audio), audio(context.audio->service), play_sounds(context.audio->play_sounds),
      cue_sounds(context.audio->cue_sounds), audio_failed(context.audio->failed), audio_director(context.audio->director),
      play_profiles(context.audio->play_profiles), encounter_profiles(context.audio->encounter_profiles),
      audio_applied(context.audio->applied), audio_in_play(context.audio->in_play), audio_tab(context.audio->state),
#endif
      navigation_(context.navigation),
#ifdef DAE_HUD
      hud_renderer(context.hud->hud_renderer), hud(context.hud->hud), hud_failed(context.hud->hud_failed),
      hud_second_drawn(context.hud->hud_second_drawn),
#endif
      cue_visual(context.recorders.visual), cue_audio(context.recorders.audio), play_saves(context.saves),
      runtime_inventory(context.inventory), runtime_altar(context.altar), window_input(context.window_input),
      close_window_(std::move(context.close_window)), reload_model_(std::move(context.reload_model)), scene_models_(context.scene_models) {
    // Play's controls: the cooked input profile of the selected Player or Combat Kit, or the registry's only one.
    AssetId selected_input;
#ifdef DAE_ANIMATION
    // A Player not yet in the start registry is packaged when Play prepares its resources; with a project mount that is not an error.
    if (!args.player_asset.empty())
        try {
            selected_input = load_cooked_player(args.registry, args.cas, AssetId::parse(args.player_asset)).input.id;
        } catch (const std::exception&) {
            if (args.sources.empty())
                throw;
        }
    else if (args.character_clips.starts_with("player:"))
        selected_input = load_cooked_player(args.registry, args.cas, AssetId::parse(args.character_clips.substr(7))).input.id;
    else if (args.character_clips.starts_with("kit:"))
        selected_input = load_cooked_combat_kit(args.registry, args.cas, AssetId::parse(args.character_clips.substr(4))).input.id;
#endif
    {
        std::ifstream file(args.registry);
        auto registry = nlohmann::json::parse(file);
        for (const auto& record : registry.at("assets"))
            if (record.at("kind") == "input" && (selected_input == AssetId{} || record.at("id") == selected_input.text())) {
                require(!inputs, "Editor package has multiple input profiles; select a Player or Combat Kit explicitly");
                inputs = std::make_unique<InputManager>(
                    load_cooked_input(args.registry, args.cas, AssetId::parse(record.at("id").get<std::string>())));
            }
        if (inputs) {
            auto controls = inputs->controls();
            native_input = std::make_unique<darkangel::editor_app::NativeInput>(controls);
            window_input = native_input.get();
            shell.input = inputs.get();
        }
    }
    require(args.players >= 1 && args.players <= 4, "--players is 1 to 4");
    shell.players = int(args.players);
    // Tools > Saves: Save now while this host runs Play and the party is not travelling.
    shell.draw_saves_tool = [this] {
        if (!play_saves) {
            ImGui::TextDisabled("Saves need a project with an asset cache.");
            return;
        }
        play_saves->draw(character && travel && !travel->busy(), [&] {
            if (save_play)
                save_play("Saved from Tools > Saves");
        });
    };
#ifdef DAE_HUD
    shell.open_frontend=[this]{
        if(editor.preview.active())throw std::runtime_error("Stop Play before opening the front end");
        for(const auto& a:editor.asset_inventory)if(a.path=="ui/menus/MainMenu.daui"){
            auto font=editor.packaged_ui_font();require(font.has_value(),"Front end requires a packaged font");
            frontend=std::make_unique<FrontendUi>(hud_renderer,UiFont{"HUD",font->second},editor.package_model(a.id),editor.assets->cas_path(),a.id.text());return;
        }throw std::runtime_error("Create ui/menus/MainMenu.daui in the UI tab first");
    };
    shell.game_overlay = [this](ImDrawList* draw, darkangel::editor_app::ViewArea area) { draw_hud(draw, area); };
    shell.game_overlay_second = [this](ImDrawList* draw, darkangel::editor_app::ViewArea area) { draw_second_hud(draw, area); };
#endif
    // Scene transitions in the editor's Play. The destination becomes the scene the editor's document, Play resources and
    // uploaded models are for, prepared exactly as a fresh Play of that scene would be; the session that is still running
    // keeps what it was built from. Every scene uses the collision asset this editor was started with: a destination whose
    // placements that asset does not match fails its preparation and the party stays where it is.
    scene_provider.on_begin = [this](AssetId scene) {
        drop_load();
        travel_load.scene = scene;
    };
    scene_provider.on_poll = [this] { return poll_load(); };
    scene_provider.on_take = [this] { return std::move(travel_load.prepared); };
    scene_provider.on_cancel = [this] { drop_load(); };
}
PlayController::~PlayController() = default;

StableId PlayController::scene_role(const std::string& value) {
    if (value.empty())
        return StableId{};
    auto id = StableId::parse(value);
    if (editor.document->world().valid(editor.document->world().find(id)))
        return id;
    StableId resolved;
    for (const auto& origin : editor.document->plan().origins)
        if (origin.local_path.ends_with("/" + id.text())) {
            require(!resolved, "Ambiguous character scene role; use a placed identity");
            resolved = origin.object;
        }
    require(bool(resolved), "Character scene role missing");
    return resolved;
}
// Spawn points whose NPC definition is in a cooked registry. A startup registry may predate the scene's spawn points; a prepared Play
// packages them all.
std::map<StableId, AssetId> PlayController::registry_spawns(const std::filesystem::path& path) {
    std::ifstream input(path);
    auto registry = nlohmann::json::parse(input);
    std::set<std::string> known;
    for (const auto& record : registry.at("assets"))
        if (record.at("kind") == "npc")
            known.insert(record.at("id").get<std::string>());
    std::map<StableId, AssetId> result;
    for (const auto& [point, npc] : editor.document->plan().spawns)
        if (known.contains(npc.text()) && !editor.document->plan().spawn_owners.contains(point))
            result.emplace(point, npc);
    return result;
}
// NPC definitions the scene's encounters can spawn, and those of them a cooked registry holds.
std::set<AssetId> PlayController::encounter_npcs() {
    std::set<AssetId> result;
    for (const auto& [object, json] : editor.document->plan().encounters) {
        try {
            for (auto npc : darkangel::decode_encounter(json).npcs())
                result.insert(npc);
        } catch (const std::exception&) {
        }
    }
    return result;
}
std::set<AssetId> PlayController::registry_encounter_npcs(const std::filesystem::path& path) {
    std::ifstream input(path);
    auto registry = nlohmann::json::parse(input);
    std::set<std::string> known;
    for (const auto& record : registry.at("assets"))
        if (record.at("kind") == "npc")
            known.insert(record.at("id").get<std::string>());
    std::set<AssetId> result;
    for (auto npc : encounter_npcs())
        if (known.contains(npc.text()))
            result.insert(npc);
    return result;
}
// Authored static collision for the open scene: its collision profile, else --character-collision, else none (the placements).
AssetId PlayController::authored_collision() const {
    if (const auto& profiles = editor.document->plan().collision_profiles; !profiles.empty())
        return profiles.begin()->second;
    return args.character_collision.empty() ? AssetId{} : AssetId::parse(args.character_collision);
}
std::unique_ptr<CharacterPreviewResources> PlayController::load_resources(const RuntimeSkinnedModel& skin) {
    return std::make_unique<darkangel::editor_app::CharacterPreviewResources>(darkangel::editor_app::load_character_preview(
        args.registry, args.cas, args.character_clips, skin, scene_role(args.character_player), registry_spawns(args.registry),
        authored_collision(), registry_encounter_npcs(args.registry)));
}
StableId PlayController::first_actor() {
    require(character_resources && !character_resources->spawns.empty(), "This exercise needs a scene with an NPC spawn point");
    return character_resources->spawns.front().actor;
}

// The soft-locked actor if there is one, else the nearest live hostile in front of the camera; a target that stops
// qualifying stays shown until another one does.
std::optional<StableId> PlayController::choose_hud_target() {
    if (!character || !character_resources || !character_resources->combat) {
        hud_target.reset();
        return hud_target;
    }
#ifdef DAE_EDITOR_GNS
    // A networked client that is catching up on a new world revision holds no observer state to aim at.
    if ((party_host || party_guest) && !character->bootstrapped()) {
        hud_target.reset();
        return hud_target;
    }
#endif
    if (auto locked = character->soft_lock(); locked && character->observer_if_ready(*locked, double(character->motor().tick))) {
        hud_target = locked;
        return hud_target;
    }
    const auto& combat = *character_resources->combat;
    const auto& self = character->motor();
    const double yaw = director ? director->yaw() : self.yaw, forward_x = std::sin(yaw), forward_z = std::cos(yaw);
    std::optional<StableId> best;
    double nearest{};
    // Every actor the session holds now, enemies an encounter brought in included.
    for (const auto id : character->actors()) {
        const auto* actor = character->actor(id);
        if (!actor || actor->faction == combat.faction)
            continue;
        auto handle = character->presentation().find(id);
        if (!character->presentation().valid(handle) || character->presentation().read(handle).health.current <= 0)
            continue;
        const auto observed = character->observer_if_ready(id, double(self.tick));
        if (!observed)
            continue;
        const auto at = observed->frame.motor.position;
        const double dx = at.x - self.position.x, dz = at.z - self.position.z, distance = std::hypot(dx, dz);
        if (dx * forward_x + dz * forward_z <= 0 || distance > 40)
            continue;
        if (!best || distance < nearest) {
            best = id;
            nearest = distance;
        }
    }
    hud_target = best;
    return hud_target;
}

PlayCueState PlayController::cue_state() const {
    return {cue_player,      play_cues,   play_cue_closure,        cue_visual, cue_audio,
            cue_primitives,  cue_effects, play_effect_generations, vfx,        vfx_failed,
#ifdef DAE_AUDIO
            &audio_workspace
#else
            nullptr
#endif
    };
}

#ifdef DAE_HUD
// Runtime HUD: the game's RmlUi document over the Game viewport, fed by a native view model of confirmed
// state. Its geometry goes into the viewport's draw list; font atlases are device textures.
UiService* PlayController::authored_ui(){return play_ui?&play_ui->service:nullptr;}
UiService* PlayController::frontend_service(){return frontend?&frontend->service:nullptr;}
PlayUi& PlayController::ensure_ui(bool second){
    if(ui_resources!=character_resources.get()||ui_registry!=args.registry){hud=nullptr;play_ui.reset();play_ui_second.reset();ui_resources=character_resources.get();ui_registry=args.registry;}
    auto& destination=second?play_ui_second:play_ui;
    if(!destination){auto font=editor.packaged_ui_font();std::string bytes=font?std::move(font->second):load_cooked_font(args.registry,args.cas,AssetId::parse("4f1d2a7c-6b0e-4c55-9a3e-8d2f5b7c1e90"));
        destination=std::make_unique<PlayUi>(hud_renderer,UiFont{"HUD",std::move(bytes)},args.registry,args.cas,character_resources->player_ui,character_resources->actor_ui);
    }
    if(!second)hud=&destination->player;
    return *destination;
}
namespace {std::vector<UiActorInfo> ui_actors(const CharacterSceneSession& session){std::vector<UiActorInfo> result;for(auto id:session.actors())if(const auto* actor=session.actor(id))result.push_back({id,actor->name});return result;}}
void PlayController::draw_hud(ImDrawList* draw,const ViewArea& area){
    if(frontend&&!editor.preview.active()){
        if(frontend->frame(draw,area.x,area.y,area.width,area.height,ImGui::IsWindowFocused())){frontend.reset();editor.play();}return;
    }
    if(!character||!character_resources||!character_resources->combat||!character->ability()){hud=nullptr;play_ui.reset();play_ui_second.reset();ui_resources=nullptr;return;}
    if(hud_failed)return;
    try{
        auto& ui=ensure_ui();hud->resize(std::max(1,int(area.width)),std::max(1,int(area.height)));
        const auto& combat=*character_resources->combat;std::optional<Health> target;const auto shown=choose_hud_target();
        if(shown){auto handle=character->presentation().find(*shown);if(character->presentation().valid(handle))target=character->presentation().read(handle).health;}
        auto model=ashen_roots::royal_hud(*character->ability(),combat.attributes,target,RuntimeInventory::name(*character_resources,character->ability()));
        model.target_locked=character->soft_lock().has_value();if(shown)if(const auto* actor=character->actor(*shown))model.target_name=actor->name;
        model.crosshair=director&&director->rig().cameras[director->live()].facing==CameraFacing::Camera;
        const auto offered=character->interaction_prompt();if(offered)model.prompt=offered->prompt;
        auto* client=character->client(character_player);if(client)party_lifecycle_hud(model,*client,offered,interact_held);
        if(travel&&travel->busy()){const auto status=travel->status();model.loading=true;model.loading_text=status.text.substr(0,96);model.loading_progress=status.progress;++travel_overlay_frames;}
        hud->update(model);
        runtime_inventory.frame(*hud,client,*character_resources,area.x,area.y,area.width,area.height,ImGui::IsWindowFocused(),model.loading);
        runtime_altar.frame(*hud,client,character->hosting(),ImGui::IsWindowFocused(),model.loading);
        if(client)ui.frame(*client,model,world_ui_camera(shell.scene_view,shell.scene_projection,area.width,area.height),ImGui::IsWindowFocused(),model.loading,area.x,area.y,character->players(),ui_actors(*character));
        runtime_inventory.flush(*hud,client,*character_resources,model.loading);runtime_altar.flush(*hud,client,character->hosting(),model.loading);ui.service.update();
        shell.inventory_open=(shell.controls_player==0)&&(runtime_inventory.open()||runtime_altar.open()||ui.modal());
        ui.render(draw,area.x,area.y,area.width,area.height);
    }catch(const std::exception& error){hud_renderer.end();hud=nullptr;play_ui.reset();play_ui_second.reset();hud_failed=true;editor.log(std::string("Game UI unavailable: ")+error.what());}
}
void PlayController::draw_second_hud(ImDrawList* draw,const ViewArea& area){
    if(!character||character->players().size()<2||!character_resources||!character_resources->combat||hud_failed)return;
    auto* client=character->client(character->players()[1]);if(!client||!client->ability())return;
    try{
        auto& ui=ensure_ui(true);if(!inventory_second)inventory_second=std::make_unique<RuntimeInventory>();if(!altar_second)altar_second=std::make_unique<RuntimeAltar>();
        auto model=ashen_roots::royal_hud(*client->ability(),character_resources->combat->attributes,{},RuntimeInventory::name(*character_resources,client->ability()));
        model.target_locked=client->soft_lock().has_value();if(const auto offered=client->interaction_prompt())model.prompt=offered->prompt;
        if(travel&&travel->busy()){const auto status=travel->status();model.loading=true;model.loading_text=status.text.substr(0,96);model.loading_progress=status.progress;}
        ui.player.resize(std::max(1,int(area.width)),std::max(1,int(area.height)));ui.player.update(model);
        inventory_second->frame(ui.player,client,*character_resources,area.x,area.y,area.width,area.height,ImGui::IsWindowFocused(),model.loading);
        altar_second->frame(ui.player,client,false,ImGui::IsWindowFocused(),model.loading);
        ui.frame(*client,model,world_ui_camera(shell.game_view_second,shell.game_projection_second,area.width,area.height),ImGui::IsWindowFocused(),model.loading,area.x,area.y,character->players(),ui_actors(*character));
        inventory_second->flush(ui.player,client,*character_resources,model.loading);altar_second->flush(ui.player,client,false,model.loading);ui.service.update();
        if(shell.controls_player==1)shell.inventory_open=inventory_second->open()||altar_second->open()||ui.modal();
        ui.render(draw,area.x,area.y,area.width,area.height);hud_second_drawn=true;
    }catch(const std::exception& error){hud_renderer.end();editor.log(std::string("Second viewport UI unavailable: ")+error.what());}
}

#endif

void PlayController::start(std::size_t skins, const RuntimeSkinnedModel* skin) {
    if (args.character_clips.empty())
        return;
    require(inputs != nullptr, "Character controls require a cooked input profile");
    const char* required[] = {"move.forward", "move.lateral", "look.turn", "walk", "jump"};
    for (unsigned action = 1; action <= 5; ++action) {
        auto definition = std::find_if(inputs->profile().actions.begin(), inputs->profile().actions.end(),
                                       [&](const auto& candidate) { return candidate.id == action; });
        require(definition != inputs->profile().actions.end() && definition->name == required[action - 1] &&
                    definition->kind == (action <= 3 ? InputActionKind::Axis : InputActionKind::Button),
                "Character input semantic contract");
    }
    require(skins == 1, "Character preset requires one loaded skin");
    character_resources = load_resources(*skin);
    shell.native_controls = true;
    if (args.character_clips.starts_with("kit:"))
        editor.authoring_kit = AssetId::parse(args.character_clips.substr(4));
    // A party guest launched after its host prepared Play with a Player gets "player:<id>": it selects that Player, so it can prepare later
    // scenes too.
    else if (args.character_clips.starts_with("player:") && editor.assets) {
        editor.authoring_player = AssetId::parse(args.character_clips.substr(7));
        editor.authoring_kit =
            AssetId::parse(editor.authoring.open(*editor.assets, editor.authoring_player).value.at("loadout").at("kit").get<std::string>());
    }
    if (character_resources->spawns.size() != editor.document->plan().spawns.size())
        editor.log("Some spawn points reference NPCs outside the loaded registry; they join when Play prepares the scene from the project "
                   "sources.",
                   darkangel::editor_app::ConsoleSeverity::Warning);
    editor.prepare_gameplay = [this](const SpawnPlan& plan, const World& world) { prepare_gameplay(plan, world); };
    editor.stop_gameplay = [this] { stop_gameplay(); };
    editor.gameplay_view = [this]() -> const World* { return character ? &character->presentation() : nullptr; };
    editor.update_gameplay = [this](double seconds) { update_gameplay(seconds); };
    editor.step_gameplay = [this] {
        if (character)
            character->step({0, 0, character->motor().yaw});
    };
}

// Every cue asset of the project, cooked and loaded from its frozen product. Cues are not yet in the Play package through the actions and
// effects that name them, so they are packaged on their own; a session started from a cooked registry alone uses the cues in that registry.
std::vector<darkangel::CueDefinition> PlayController::load_play_cues() {
    std::vector<darkangel::CueDefinition> cues;
    try {
        play_cue_closure = 0;
        if (editor.assets) {
            editor.assets->scan();
            std::set<AssetId> held;
            std::set<std::string> keys;
            // Cues the Play package reached through the effects that bind them come first. The project's other cues answer the keys nothing
            // names by asset (action cue blocks, footsteps).
            if (!args.registry.empty() && std::filesystem::is_regular_file(args.registry)) {
                try {
                    for (auto& cue : darkangel::load_cooked_cues(args.registry, args.cas)) {
                        held.insert(cue.id);
                        keys.insert(cue.key);
                        cues.push_back(std::move(cue));
                    }
                } catch (const std::exception& error) {
                    cues.clear();
                    held.clear();
                    keys.clear();
                    editor.log(std::string("The Play package's cues could not be read: ") + error.what(),
                               darkangel::editor_app::ConsoleSeverity::Warning);
                }
            }
            play_cue_closure = cues.size();
            std::vector<AssetId> roots;
            for (const auto& asset : editor.assets->assets())
                if (std::filesystem::path(asset.path).extension() == ".dacue" && !held.contains(asset.id))
                    roots.push_back(asset.id);
            if (roots.size() > 64) {
                editor.log("This project has more than 64 cue assets outside the Play package; Play uses the first 64 of them.",
                           darkangel::editor_app::ConsoleSeverity::Warning);
                roots.resize(64);
            }
            if (!roots.empty()) {
                const auto registry = editor.assets->cas_path().parent_path() / ("cues-" + AssetId::random().text() + ".json");
                ComposerPackageFile cleanup{registry};
                darkangel::editor_app::package_authoring(*editor.assets, roots, registry);
                for (auto& cue : darkangel::load_cooked_cues(registry, editor.assets->cas_path())) {
                    if (keys.insert(cue.key).second)
                        cues.push_back(std::move(cue));
                    else
                        editor.log("Two cues answer to the key " + cue.key + "; " + cue.name + " is not used.",
                                   darkangel::editor_app::ConsoleSeverity::Warning);
                }
            }
        } else
            cues = darkangel::load_cooked_cues(args.registry, args.cas);
        // The weapon trails and decals the cues name, by the identities in the cue products.
        play_trails.clear();
        play_decals.clear();
        if (editor.assets) {
            std::vector<AssetId> primitives;
            for (const auto& cue : cues)
                for (const auto id : {cue.trail_asset, cue.decal_asset})
                    if (id != AssetId{} && std::find(primitives.begin(), primitives.end(), id) == primitives.end())
                        primitives.push_back(id);
            if (!primitives.empty() && primitives.size() <= 64) {
                const auto registry = editor.assets->cas_path().parent_path() / ("primitives-" + AssetId::random().text() + ".json");
                ComposerPackageFile cleanup{registry};
                darkangel::editor_app::package_authoring(*editor.assets, primitives, registry);
                for (auto& trail : darkangel::load_cooked_trails(registry, editor.assets->cas_path()))
                    play_trails[trail.id] = std::move(trail);
                for (auto& decal : darkangel::load_cooked_decals(registry, editor.assets->cas_path()))
                    play_decals[decal.id] = std::move(decal);
            }
        }
        // A cue's visual names an effect source by its path; each is cooked, packaged and loaded into the service.
        play_effects.clear();
        if (vfx && editor.assets) {
            std::vector<AssetId> effects;
            std::vector<std::string> names;
            for (const auto& cue : cues) {
                if (cue.visual.empty() || std::find(names.begin(), names.end(), cue.visual) != names.end())
                    continue;
                bool found = false;
                for (const auto& asset : editor.assets->assets())
                    if (cue.visual_asset != AssetId{} ? asset.id == cue.visual_asset : asset.path == cue.visual) {
                        effects.push_back(asset.id);
                        names.push_back(cue.visual);
                        found = true;
                    }
                if (!found)
                    editor.log("Cue " + cue.name + " names a visual effect that is not in the project: " + cue.visual,
                               darkangel::editor_app::ConsoleSeverity::Warning);
            }
#ifdef DAE_AUDIO
            // A cue's audio names a sound source by its path, the same way.
            play_sounds.clear();
            if (audio_workspace.ensure()) {
                std::vector<AssetId> sounds;
                for (const auto& cue : cues) {
                    if (cue.audio.empty() || play_sounds.contains(cue.audio))
                        continue;
                    AssetId found = cue.audio_asset;
                    if (found == AssetId{})
                        for (const auto& asset : editor.assets->assets())
                            if (asset.path == cue.audio)
                                found = asset.id;
                    if (found == AssetId{}) {
                        editor.log("Cue " + cue.name + " names a sound that is not in the project: " + cue.audio,
                                   darkangel::editor_app::ConsoleSeverity::Warning);
                        continue;
                    }
                    sounds.push_back(found);
                    play_sounds[cue.audio] = found;
                }
                audio_workspace.load(sounds);
            }
#endif
            if (!effects.empty() && effects.size() <= 64) {
                const auto registry = editor.assets->cas_path().parent_path() / ("vfx-" + AssetId::random().text() + ".json");
                ComposerPackageFile cleanup{registry};
                darkangel::editor_app::package_authoring(*editor.assets, effects, registry);
                play_effect_generations.clear();
                for (std::size_t index = 0; index < effects.size(); ++index) {
                    const auto cooked = darkangel::load_cooked_vfx(registry, editor.assets->cas_path(), effects[index]);
                    vfx->load(cooked);
                    play_effects[names[index]] = effects[index];
                    play_effect_generations[names[index]] = cooked.generation.substr(0, 12);
                }
            }
        }
    } catch (const std::exception& error) {
        cues.clear();
        editor.log(std::string("Cues are unavailable for this Play: ") + error.what(), darkangel::editor_app::ConsoleSeverity::Warning);
    }
    return cues;
}

void PlayController::prepare_gameplay(const SpawnPlan& plan, const World& world) {
    navigation_polygons = 0;
    behavior_running.clear();
    hud_target.reset(); // Every scene session of this Play gets its own controller source: behaviours run on the navigation mesh of the
                        // collision that session simulates.
    make_controller = [this] {
        SpawnController controller;
#ifdef DAE_BEHAVIOR
        // A spawned NPC with an authored behaviour runs its tree on the navigation mesh of this session's collision; one without a
        // behaviour stays passive. Held by the controller source itself: an encounter asks it for controllers long after this preparation
        // has returned.
        auto mesh_slot = std::make_shared<std::shared_ptr<const darkangel::NavigationMesh>>();
        controller = [this, mesh_slot](const darkangel::editor_app::PreparedSpawn& spawn,
                                       const darkangel::CollisionDefinition& collision) -> std::shared_ptr<darkangel::ActorController> {
            auto& session_mesh = *mesh_slot;
            if (!spawn.behavior)
                return {};
            if (!session_mesh)
                session_mesh = navigation_->mesh_for(collision);
            navigation_polygons = session_mesh->polygons();
            if (behavior_running.empty())
                behavior_running = spawn.behavior->name;
            return darkangel::make_behavior_controller(*spawn.behavior, session_mesh, spawn.tuning);
        };
#endif
        return controller;
    };
#ifdef DAE_EDITOR_GNS
    if (!args.join.empty())
        make_controller = [] { return SpawnController{}; };
#endif
    auto controller = make_controller();
    companions.clear();
    if (character_resources->combat)
        for (int seat = 1; seat < shell.players; ++seat)
            companions.push_back(darkangel::party_member(unsigned(seat)));
    director_second.reset();
    companion_script.cursor() = 0;
    companion_script.interactions() = 0;
    auto first_scene = std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(
        plan, world, *character_resources,
        controller));
    first_scene->scene = editor.document->root();
    character_player = first_scene->player;
    play_ledger = darkangel::WorldLedger{};
    parked_documents.clear();
    parked_paths.clear();
    globals_scene = authoring_scene = editor.document->root();
    authoring_path = editor.path;
    travel_preview.reset();
    travel_load = TravelLoad{};
    travel_arrivals = travel_failures = travel_overlay_frames = 0;
    travel_failure.clear();
    travel_was_busy = false;
    owner_script.cursor() = 0;
    owner_script.interactions() = 0;
    travel = std::make_unique<darkangel::SceneTransitionCoordinator>(scene_provider, play_ledger,
                                                                     std::vector<StableId>(companions.begin(), companions.end()));
    if (args.travel_hold >= 0)
        travel->minimum_seconds = args.travel_hold;
#ifdef DAE_EDITOR_GNS
    travel->inventory_profile = darkangel::editor_app::local_player_profile(args.local_profile == 4 ? 0 : args.local_profile);
    const bool network_host = args.join.empty() && !exercises::local_party(args) && (shell.players > 1 || !args.listen.empty());
    if (network_host) {
        party_host = std::make_unique<darkangel::editor_app::EditorPartyHost>(
            darkangel::editor_app::local_player_profile(args.local_profile == 4 ? 0 : args.local_profile), unsigned(shell.players),
            args.listen);
        travel->build_session = [this](darkangel::PreparedScene& scene, StableId entry, const darkangel::SceneStart& start,
                                       std::span<const darkangel::ScenePlayer> others) {
            return party_host->build(scene, entry, start, others);
        };
        travel->ready = [this](CharacterSceneSession& session) { return party_host->ready(session); };
        travel->resumed = [this] { party_host->resume(); };
        travel->cancelled = [this](std::string_view reason) { party_host->cancel(reason); };
        travel->construction_text = [this] { return party_host->status(); };
        travel->party_size = [this] { return party_host->members(); };
    }
#endif
    // What hangs off a session is dropped before the coordinator destroys it, and built again for the next one.
    travel->unloading = [this] {
#ifdef DAE_HUD
        runtime_inventory.reset();
        runtime_altar.reset();
        shell.inventory_open = false;
#endif
        hud_target.reset();
        if (character)
            character->present_cues(nullptr);
        cue_player.reset();
        cue_effects.bind(nullptr, nullptr, {});
        cue_primitives.bind(nullptr, nullptr, {});
#ifdef DAE_AUDIO
        cue_sounds.bind(nullptr, nullptr, {});
        audio_director.reset();
#endif
        director.reset();
        director_second.reset();
        character = nullptr;
    };
    travel->loaded = [this] {
        character = travel->session();
        character_player = travel->scene()->player;
        // The Preview world (scripts) of the scene the editor's document now is.
        if (travel_preview && travel->scene()->scene == globals_scene) {
            editor.preview.retire();
            editor.preview.activate(std::move(travel_preview));
        }
        attach_session(editor.document->plan());
    };
#ifdef DAE_EDITOR_GNS
    if (!args.join.empty()) {
        const bool local = args.join.starts_with("127.0.0.1:");
        const auto slot = args.local_profile == 4 ? (local ? args.player : 0) : args.local_profile;
        party_guest = std::make_unique<darkangel::editor_app::EditorPartyGuest>(
            darkangel::editor_app::local_player_profile(slot), args.player, args.join, scene_provider, args.party_mismatch);
        party_guest->unloading = travel->unloading;
        party_guest->loaded = [this] {
            character = party_guest->session();
            character_player = party_guest->owner();
            if (travel_preview) {
                editor.preview.retire();
                editor.preview.activate(std::move(travel_preview));
            }
            attach_session(editor.document->plan());
        };
        party_guest->initial(std::move(first_scene));
        travel.reset();
        shell.client_only = true;
    } else
#endif
    { // Play from the save slot: its ledger and inventories, and the party at its checkpoint (in this scene, or travelling there).
        std::optional<darkangel::WorldDestination> checkpoint;
        if (play_saves && play_saves->play_from_slot) {
            std::string warning;
            try {
                if (auto loaded = play_saves->load(warning)) {
                    play_ledger = std::move(loaded->ledger);
                    travel->restore_inventory(std::move(loaded->inventories));
                    checkpoint = darkangel::read_checkpoint(play_ledger);
                    editor.log("Play starts from the save slot.");
                } else
                    editor.log("The save slot is empty; Play starts fresh.");
            } catch (const std::exception& error) {
                editor.log(std::string("The save could not be loaded: ") + error.what(), darkangel::editor_app::ConsoleSeverity::Error);
            }
            if (!warning.empty())
                editor.log(warning, darkangel::editor_app::ConsoleSeverity::Warning);
        }
        const bool here = checkpoint && checkpoint->scene == first_scene->scene;
        travel->start(std::move(first_scene), here ? checkpoint->altar : StableId{});
        character = travel->session();
        if (checkpoint && !here && !travel->request({checkpoint->scene, checkpoint->altar, "Play from save"}))
            editor.log("The saved checkpoint's scene could not be entered.", darkangel::editor_app::ConsoleSeverity::Warning);
    }
    save_play = [this](std::string reason) {
        if (!play_saves || !travel || !travel->scene() || !character || !character->hosting())
            return;
        character->capture();
        auto* inventory = travel->inventory();
#ifdef DAE_EDITOR_GNS
        if (!inventory && party_host)
            inventory = party_host->inventory();
#endif
        if (play_saves->save(darkangel::editor_app::encode_play_save(play_ledger, inventory, travel->scene()->scene, {}),
                             std::move(reason)))
            editor.log("Saving the game.");
    };
    if (character)
        attach_session(plan);
#ifdef DAE_EDITOR_GNS
    if (party_host) {
        std::vector<std::string> child{
            "--registry",      args.registry.string(), "--cas",     args.cas.string(), "--model",           args.model,
            "--scene",         editor.path.string(),   "--backend", args.backend,      "--character-clips", args.character_clips,
            "--game-workspace"};
        if (!args.sources.empty())
            child.insert(child.end(), {"--sources", args.sources.string(), "--asset-cache", args.asset_cache.string()});
        if (!args.character_collision.empty())
            child.insert(child.end(), {"--character-collision", args.character_collision});
        if (!args.character_player.empty())
            child.insert(child.end(), {"--character-player", args.character_player});
        if (!args.player_asset.empty())
            child.insert(child.end(), {"--player-asset", args.player_asset});
        if (!args.input_script.empty())
            child.insert(child.end(), {"--player-script", args.input_script});
        if (args.hidden)
            child.push_back("--hidden");
        if (args.no_audio)
            child.push_back("--no-audio");
        if (args.party_mismatch)
            child.push_back("--party-mismatch");
        for (unsigned seat = 1; seat < unsigned(shell.players); ++seat) {
            auto arguments = child;
            if (!args.capture.empty())
                arguments.insert(arguments.end(),
                                 {"--capture", (args.capture.parent_path() / (args.capture.stem().string() + "-player-" +
                                                                              std::to_string(seat) + args.capture.extension().string()))
                                                   .string()});
            if (!args.party_report.empty())
                arguments.insert(arguments.end(),
                                 {"--party-report", (args.party_report.parent_path() /
                                                     (args.party_report.stem().string() + "-player-" + std::to_string(seat) + ".json"))
                                                        .string()});
            party_host->launch(seat, std::move(arguments),
                               std::filesystem::path(".cache") / "party" / ("player-" + std::to_string(seat) + ".log"));
        }
        editor.log("Party host listening at " + party_host->address());
    }
#endif
    editor.log(
        character_resources->combat
            ? "Native Loopback combat Play: light attack uses the cooked kit, action and authoritative target damage. Click Game for input."
            : "Native Loopback character Play: locomotion and solid box collision proxies. Use Game to play; click its viewport for "
              "input.");
}

// The session's cue presentation: an effect cue names its cue asset; an action cue block only has a key.
void PlayController::attach_session(const SpawnPlan& plan) {
    cue_player.reset();
    cue_effects.bind(nullptr, nullptr, {});
    cue_primitives.bind(nullptr, nullptr, {});
#ifdef DAE_AUDIO
    cue_sounds.bind(nullptr, nullptr, {});
#endif
    cue_visual.clear();
    cue_audio.clear();
    if (character_resources->combat) {
        if (!play_cues)
            play_cues = load_play_cues();
        cue_player = std::make_unique<darkangel::CuePlayer>(
            [this](std::string_view key, AssetId cue) -> std::optional<darkangel::CueDefinition> {
                for (const auto& known : *play_cues)
                    if (cue != AssetId{} && known.id == cue)
                        return known;
                for (const auto& known : *play_cues)
                    if (known.key == key)
                        return known;
                return std::nullopt;
            },
            &cue_effects,
#ifdef DAE_AUDIO
            &cue_sounds
#else
            &cue_audio
#endif
        );
        character->present_cues(cue_player.get());
        cue_primitives.bind(&play_trails, &play_decals, [this](std::uint64_t network) -> std::optional<std::array<float, 3>> {
            if (!character)
                return std::nullopt;
            const auto identity = character->identity(network);
            if (!identity)
                return std::nullopt;
            const auto handle = character->presentation().find(*identity);
            if (!character->presentation().valid(handle))
                return std::nullopt;
            const auto& at = character->presentation().read(handle).transform;
            return std::array<float, 3>{float(at.x), float(at.y), float(at.z)};
        });
        cue_effects.bind(vfx.get(), &play_effects, [this](std::uint64_t network) -> std::optional<std::array<float, 3>> {
            if (!character)
                return std::nullopt;
            const auto identity = character->identity(network);
            if (!identity)
                return std::nullopt;
            const auto handle = character->presentation().find(*identity);
            if (!character->presentation().valid(handle))
                return std::nullopt;
            const auto& at = character->presentation().read(handle).transform;
            return std::array<float, 3>{float(at.x), float(at.y) + 1.f, float(at.z)};
        });
        vfx_tick = 0;
#ifdef DAE_AUDIO
        cue_sounds.bind(audio.get(), &play_sounds, [this](std::uint64_t network) -> std::optional<std::array<float, 3>> {
            if (!character)
                return std::nullopt;
            const auto identity = character->identity(network);
            if (!identity)
                return std::nullopt;
            const auto handle = character->presentation().find(*identity);
            if (!character->presentation().valid(handle))
                return std::nullopt;
            const auto& at = character->presentation().read(handle).transform;
            return std::array<float, 3>{float(at.x), float(at.y) + 1.f, float(at.z)};
        });
        // The scene's audio profile starts with Play; an encounter's is layered over it while the encounter runs.
        audio_director.reset();
        play_profiles.clear();
        encounter_profiles.clear();
        audio_tab.profile_audition = false;
        audio_in_play = true;
        if (audio) {
            std::vector<AssetId> wanted;
            for (const auto& [object, profile] : plan.audio_profiles)
                wanted.push_back(profile);
            for (const auto& [object, component] : plan.encounters) {
                try {
                    if (const auto profile = darkangel::decode_encounter(component).audio_profile; profile != AssetId{}) {
                        wanted.push_back(profile);
                        encounter_profiles[object] = profile;
                    }
                } catch (const std::exception&) {
                }
            }
            try {
                play_profiles = audio_workspace.load(wanted);
                for (const auto profile : wanted)
                    if (!play_profiles.contains(profile))
                        editor.log("An audio profile named by the scene is not in the project: " + profile.text(),
                                   darkangel::editor_app::ConsoleSeverity::Warning);
            } catch (const std::exception& error) {
                editor.log(std::string("Audio profiles are unavailable for this Play: ") + error.what(),
                           darkangel::editor_app::ConsoleSeverity::Warning);
            }
            audio_director = std::make_unique<darkangel::AudioDirector>(*audio, audio_workspace.project_mix());
            audio_applied = audio_workspace.project_mix();
            if (!plan.audio_profiles.empty())
                if (const auto found = play_profiles.find(plan.audio_profiles.begin()->second); found != play_profiles.end())
                    audio_director->scene(found->second);
        }
#else
        (void)plan;
#endif
    }
}

void PlayController::stop_gameplay() {
#ifdef DAE_HUD
    runtime_inventory.reset();
    runtime_altar.reset();
    shell.inventory_open = false;
    // Stop releases RmlUi, so the UI tab's preview can take it again; the next Play makes a fresh HUD.
    hud=nullptr;play_ui.reset();play_ui_second.reset();inventory_second.reset();altar_second.reset();ui_resources=nullptr;frontend.reset();
    hud_failed = false;
#endif
    hud_target.reset();
    director.reset();
    shell.camera_live.clear();
    if (travel)
        travel->stop();
    travel.reset();
#ifdef DAE_EDITOR_GNS
    if (party_guest)
        party_guest->stop();
    party_guest.reset();
    if (party_host)
        party_host->stop();
    party_host.reset();
#endif
    character = nullptr;
    travel_preview.reset();
    travel_load = TravelLoad{};
    if (authoring_scene != AssetId{} && globals_scene != authoring_scene)
        if (auto parked = parked_documents.find(authoring_scene); parked != parked_documents.end()) {
            editor.document = std::move(parked->second);
            editor.path = authoring_path;
            editor.selected = {};
        }
    parked_documents.clear();
    parked_paths.clear();
    globals_scene = authoring_scene;
    cue_player.reset();
#ifdef DAE_AUDIO
    audio_director.reset();
    play_profiles.clear();
    encounter_profiles.clear();
    audio_in_play = false;
#endif
    cue_effects.bind(nullptr, nullptr, {});
#ifdef DAE_AUDIO
    cue_sounds.bind(nullptr, nullptr, {});
#endif
    shell.controls_focus = false;
}

void PlayController::update_gameplay(double seconds) {
    if (!character)
        return;
// While the party travels its input is held.
#ifdef DAE_EDITOR_GNS
    if (party_guest && !party_guest->running()) {
        character->pump();
        return;
    }
    if (party_host && travel && travel->busy()) {
        character->pump();
        return;
    }
#endif
    // A long editor stall (cooking at Play start, a debugger) is not simulated time: the session catches up at most a quarter second.
    const double stalled = std::min(seconds, .25);
    if (travel && travel->busy()) {
        CharacterSceneInput held;
        held.yaw = character->predicted_motor().yaw;
        character->advance(stalled, held);
    } else
        character->advance(stalled, character_input);
}

// The open scene's static collision as Play would build it now: its authored collision, or the placements' static layer with each
// model's cooked shape (a model not in the prepared package yet has none until Play resources are prepared again). Rebuilt when
// the document changes, from the shared geometry cache, so no shape is built twice.
std::shared_ptr<const darkangel::CollisionDefinition> PlayController::static_collision() {
    if (!character_resources)
        return nullptr;
    if (character_resources->collision)
        return character_resources->collision;
    if (!character_resources->model_collision)
        return nullptr;
    if (!scene_static_ || scene_static_document_ != editor.document.get() || scene_static_revision_ != editor.document->revision() ||
        scene_static_resources_ != character_resources.get()) {
        auto definition = std::make_shared<darkangel::CollisionDefinition>();
        definition->id = AssetId::random();
        definition->schema = 3;
        const auto& source = character_resources->model_collision;
        definition->statics = darkangel::scene_static_collision(
            editor.document->plan(), editor.document->world(), character_resources->skin,
            [&](AssetId model) -> std::shared_ptr<const darkangel::CollisionGeometry> {
                try {
                    return source(model);
                } catch (const std::exception&) {
                }
                // A model placed since Play resources were prepared lives in its own package.
                if (const auto package = editor.model_packages.find(model); package != editor.model_packages.end() && editor.assets)
                    try {
                        const auto data = darkangel::load_cooked_model_collision(package->second, editor.assets->cas_path(), model);
                        return data ? package_geometry_.get(data) : nullptr;
                    } catch (const std::exception&) {
                    }
                return nullptr;
            });
        scene_static_ = std::move(definition);
        scene_static_document_ = editor.document.get();
        scene_static_revision_ = editor.document->revision();
        scene_static_resources_ = character_resources.get();
    }
    return scene_static_;
}

void PlayController::install_preparation() {
    if (character_resources && editor.assets && editor.authoring_kit != AssetId{}) {
        editor.authoring_roots = [this] {
            const auto id = editor.model;
            std::set<AssetId> unique{id, editor.authoring_player == AssetId{} ? editor.authoring_kit : editor.authoring_player};
            for(auto ui:ashen_roots::core_ui_roots())unique.insert(ui);
            for(const auto& [object,text]:editor.document->plan().interactables){auto d=decode_interactable(text);if(d.puzzle)unique.insert(AssetId::parse(d.puzzle->document));}
            for (auto npc : encounter_npcs())
                unique.insert(npc);
            for (const auto& [point, npc] : editor.document->plan().spawns) {
                unique.insert(npc);
                for (auto visual : editor.authoring.worn_visuals(*editor.assets, npc))
                    unique.insert(visual);
            }
            if (editor.authoring_player != AssetId{})
                for (auto visual : editor.authoring.worn_visuals(*editor.assets, editor.authoring_player))
                    unique.insert(visual);
            for (const auto& [object, asset] : editor.document->plan().models)
                unique.insert(asset);
            for(const auto& [object,slots]:editor.document->plan().materials)for(const auto& [slot,material]:slots)unique.insert(material);
            for (const auto& asset : editor.asset_inventory)
                if (std::filesystem::path(asset.path).extension() == ".daitem") {
                    unique.insert(asset.id);
                    try {
                        const auto& item = editor.authoring.open(*editor.assets, asset.id).value;
                        if (item.contains("properties") && item.at("properties").contains("mask") &&
                            item.at("properties").at("mask").is_string()) {
                            const auto& mask =
                                editor.authoring.open(*editor.assets, AssetId::parse(item.at("properties").at("mask").get<std::string>()))
                                    .value;
                            if (mask.contains("visual") && mask.at("visual").is_string())
                                unique.insert(AssetId::parse(mask.at("visual").get<std::string>()));
                        }
                    } catch (const std::exception&) {
                    }
                }
            if (travel_preparing)
                for (const auto& [asset, unused] : scene_models_)
                    unique.insert(asset);
            if (!args.character_collision.empty())
                unique.insert(AssetId::parse(args.character_collision));
            for (const auto& [object, profile] : editor.document->plan().collision_profiles)
                unique.insert(profile);
            return std::vector<AssetId>(unique.begin(), unique.end());
        };
        editor.prepare_play_resources = [this] {
            require(travel_preparing || !editor.preview.active(), "Stop Play before preparing authored resources");
            if (!travel_preparing)
                play_cues.reset();
            const auto started = std::chrono::steady_clock::now();
            auto roots = editor.authoring_roots();
            auto candidate = editor.assets->cas_path().parent_path() / ("play-" + AssetId::random().text() + ".json");
#ifdef DAE_EDITOR_GNS
            if (!args.join.empty()) {
                require(!roots.empty(), "Guest scene has no package roots");
                editor.assets->package(roots.front(), candidate, std::span<const AssetId>(roots).subspan(1));
            } else
#endif
                darkangel::editor_app::package_authoring(*editor.assets, roots, candidate);
            // Static collision needs no bake: the package holds every placed model's cooked collision shape, and host and guests
            // each build the same static layer from the scene when Play starts.
            validate_cooked_material_bindings(candidate,editor.assets->cas_path(),editor.document->plan());
            const auto packaged = std::chrono::steady_clock::now();
            auto old_registry = args.registry, old_cas = args.cas;
            auto old_clips = args.character_clips;
            try {
                args.registry = candidate;
                args.cas = editor.assets->cas_path();
                args.character_clips = editor.authoring_player == AssetId{} ? "kit:" + editor.authoring_kit.text()
                                                                            : "player:" + editor.authoring_player.text();
                reload_model_();
                if (editor.authoring_player != AssetId{})
                    editor.authoring_kit = character_resources->combat->kit->id;
            } catch (...) {
                args.registry = old_registry;
                args.cas = old_cas;
                args.character_clips = old_clips;
                throw;
            }
            editor.refresh_assets();
            editor.model_packages.clear();
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            editor.log("Prepared current scene and combat kit for a fresh Play session (" + std::to_string(roots.size()) + " roots; package " +
                       std::to_string(int(ms(started, packaged))) + " ms, resources " +
                       std::to_string(int(ms(packaged, std::chrono::steady_clock::now()))) + " ms).");
        };
    }
}

std::filesystem::path PlayController::find_scene(AssetId scene) {
    if (!editor.assets)
        return {};
    std::vector<std::filesystem::path> files;
    std::error_code failed;
    for (std::filesystem::recursive_directory_iterator file(editor.assets->source_root(), failed), end; !failed && file != end;
         file.increment(failed))
        if (file->is_regular_file() && file->path().extension() == ".dascene")
            files.push_back(file->path());
    for (const auto& file : files) {
        try {
            std::ifstream input(file);
            const auto value = nlohmann::json::parse(input);
            if (value.contains("root") && value.at("root").is_string() && value.at("root").get<std::string>() == scene.text())
                return file;
        } catch (const std::exception&) {
        }
    }
    return {};
}
void PlayController::enter_scene(AssetId scene, std::unique_ptr<darkangel::EditorDocument> document, const std::filesystem::path& path) {
    require(bool(editor.prepare_play_resources),
            "This editor session cannot prepare another scene for Play: it was started without project sources");
    parked_documents[globals_scene] = std::move(editor.document);
    parked_paths[globals_scene] = editor.path;
    editor.document = std::move(document);
    editor.path = path;
    globals_scene = scene;
    editor.selected = {};
    travel_preparing = true;
    try {
        editor.prepare_play_resources();
        editor.prepare_render_materials(editor.document->plan());
    } catch (...) {
        travel_preparing = false;
        throw;
    }
    travel_preparing = false;
    auto scripts = editor.document->scripts();
    travel_preview = editor.preview.prepare(editor.document->plan(), scripts, [&](AssetId asset) {
        if (editor.prepare_model)
            editor.prepare_model(asset);
    });
}
void PlayController::drop_load() {
    if (travel_load.document && travel_load.parked) {
        parked_paths[travel_load.scene] = travel_load.path;
        parked_documents[travel_load.scene] = std::move(travel_load.document);
    }
    travel_load = TravelLoad{};
}
darkangel::SceneLoadStatus PlayController::poll_load() {
    auto& load = travel_load;
    try {
        if (load.step == 0) {
            if (auto parked = parked_documents.find(load.scene); parked != parked_documents.end()) {
                load.document = std::move(parked->second);
                load.path = parked_paths[load.scene];
                load.parked = true;
                parked_documents.erase(parked);
            } else {
                load.path = find_scene(load.scene);
                if (load.path.empty())
                    return {darkangel::SceneLoad::Failed, 0, "No scene with the identity " + load.scene.text() + " is in the project"};
                load.document = darkangel::EditorDocument::open(load.path, editor.prefab_links());
                require(load.document->root() == load.scene, "The scene file names another scene");
            }
            load.step = 1;
            return {darkangel::SceneLoad::Working, .25, "Opening " + load.path.filename().string()};
        }
        if (load.step == 1) {
            load.parked = false;
            enter_scene(load.scene, std::move(load.document), load.path);
            load.step = 2;
            return {darkangel::SceneLoad::Working, .7, "Preparing " + load.path.filename().string()};
        }
        if (load.step == 2) {
            require(bool(travel_preview), "The destination has no Preview world");
            load.prepared = std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(
                travel_preview->plan, *travel_preview->world, *character_resources, make_controller()));
            load.prepared->scene = load.scene;
            load.step = 3;
        }
        return {darkangel::SceneLoad::Ready, 1, "Entering " + load.path.filename().string()};
    } catch (const std::exception& error) {
        return {darkangel::SceneLoad::Failed, 0, error.what()};
    }
}

PlayController::Reload PlayController::prepare_reload(std::size_t skins, const RuntimeSkinnedModel* skin) {
    Reload reload;
    auto& preview_candidate = reload.resources;
    if (character_resources) {
        require(skins == 1, "Character reload requires one skin");
        preview_candidate = load_resources(*skin);
    }
    auto& input_candidate = reload.inputs;
    auto& native_candidate = reload.native;
    if (preview_candidate && preview_candidate->combat) {
        const auto& profile = preview_candidate->combat->input;
        const char* semantics[] = {"move.forward", "move.lateral", "look.turn", "walk", "jump"};
        for (unsigned i = 1; i <= 5; ++i) {
            auto action = std::find_if(profile.actions.begin(), profile.actions.end(), [&](const auto& a) { return a.id == i; });
            require(action != profile.actions.end() && action->name == semantics[i - 1] &&
                        action->kind == (i <= 3 ? InputActionKind::Axis : InputActionKind::Button),
                    "Play kit input semantic contract mismatch");
        }
        input_candidate = std::make_unique<InputManager>(profile);
        auto controls = input_candidate->controls();
        native_candidate = std::make_unique<darkangel::editor_app::NativeInput>(controls);
    }
    if (preview_candidate) {
        auto probe = darkangel::editor_app::prepare_character_preview(
            editor.document->plan(), editor.document->world(), *preview_candidate);
    }
    return reload;
}
void PlayController::commit_reload(Reload reload) {
    if (character && !travel_preparing)
        editor.stop();
    if (reload.resources)
        character_resources = std::move(reload.resources);
    if (reload.inputs) {
        inputs = std::move(reload.inputs);
        native_input = std::move(reload.native);
        window_input = native_input.get();
        shell.input = inputs.get();
        shell.input_events.clear();
        combat_events.clear();
        shell.controls_focus = false;
    }
}

void PlayController::launch(unsigned frame) {
    // Gate authoring: world objects placed through the Scene tab's own paths and saved, before Play starts.
    if (!args.world_place.empty() && frame == 0) {
        std::ifstream plan(args.world_place);
        require(bool(plan), "--world-place plan missing");
        darkangel::editor_app::place_world_plan(editor, shell, nlohmann::json::parse(plan));
        if (editor.prepare_play_resources)
            editor.prepare_play_resources();
    }
    if (args.play_from_save && frame == 0 && play_saves)
        play_saves->play_from_slot = true;
    if ((args.play || exercises::starts_play(args) || !args.join.empty() || !args.listen.empty()) && frame == 0)
        editor.play();
}

void PlayController::poll_input(unsigned frame) {
    if (native_input) {
        auto raw = native_input->poll();
        bool allow = shell.workspace == darkangel::editor_app::Workspace::Game && shell.controls_focus && !shell.controls_acquired &&
                     editor.preview.active() && !editor.paused;
        if (darkangel::editor_app::exercises::fixed_step_play(args)) {
            raw.time_us = std::uint64_t(frame + 1) * 16667;
            raw.samples.clear();
            for (auto control : inputs->controls())
                raw.samples.push_back({control, darkangel::editor_app::exercises::synthetic_control_down(args, frame, control) ? 1.f : 0.f,
                                       raw.time_us, false});
            allow = shell.workspace == darkangel::editor_app::Workspace::Game && editor.preview.active() && !editor.paused;
        }
        if (raw.overflow) {
            allow = false;
            editor.log("Input queue overflow; controls cancelled until neutral.", darkangel::editor_app::ConsoleSeverity::Warning);
        }
        auto input_start = std::chrono::steady_clock::now();
        InputBatch batch;
        try {
            batch = inputs->update(raw.time_us, raw.samples, allow);
        } catch (const std::exception& error) {
            batch = inputs->update(raw.time_us, {}, false);
            allow = false;
            editor.stop();
            editor.log(error.what(), darkangel::editor_app::ConsoleSeverity::Error);
        }
        shell.input_processing_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - input_start).count();
        for (const auto& event : batch.events) {
            if (character_resources && character_resources->combat) {
                bool routed = false;
                for (auto action : character_resources->combat->loadout_actions)
                    routed |= action && action == event.action;
                for (const auto& slot : character_resources->combat->kit->slots)
                    routed |= slot.input_action && slot.input_action == event.action;
                for (const auto& loadout : character_resources->combat->loadouts)
                    if (loadout)
                        for (const auto& slot : loadout->kit->slots)
                            routed |= slot.input_action && slot.input_action == event.action;
                if (routed)
                    combat_events.push_back(event);
            }
            if (event.action == 5 && event.edge == InputEdge::Pressed)
                shell.jump = true;
            if (event.action == 7 && event.edge == InputEdge::Pressed)
                interact_pressed = true;
            if (event.edge != InputEdge::Hold) {
                shell.input_events.push_back(event);
                if (shell.input_events.size() > 48)
                    shell.input_events.erase(shell.input_events.begin());
            }
        }
        interact_held = allow && inputs->state(7).down;
        if (allow) {
            shell.forward = inputs->state(1).value;
            shell.lateral = inputs->state(2).value;
            shell.turn = inputs->state(3).value;
            shell.walk = inputs->state(4).down;
        }
    }
}

void PlayController::frame(unsigned frame, double elapsed_seconds) {
    auto& script = companion_script.steps();
    auto& player_script = owner_script.steps();
    auto& script_at = companion_script.cursor();
    auto& player_script_at = owner_script.cursor();
    auto& script_capture = script_signals.capture;
    if (!args.character_clips.empty()) {
        character_input = {};
        if (character) {
            double forward = shell.forward, lateral = shell.lateral;
            auto length = std::hypot(forward, lateral);
            if (length > 1) {
                forward /= length;
                lateral /= length;
            }
            double scale = shell.walk ? .4 : 1;
            const double look_step = darkangel::editor_app::exercises::fixed_step_play(args) ? 1. / 60 : std::min(elapsed_seconds, .1);
            if (!director)
                director = std::make_unique<CameraDirector>(character_resources->cameras, character->predicted_motor().yaw);
            // Look input orbits the camera: stick axes through semantic actions, mouse while the viewport has control.
            double look_yaw = shell.turn * look_step * 2, look_pitch = 0;
            for (const auto& action : inputs->profile().actions)
                if (action.name == "look.pitch" && action.kind == InputActionKind::Axis)
                    look_pitch = inputs->state(action.id).value * look_step * 1.5;
            ImVec2 mouse_delta{};
            if (shell.controls_focus && !args.hidden)
                mouse_delta = ImGui::GetIO().MouseDelta;
            if (const auto scripted = darkangel::editor_app::exercises::scripted_look(args, frame))
                mouse_delta = {(*scripted)[0], (*scripted)[1]};
            look_yaw -= mouse_delta.x * .0035;
            look_pitch += mouse_delta.y * .0035;
            director->orbit(look_yaw, look_pitch);
            camera_held.clear();
            camera_tags.clear();
            for (const auto& camera : director->rig().cameras)
                if (!camera.input_action.empty())
                    for (const auto& action : inputs->profile().actions)
                        if (action.name == camera.input_action && inputs->state(action.id).down &&
                            std::find(camera_held.begin(), camera_held.end(), action.name) == camera_held.end())
                            camera_held.push_back(action.name);
            if (character->ability())
                camera_tags = character->ability()->tags.values;
            const auto& live_camera = director->rig().cameras[director->select(camera_held, camera_tags)];
            const double camera_yaw = director->yaw();
            const double move_x = scale * (lateral * std::cos(camera_yaw) + forward * std::sin(camera_yaw)),
                         move_z = scale * (forward * std::cos(camera_yaw) - lateral * std::sin(camera_yaw));
            // Free-look turns the character toward its movement; an aim camera keeps it facing with the camera.
            double yaw = character->predicted_motor().yaw, aim_pitch = 0;
            if (live_camera.facing == CameraFacing::Camera) {
                yaw = camera_yaw;
                // Crosshair convergence: aim from the muzzle of the kit's aimed ability at what the screen centre is on.
                double aim_range = 60;
                MotorVec muzzle{0, 1.3, .6};
                if (character_resources->combat)
                    for (const auto& ability : character_resources->combat->abilities)
                        if (ability->aim && !ability->melee.empty()) {
                            aim_range = ability->aim->range;
                            muzzle = {ability->melee[0].offset[0], ability->melee[0].offset[1], ability->melee[0].offset[2]};
                            break;
                        }
                const auto& eye = director->pose();
                const MotorVec toward{eye.target[0] - eye.position[0], eye.target[1] - eye.position[1], eye.target[2] - eye.position[2]};
                if (std::hypot(std::hypot(toward.x, toward.y), toward.z) > 1e-6) {
                    const auto solved = character->aim({eye.position[0], eye.position[1], eye.position[2]}, toward, aim_range, muzzle);
                    yaw = solved.yaw;
                    aim_pitch = solved.pitch;
                }
            } else if (std::hypot(move_x, move_z) > .05) {
                const double difference = std::remainder(std::atan2(move_x, move_z) - yaw, 6.283185307179586), step = 14 * look_step;
                yaw = std::remainder(yaw + std::clamp(difference, -step, step), 6.283185307179586);
            }
            character_input = {move_x, move_z, yaw, shell.jump, false};
            character_input.pitch = aim_pitch;
            character_input.combat_events = std::move(combat_events);
            // The interact input asks for the interactable the HUD offers; the server decides.
            if (character && character->interaction_prompt())
                interaction_offered = true;
            if (interact_pressed) {
                interact_pressed = false;
                if (const auto offered = character->interaction_prompt()) {
                    character_input.interact = offered->id;
                    ++interactions_asked;
                }
            } else if (interact_held)
                if (const auto offered = character->interaction_prompt(); offered && offered->hold)
                    character_input.interact = offered->id;
            combat_events.clear();
        } else
            combat_events.clear();
        if (shell.inventory_open && character)
            character_input = {0, 0, character->predicted_motor().yaw};
        // Scripts pause while the party travels; await-scene waits for the party's n-th arrival.
        script_capture = false;
        bool travelling_now = travel && travel->busy();
#ifdef DAE_EDITOR_GNS
        travelling_now |= party_guest && !party_guest->running();
#endif
        auto run_script = [this, travelling_now, frame](darkangel::editor_app::exercises::ScriptRunner& runner,
                                                        const darkangel::SceneClient& who, CharacterSceneInput& out) {
            darkangel::editor_app::exercises::ScriptContext context{*character,      who, script_signals, travelling_now, travel_arrivals,
                                                                    travel_failures, {}};
#ifdef DAE_HUD
            context.menu = [this](std::string_view verb, unsigned value) {
                if (!hud)
                    return false;
                if(verb.starts_with("ui-")){
                    if(!play_ui)return false;
                    if(verb=="ui-await")return play_ui->service.modal_open();
                    if(verb=="ui-next")return play_ui->service.key(UiKey::Down);
                    if(verb=="ui-confirm")return play_ui->service.key(UiKey::Confirm);
                    if(verb=="ui-cancel")return play_ui->service.key(UiKey::Cancel);
                    if(verb=="ui-solved"){auto* c=character->client(character_player);const auto* state=c?c->world_state():nullptr;if(!state||state->puzzles.empty()||!std::all_of(state->puzzles.begin(),state->puzzles.end(),[](const auto& p){return p.solved;}))return false;std::cout<<"Script UI solved seat="<<args.player<<std::endl;return true;}
                    return false;
                }
                if (verb.starts_with("inventory-"))
                    return runtime_inventory.exercise(verb, value, *hud, character->client(character_player), *character_resources);
                return runtime_altar.exercise(verb, value, *hud, character->client(character_player), character->hosting());
            };
#endif
            runner.frame(context, frame, out);
        };
        if (character && player_script_at < player_script.size()) {
            character_input = {0, 0, character->predicted_motor().yaw};
            run_script(owner_script, *character->client(character_player), character_input);
        }
        // A second local player: a script when one was given (gates), otherwise the keyboard and mouse while its viewport has control.
        if (character && character->players().size() > 1 && character->players()[1] != character_player &&
            character->client(character->players()[1])) {
            const auto second_id = character->players()[1];
            const auto* second = character->client(second_id);
            CharacterSceneInput other;
            other.yaw = second->predicted_motor().yaw;
            if (travelling_now) {
            } else if (script_at < script.size())
                run_script(companion_script, *second, other);
            else if (shell.controls_player == 1 && shell.controls_focus && director_second) {
                // The focused viewport's player takes the keyboard and mouse; the first player stands still meanwhile.
                if (!args.hidden) {
                    const auto moved = ImGui::GetIO().MouseDelta;
                    director_second->orbit(-moved.x * .0035, moved.y * .0035);
                }
                double forward = shell.forward, lateral = shell.lateral;
                const auto length = std::hypot(forward, lateral);
                if (length > 1) {
                    forward /= length;
                    lateral /= length;
                }
                const double facing = director_second->yaw();
                other.x = lateral * std::cos(facing) + forward * std::sin(facing);
                other.z = forward * std::cos(facing) - lateral * std::sin(facing);
                if (std::hypot(other.x, other.z) > .05)
                    other.yaw = std::atan2(other.x, other.z);
                other.jump = character_input.jump;
                other.combat_events = std::move(character_input.combat_events);
                if (character_input.interact) {
                    character_input.interact.reset();
                    if (const auto offered = second->interaction_prompt())
                        other.interact = offered->id;
                }
                character_input = {0, 0, character->predicted_motor().yaw};
            }
            character->input(second_id, std::move(other));
        }
#ifdef DAE_EDITOR_GNS
        if (party_host) {
            party_host->pump(std::min(elapsed_seconds, .1), character,
                             travel && travel->busy() && travel->status().phase < darkangel::TransitionPhase::Swapping);
            for (const auto& drop : party_host->drain_drops()) {
                std::cout << "Party host dropped seat=" << drop.seat << " reason=" << drop.reason << std::endl;
                editor.log("Player " + std::to_string(drop.seat) + ": " + drop.reason, darkangel::editor_app::ConsoleSeverity::Warning);
            }
        }
        if (party_guest) {
            party_guest->pump();
            shell.client_status = party_guest->status();
            // A guest counts its entries into a scene as the host counts the party's arrivals.
            if (party_guest->running() && party_guest->epoch() != guest_arrived_epoch) {
                guest_arrived_epoch = party_guest->epoch();
                ++travel_arrivals;
            }
            if (party_guest->finished())
                close_window_();
        }
#endif
#ifdef DAE_EDITOR_GNS
        if (frame % 60 == 0)
            party_receipt(frame);
        // A scripted party run does not idle after its Play was stopped by an error.
        if (!args.party_report.empty() && travel_arrivals && !editor.preview.active() && (!player_script.empty() || !script.empty())) {
            party_receipt(frame);
            throw std::runtime_error("Party Play stopped: " +
                                     (editor.console.empty() ? std::string("no reason given") : editor.console.back().message));
        }
#endif
        editor.update(darkangel::editor_app::exercises::fixed_step_play(args) ? 1. / 60 : elapsed_seconds);
        // Party travel: the coordinator takes what the session's triggers raised and advances a running transition one phase a frame.
        if (travel && editor.preview.active() && !editor.paused) {
            try {
                travel->update(darkangel::editor_app::exercises::fixed_presentation_step(args) ? 1. / 60 : std::min(elapsed_seconds, .1));
            } catch (const std::exception& error) {
                editor.log(std::string("Scene transition: ") + error.what(), darkangel::editor_app::ConsoleSeverity::Error);
            }
            if (travel_was_busy && !travel->busy()) {
                if (const auto& ended = travel->outcome()) {
                    if (ended->arrived) {
                        ++travel_arrivals;
                        // A scripted walk that carried a player into the transition ends with it.
                        if (player_script_at < player_script.size() && player_script[player_script_at].verb == "walk-to")
                            ++player_script_at;
                        if (script_at < script.size() && script[script_at].verb == "walk-to")
                            ++script_at;
                        editor.log("The party arrived in " + editor.path.filename().string() + ".");
                        if (ended->save && save_play)
                            save_play("Rested at an altar");
                    } else {
                        ++travel_failures;
                        travel_failure = ended->failure;
                        editor.log("The party did not travel: " + ended->failure, darkangel::editor_app::ConsoleSeverity::Warning);
                    }
                }
            }
            travel_was_busy = travel->busy();
            // The save worker reports when a write has finished; the console says what happened.
            if (play_saves) {
                const auto before = play_saves->status();
                play_saves->poll();
                if (play_saves->status() != before && !play_saves->status().starts_with("Saving"))
                    editor.log(play_saves->status());
            }
            if (travel->fatal()) {
                const auto why = travel->outcome() ? travel->outcome()->failure : std::string("Play cannot continue");
                editor.stop();
                editor.log(why, darkangel::editor_app::ConsoleSeverity::Error);
            }
            // A transition that did not arrive can leave the editor prepared for the destination: it follows the party back.
            else if (!travel->busy() && travel->scene() && globals_scene != travel->scene()->scene) {
                const auto back = travel->scene()->scene;
                try {
                    auto parked = parked_documents.find(back);
                    require(parked != parked_documents.end(), "The scene the party is in is no longer open");
                    auto document = std::move(parked->second);
                    const auto file = parked_paths[back];
                    parked_documents.erase(parked);
                    enter_scene(back, std::move(document), file);
                    editor.preview.retire();
                    editor.preview.activate(std::move(travel_preview));
                    travel->unloading();
                    character = travel->session();
                    attach_session(editor.document->plan());
                } catch (const std::exception& error) {
                    const std::string why = error.what();
                    editor.stop();
                    editor.log("The scene the party is in could not be shown again: " + why, darkangel::editor_app::ConsoleSeverity::Error);
                }
            }
        }
    }
}

const CameraPose* PlayController::game_camera(double elapsed_seconds) {
    if (!character)
        director.reset();
    if (character && editor.preview.active()) {
        const auto& state = character->motor();
        if (!director)
            director = std::make_unique<CameraDirector>(character_resources->cameras, state.yaw);
        const auto& camera =
            director->update(darkangel::editor_app::exercises::fixed_step_play(args) ? 1. / 60 : std::min(elapsed_seconds, .1),
                             CameraTargets{{state.position.x, state.position.y, state.position.z}, std::nullopt}, camera_held, camera_tags);
        shell.camera_live = director->rig().cameras[director->live()].name;
        return &camera;
    }
    return nullptr;
}
const CameraPose& PlayController::second_camera(double elapsed_seconds) {
    const auto* second = character->client(character->players()[1]);
    const auto& walker = second->predicted_motor();
    if (!director_second)
        director_second = std::make_unique<CameraDirector>(character_resources->cameras, walker.yaw);
    return director_second->update(darkangel::editor_app::exercises::fixed_presentation_step(args) ? 1. / 60
                                                                                                   : std::min(elapsed_seconds, .1),
                                   CameraTargets{{walker.position.x, walker.position.y, walker.position.z}, std::nullopt},
                                   std::span<const std::string>{}, std::span<const TagId>{});
}

void PlayController::present_effects(const VfxView& shown) {
    using namespace DirectX;
    if (vfx && character && shell.workspace == darkangel::editor_app::Workspace::Game) {
        const auto tick = character->motor().tick;
        if (vfx_tick && tick > vfx_tick) {
            vfx->update(double(std::min<std::uint64_t>(tick - vfx_tick, 120)) / 60.);
#ifdef DAE_AUDIO
            if (audio)
                audio->update(double(std::min<std::uint64_t>(tick - vfx_tick, 120)) / 60.);
#endif
        }
        vfx_tick = tick;
        cue_effects.follow();
        {
            const auto& rig = character_resources->rig.definition;
            for (const auto& request : cue_primitives.trails.emitting()) {
                const auto identity = character->identity(request.network);
                if (!identity)
                    continue;
                const auto* pose = character->pose(*identity);
                const auto handle = character->presentation().find(*identity);
                if (!pose || !character->presentation().valid(handle))
                    continue;
                std::string key = request.definition->socket;
                if (const auto socket = rig.sockets.find(key); socket != rig.sockets.end())
                    key = socket->second;
                const auto joint =
                    std::find_if(rig.joints.begin(), rig.joints.end(), [&](const auto& candidate) { return candidate.key == key; });
                if (joint == rig.joints.end())
                    continue;
                const auto index = std::size_t(joint - rig.joints.begin());
                if (index >= pose->size())
                    continue;
                const auto& at = character->presentation().read(handle).transform;
                XMFLOAT4X4 stored_joint;
                std::memcpy(&stored_joint, (*pose)[index].values.data(), sizeof(stored_joint));
                const auto joint_world = XMLoadFloat4x4(&stored_joint) *
                                         XMMatrixScaling(float(at.scale), float(at.scale), float(at.scale)) *
                                         XMMatrixRotationRollPitchYaw(float(at.pitch), float(at.yaw), float(at.roll)) *
                                         XMMatrixTranslation(float(at.x), float(at.y), float(at.z));
                const auto point = [&](const std::array<float, 3>& offset) {
                    XMFLOAT3 out;
                    XMStoreFloat3(&out, XMVector3TransformCoord(XMVectorSet(offset[0], offset[1], offset[2], 1), joint_world));
                    return std::array<float, 3>{out.x, out.y, out.z};
                };
                cue_primitives.trails.sample(request.instance, tick, point(request.definition->base), point(request.definition->tip));
            }
            cue_primitives.trails.advance(tick);
            cue_primitives.decals.advance(tick);
            std::vector<darkangel::PrimitiveVertex> built;
            std::vector<darkangel::VfxQuadVertex> quads;
            const auto hand = [&](bool additive) {
                quads.clear();
                for (const auto& vertex : built)
                    quads.push_back({vertex.position, vertex.color});
                built.clear();
                vfx->quads(quads, additive);
            };
            cue_primitives.decals.build(tick, built);
            hand(false);
            cue_primitives.trails.build(tick, built);
            hand(true);
        }
#ifdef DAE_AUDIO
        if (audio) {
            audio->listener(shown.eye, shown.forward, {0, 1, 0});
            cue_sounds.follow();
        }
#endif
        try {
            vfx->draw(shown);
        } catch (const std::exception& error) {
            editor.log(std::string("Visual effects stopped: ") + error.what(), darkangel::editor_app::ConsoleSeverity::Error);
            cue_effects.bind(nullptr, nullptr, {});
            vfx.reset();
        }
    }
}

bool PlayController::quit_ready() const {
    return script_signals.quit && character && character->bootstrapped();
}

void PlayController::party_receipt(unsigned frame) {
#ifdef DAE_EDITOR_GNS
    const auto& player_script = owner_script.steps();
    const auto& player_script_at = owner_script.cursor();
    if (args.party_report.empty())
        return;
    nlohmann::json value{{"schema", 1},
                         {"process", GetCurrentProcessId()},
                         {"role", args.join.empty() ? "host" : "guest"},
                         {"frame", frame},
                         {"scene", globals_scene.text()},
                         {"epoch", std::uint64_t(party_guest  ? party_guest->epoch()
                                                 : party_host ? party_host->epoch()
                                                              : 0)},
                         {"playing", bool(editor.preview.active())},
                         {"running", bool(party_guest ? party_guest->running() : party_host && travel && !travel->busy())},
                         {"ready", character && character->bootstrapped()},
                         {"players", nlohmann::json::array()},
                         {"actors", nlohmann::json::array()},
                         {"arrivals", travel_arrivals},
                         {"failures", travel_failures},
                         {"script_steps", player_script_at},
                         {"script_count", player_script.size()},
                         {"error", party_guest ? party_guest->error() : travel_failure}};
#ifdef DAE_HUD
    value["world_ui"] = nlohmann::json::array();
    if (hud && !hud_failed)
        for (const auto& marker : play_ui->world_markers())
            value["world_ui"].push_back({{"label", marker.label},
                                         {"x", marker.x},
                                         {"y", marker.y},
                                         {"width", marker.width},
                                         {"health_bar", marker.health_bar},
                                         {"percent", marker.percent}});
#endif
    value["render_material_bindings"]=nlohmann::json::object();if(editor.preview.active())for(const auto& [object,slots]:editor.preview.active()->plan.materials)for(const auto& [slot,material]:slots)value["render_material_bindings"][object.text()][slot.text()]=material.text();
    for (std::size_t line = editor.console.size() > 12 ? editor.console.size() - 12 : 0; line < editor.console.size(); ++line)
        value["console"].push_back(editor.console[line].message.substr(0, 300));
    if (character && character->bootstrapped()) {
        value["owner"] = character_player.text();
        value["authority"] = character->hosting();
        value["tick"] = character->motor().tick;
        value["predicted_tick"] = character->predicted_motor().tick;
        const auto at = character->predicted_motor().position;
        value["predicted_position"] = {at.x, at.y, at.z};
        value["pending_motor"] = character->pending_prediction();
        value["rebases"] = character->resynchronizations();
        value["client"] = character->client(character_player)->bootstrap_status();
        value["script_steps"] = player_script_at;
        for (const auto id : character->players()) {
            auto handle = character->presentation().find(id);
            if (!character->presentation().valid(handle))
                continue;
            const auto& object = character->presentation().read(handle);
            value["players"].push_back({{"id", id.text()},
                                        {"position", {object.transform.x, object.transform.y, object.transform.z}},
                                        {"health", object.health.current},
                                        {"pose", character->pose(id) != nullptr}});
        }
        for (const auto id : character->actors()) {
            auto handle = character->presentation().find(id);
            if (!character->presentation().valid(handle))
                continue;
            const auto& object = character->presentation().read(handle);
            value["actors"].push_back({{"id", id.text()}, {"health", object.health.current}, {"pose", character->pose(id) != nullptr}});
        }
        if (const auto* ability = character->ability()) {
            value["equipped_masks"] = nlohmann::json::array();
            for (auto mask : ability->equipped_masks)
                value["equipped_masks"].push_back(mask == AssetId{} ? nlohmann::json(nullptr) : nlohmann::json(mask.text()));
            value["active_mask_slot"] = ability->loadout;
            value["highest_operation"] = ability->highest_operation;
            unsigned accepted{}, refused{};
            for (const auto& operation : ability->operations) {
                accepted += operation.committed;
                refused += operation.failure != AbilityFailure::None;
            }
            value["accepted"] = accepted;
            value["refused"] = refused;
        }
        if (const auto* owner = character->client(character_player); owner && owner->inventory()) {
            const auto& inventory = *owner->inventory();
            value["inventory"] = nlohmann::json::parse(darkangel::encode_inventory(inventory.inventory));
            value["inventory_operation"] = inventory.highest_operation;
            value["inventory_pending"] = owner->inventory_pending();
            value["inventory_failure"] = int(inventory.receipt.failure);
        }
        if (const auto* outcome = character->last_interaction()) {
            value["interaction"] = darkangel::interaction_result_text(outcome->result);
            value["interaction_object"] = outcome->object.text();
        }
        if (const auto* owner = character->client(character_player)) {
            if (const auto* world = owner->world_state())
                value["world"] = nlohmann::json::parse(darkangel::encode_world_state(*world));
            value["downed"] = owner->downed();
        }
        value["scene_now"] = travel && travel->scene() ? travel->scene()->scene.text() : std::string();
        value["arrivals"] = travel_arrivals;
        value["travel_failures"] = travel_failures;
        if (play_saves)
            value["save_status"] = play_saves->status();
        if (character->hosting()) {
            value["movers"] = nlohmann::json::array();
            for (const auto& [id, at] : character->authority().movers())
                value["movers"].push_back({{"id", id.text()}, {"at", {at.x, at.y, at.z}}});
        }
    }
    // What a stopped process last saw while it ran stays in its final receipt.
    if (value.at("running").get<bool>() && value.contains("owner")) {
        party_last_running = value;
        if (party_fullest.is_null() || value.at("players").size() >= party_fullest.at("players").size())
            party_fullest = value;
        else
            value["fullest"] = party_fullest;
    } else if (!party_last_running.is_null()) {
        value["last_running"] = party_last_running;
        value["fullest"] = party_fullest;
    }
    if (!args.party_report.parent_path().empty())
        std::filesystem::create_directories(args.party_report.parent_path());
    const auto temporary = args.party_report.string() + ".tmp";
    {
        std::ofstream file(temporary);
        file << value.dump(2) << '\n';
        require(bool(file), "Cannot write party diagnostic receipt");
    } // Something on the machine briefly holds a just-replaced evidence file; a diagnostic receipt waits for it, then skips this frame.
    bool published = false;
    for (unsigned attempt = 0; attempt < 100 && !published; ++attempt) {
        published = MoveFileExW(std::filesystem::path(temporary).c_str(), args.party_report.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
        if (!published)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!published)
        std::cerr << "Party diagnostic receipt was busy; skipped one update" << std::endl;
#else
    (void)frame;
#endif
}
} // namespace darkangel::editor_app
