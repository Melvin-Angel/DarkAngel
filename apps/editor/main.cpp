#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include <wincodec.h>
#include <EngineFactoryD3D12.h>
#include <EngineFactoryVk.h>
#include <RenderDevice.h>
#include <DeviceContext.h>
#include <SwapChain.h>
#include <Texture.h>
#include <Buffer.h>
#include <PipelineState.h>
#include <ShaderResourceBinding.h>
#include <RefCntAutoPtr.hpp>
#include <ImGuiImplWin32.hpp>
#include <imgui.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <darkangel/assets.hpp>
#include <darkangel/spline.hpp>
#ifdef DAE_ANIMATION
#include <darkangel/animation_assets.hpp>
#include <nlohmann/json.hpp>
#endif
#include <darkangel/editor_service.hpp>
#include "editor_ui.hpp"
#include "editor_options.hpp"
#ifdef DAE_CHARACTER_SCENE
#include "exercises/script_runner.hpp"
#include "exercises/authoring_sources.hpp"
#include "exercises/scene_editing.hpp"
#include "exercises/preview_results.hpp"
#include "exercises/gameplay_results.hpp"
#include "exercises/synthetic_input.hpp"
#include "exercises/acceptance.hpp"
#ifdef DAE_CHARACTER_SCENE
#include "exercises/media_exercises.hpp"
#include "exercises/navigation_exercises.hpp"
#endif
#endif
#include "editor_theme.hpp"
#include "render/render_system.hpp"
#include "package_file.hpp"
#include "vfx_workspace.hpp"
#ifdef DAE_AUDIO
#include "audio_workspace.hpp"
#endif
#include "gameplay_debug.hpp"
#include "cue_debug.hpp"
#include "vfx_cue_sink.hpp"
#ifdef DAE_AUDIO
#include "audio_cue_sink.hpp"
#include "primitive_cue_sink.hpp"
#include <darkangel/primitive_assets.hpp>
#include <darkangel/audio_director.hpp>
#include <deque>
#endif
#include "native_input.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#ifdef DAE_CHARACTER_SCENE
#include "character_preview.hpp"
#ifdef DAE_EDITOR_GNS
#include "network_play.hpp"
#endif
#ifdef DAE_HUD
#include "hud_overlay.hpp"
#include "world_ui_presenter.hpp"
#include "runtime_inventory.hpp"
#include "runtime_altar.hpp"
#include "play_saves.hpp"
#include "exercises/world_place_exercise.hpp"
#include "scene_collision.hpp"
#include <darkangel/hud.hpp>
#include <ashen_roots/royal_hud.hpp>
#endif
#include <darkangel/actor_assets.hpp>
#include <darkangel/cue_assets.hpp>
#endif
#ifdef DAE_AGENT_ENDPOINTS
#include <darkangel/editor_rpc.hpp>
#endif
#include <ImGuizmo.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <cmath>
#include <stdexcept>
#include <vector>

using namespace Diligent;
#ifdef DAE_NAVIGATION
#include <darkangel/navigation.hpp>
#include "navigation_tool.hpp"
#endif
#ifdef DAE_BEHAVIOR
#include <darkangel/behavior_tree.hpp>
#endif
using namespace darkangel;
using darkangel::editor_app::render::Constants;using darkangel::editor_app::render::GpuMesh;using darkangel::editor_app::render::Material;using darkangel::editor_app::render::UploadedModel;using darkangel::editor_app::render::pipeline;using darkangel::editor_app::render::texture;using darkangel::editor_app::render::capture;using darkangel::editor_app::render::upload;using darkangel::editor_app::render::shader_text;using darkangel::editor_app::render::shader_source;
#ifdef DAE_ANIMATION
using darkangel::editor_app::render::UploadedSkin;using darkangel::editor_app::render::skin_shader;using darkangel::editor_app::render::upload_skin;using darkangel::editor_app::render::upload_palette;
#endif
namespace {
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
using darkangel::editor_app::ComposerPackageFile;
struct ComApartment {ComApartment(){require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"Editor COM initialization failed");}~ComApartment(){DirectX::SetWICFactory(nullptr);CoUninitialize();}};
#ifdef DAE_CHARACTER_SCENE
// The editor's scene provider: Play's own preparation, for the scene the party travels to.
struct EditorSceneProvider final:darkangel::SceneProvider {
    std::function<void(AssetId)> on_begin;std::function<darkangel::SceneLoadStatus()> on_poll;std::function<std::unique_ptr<darkangel::PreparedScene>()> on_take;std::function<void()> on_cancel;
    void begin(AssetId scene)override{on_begin(scene);}
    darkangel::SceneLoadStatus poll()override{return on_poll();}
    std::unique_ptr<darkangel::PreparedScene> take()override{return on_take();}
    void cancel()override{on_cancel();}
};
#endif

}
int main(int argc,char** argv){try{
    auto args=darkangel::editor_app::parse_editor_options(argc,argv);auto id=AssetId::parse(args.model);ModelStore store;auto lease=store.prepare(args.registry,args.cas,id);store.publish(lease);
    ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // The render system is made first and shut down after the frame loop; every device resource below lives between.
    darkangel::editor_app::RenderSystem render(args.width,args.height,args.backend);
    auto& window=render.window;auto& device=render.device;auto& context=render.context;auto& swap=render.swap;auto& swap_desc=render.swap_desc;auto& gui=render.gui;
    auto& window_input=render.input;auto& resize_width=render.resize_width;auto& resize_height=render.resize_height;
    darkangel::editor_app::initialize_theme(GetDpiForWindow(window)/96.f);if(!args.hidden)ShowWindow(window,SW_SHOW);
    render.create_frame_pipelines();BufferDesc cb=render.constants_desc;auto& constant_buffer=render.constant_buffer;auto& pso=render.pso;auto& pso_cull=render.pso_cull;
    // A projectile in flight is drawn as a small arrow-like solid built here, not from an asset.
    UploadedModel projectile_gpu;std::size_t projectiles_seen{};
    {RuntimeModel shape;shape.id=AssetId::random();CookedMaterial material;material.id=shape.id;material.color={3.2f,2.3f,.6f,1.f};material.roughness=.4f;material.double_sided=true;shape.materials.push_back(material);
        CookedMesh mesh;const float half=.45f,girth=.085f;const std::array<std::array<float,3>,6> points{{{0,0,half},{0,0,-half},{girth,0,0},{0,girth,0},{-girth,0,0},{0,-girth,0}}};
        for(const auto& point:points){const float length=std::sqrt(point[0]*point[0]+point[1]*point[1]+point[2]*point[2]);mesh.vertices.push_back({point,{point[0]/length,point[1]/length,point[2]/length},{0,0}});}
        mesh.indices={0,2,3,0,3,4,0,4,5,0,5,2,1,3,2,1,4,3,1,5,4,1,2,5};mesh.minimum={-girth,-girth,-half};mesh.maximum={girth,girth,half};mesh.parts.push_back({shape.id,0,static_cast<std::uint32_t>(mesh.indices.size())});shape.meshes.push_back(std::move(mesh));
        projectile_gpu=upload(device,shape,pso,pso_cull);}
    auto gpu=upload(device,*lease.model,pso,pso_cull);darkangel::editor_app::Controller editor(id,gpu.radius);darkangel::editor_app::Shell shell;if(!args.sources.empty())editor.configure_assets(args.sources,args.asset_cache);shell.select_workspace(args.character_workspace?darkangel::editor_app::Workspace::Character:args.npc_workspace?darkangel::editor_app::Workspace::Npc:args.player_workspace?darkangel::editor_app::Workspace::Player:args.animation_workspace?darkangel::editor_app::Workspace::Animation:args.ability_workspace?darkangel::editor_app::Workspace::Ability:args.assets_workspace?darkangel::editor_app::Workspace::Assets:(args.game_workspace||args.exercise_character||args.exercise_combat||args.exercise_heavy)?darkangel::editor_app::Workspace::Game:darkangel::editor_app::Workspace::Scene);if(args.exercise_reaction){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;}if(args.exercise_camera_preview){shell.camera_preview=1;shell.camera_preview_yaw=25;shell.camera_preview_pitch=10;}if(args.tools_workspace)shell.open_tool(darkangel::editor_app::ToolView::InputBindings);if(args.navigation_tool)shell.open_tool(darkangel::editor_app::ToolView::Navigation);if(args.settings_workspace)shell.select_workspace(darkangel::editor_app::Workspace::Settings);if(!args.import_preview.empty())shell.open_import(args.import_preview);std::size_t import_count=editor.asset_inventory.size();
    std::unique_ptr<InputManager> inputs;std::unique_ptr<darkangel::editor_app::NativeInput> native_input;
    AssetId selected_input;
#ifdef DAE_ANIMATION
    // A Player not yet in the start registry is packaged when Play prepares its resources; with a project mount that is not an error.
    if(!args.player_asset.empty())try{selected_input=load_cooked_player(args.registry,args.cas,AssetId::parse(args.player_asset)).input.id;}catch(const std::exception&){if(args.sources.empty())throw;}
    else if(args.character_clips.starts_with("player:"))selected_input=load_cooked_player(args.registry,args.cas,AssetId::parse(args.character_clips.substr(7))).input.id;
    else if(args.character_clips.starts_with("kit:"))selected_input=load_cooked_combat_kit(args.registry,args.cas,AssetId::parse(args.character_clips.substr(4))).input.id;
#endif
    {std::ifstream file(args.registry);auto registry=nlohmann::json::parse(file);for(const auto& record:registry.at("assets"))if(record.at("kind")=="input"&&(selected_input==AssetId{}||record.at("id")==selected_input.text())){require(!inputs,"Editor package has multiple input profiles; select a Player or Combat Kit explicitly");inputs=std::make_unique<InputManager>(load_cooked_input(args.registry,args.cas,AssetId::parse(record.at("id").get<std::string>())));}if(inputs){auto controls=inputs->controls();native_input=std::make_unique<darkangel::editor_app::NativeInput>(controls);window_input=native_input.get();shell.input=inputs.get();}}
    std::map<AssetId,UploadedModel> scene_models;
    // Models worn by the active Play actor (mask visuals); released when Play stops.
    std::map<AssetId,UploadedModel> worn_models;std::set<AssetId> worn_failed;
#ifdef DAE_ANIMATION
    RefCntAutoPtr<IBuffer> bone_buffer;cb.Name="Canonical human skin palette";cb.Size=256*16*sizeof(float);device->CreateBuffer(cb,nullptr,&bone_buffer);require(bone_buffer,"Skin palette buffer creation failed");auto skin_source=skin_shader();auto skin_pso=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,skin_source.c_str(),bone_buffer);auto skin_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,skin_source.c_str(),bone_buffer);std::map<AssetId,UploadedSkin> scene_skins;std::unique_ptr<AnimationClip> diagnostic_clip;if(!args.pose_clip.empty()){auto cooked=load_cooked_clip(args.registry,args.cas,AssetId::parse(args.pose_clip));diagnostic_clip=std::make_unique<AnimationClip>(std::move(cooked.definition),cooked.archive);}
    auto is_skin=[&](AssetId asset){std::ifstream input(args.registry);auto registry=nlohmann::json::parse(input);for(const auto& record:registry.at("assets"))if(record.at("id")==asset.text())return record.at("kind")=="human-binding";return false;};
#endif
    editor.prepare_model=[&](AssetId requested){if(requested==id||scene_models.contains(requested))return;require(scene_models.size()<63,"Editor model resource limit");
#ifdef DAE_ANIMATION
        if(scene_skins.contains(requested))return;if(is_skin(requested)){require(scene_skins.size()<4,"Editor skin resource limit");scene_skins.emplace(requested,upload_skin(device,load_cooked_skinned_model(args.registry,args.cas,requested),args.skin_reference?pso.RawPtr():skin_pso.RawPtr(),args.skin_reference?pso_cull.RawPtr():skin_cull.RawPtr(),diagnostic_clip.get(),args.pose_tick,args.skin_reference));return;}
#endif
        auto cooked=load_cooked_model(args.registry,args.cas,requested);auto resources=upload(device,cooked,pso,pso_cull);scene_models.emplace(requested,std::move(resources));};
    if(!args.scene.empty())editor.open(args.scene);const auto import_baseline=editor.document->world().serialize();
#ifdef DAE_CHARACTER_SCENE
    auto scene_role=[&](const std::string& value){if(value.empty())return StableId{};auto id=StableId::parse(value);if(editor.document->world().valid(editor.document->world().find(id)))return id;StableId resolved;for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+id.text())){require(!resolved,"Ambiguous character scene role; use a placed identity");resolved=origin.object;}require(bool(resolved),"Character scene role missing");return resolved;};
    std::optional<UploadedSkin> authoring_skin;CharacterDefinition authoring_character_definition;
    std::unique_ptr<darkangel::editor_app::GraphPosePreview> graph_pose;std::unique_ptr<darkangel::editor_app::ComposerPosePreview> composer_pose;std::vector<JointMatrix> composer_matrices;
    // Presentation cues of the Play session: the cooked cue assets, the player the session drives, and two recording sinks that stand in
    // for the visual and the audio service until they exist. Declared before the session, which must not outlive its cue player.
    std::optional<std::vector<darkangel::CueDefinition>> play_cues;std::size_t play_cue_closure{};darkangel::editor_app::RecordingCueSink cue_visual{true},cue_audio{false};bool cue_debug_selected{};
    // Visual effects: one service on this device, the effect asset behind each cue visual, and the sink that plays them. The recording sink stays behind it for the Cues tab.
    darkangel::editor_app::VfxWorkspace vfx_workspace(editor);auto& vfx=vfx_workspace.service;std::map<std::string,AssetId> play_effects;std::map<std::string,std::string> play_effect_generations;darkangel::editor_app::PrimitiveCueSink cue_primitives{&cue_visual};std::map<AssetId,darkangel::TrailDefinition> play_trails;std::map<AssetId,darkangel::DecalDefinition> play_decals;darkangel::editor_app::VfxCueSink cue_effects{&cue_primitives};std::uint64_t vfx_tick{};auto& vfx_failed=vfx_workspace.failed;
    vfx_workspace.start(device,context,int(swap_desc.ColorBufferFormat),int(swap_desc.DepthBufferFormat));
#ifdef DAE_AUDIO
    // Sounds, music and the Audio tab: one workspace for the process, its service made when the first Play or audition needs it.
    darkangel::editor_app::AudioWorkspace audio_workspace(editor,args,cue_audio);
    auto& audio=audio_workspace.service;auto& play_sounds=audio_workspace.play_sounds;auto& cue_sounds=audio_workspace.cue_sounds;auto& audio_failed=audio_workspace.failed;
    auto& audio_director=audio_workspace.director;auto& play_profiles=audio_workspace.play_profiles;auto& encounter_profiles=audio_workspace.encounter_profiles;
    auto& audio_applied=audio_workspace.applied;auto& audio_in_play=audio_workspace.in_play;auto& audio_generations=audio_workspace.generations;auto& audio_tab=audio_workspace.state;
    const auto& audio_mix_path=audio_workspace.mix_path;auto project_mix=[&]{return audio_workspace.project_mix();};auto ensure_audio=[&]{return audio_workspace.ensure();};
    auto load_audio=[&](std::vector<AssetId> wanted){return audio_workspace.load(std::move(wanted));};
#endif
    // The VFX tab: the selected effect is cooked and played in a loop in the preview, and cooked again whenever its source file changes on disk
    // (that is how an edit saved in Effekseer shows up). A refusal by the cooker is shown as it is.
    auto& vfx_preview=vfx_workspace.state;
    auto vfx_effects=[&]{return vfx_workspace.effects();};auto vfx_cook=[&]{vfx_workspace.cook();};auto vfx_select=[&](const darkangel::AssetInfo& asset){vfx_workspace.select(asset);};
    vfx_workspace.reveal_cue_form=args.exercise_cue_form;vfx_workspace.on_cue_sources_saved=[&]{play_cues.reset();};
    if(editor.assets)shell.draw_vfx_panel=[&]{vfx_workspace.draw_panel();};
    if(args.vfx_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Vfx);if(editor.assets)editor.refresh_assets();for(const auto& asset:vfx_effects())if(asset.path==args.vfx_select)vfx_select(asset);}
    std::unique_ptr<darkangel::CuePlayer> cue_player;
    // Presentation-only camera brain for the Play actor; rebuilt for each Play session.
    // Further local players of this Play, the second one's camera, and the script that drives it when one was given:
    // steps "walk-to:x,z", "interact" and "wait:frames", separated by ';'.
    std::unique_ptr<CameraDirector> director_second;std::vector<StableId> companions;darkangel::editor_app::exercises::ScriptRunner companion_script(args.input_script),owner_script(args.player_script);
    auto& script=companion_script.steps();auto& player_script=owner_script.steps();auto& script_at=companion_script.cursor();auto& player_script_at=owner_script.cursor();
    auto& script_interactions=companion_script.interactions();auto& player_script_interactions=owner_script.interactions();
    darkangel::editor_app::exercises::ScriptSignals script_signals;auto& script_capture=script_signals.capture;auto& script_captured=script_signals.captured;auto& script_quit=script_signals.quit;
    std::uint64_t guest_arrived_epoch{};require(args.players>=1&&args.players<=4,"--players is 1 to 4");shell.players=int(args.players);
    std::unique_ptr<CameraDirector> director;std::vector<std::string> camera_held;std::vector<TagId> camera_tags;float camera_fov=DirectX::XM_PIDIV4;
    // Play's scene session belongs to the transition coordinator, which replaces it when the party travels; character is the
    // running session. The world ledger lives as long as one Play. While a Play travels, the editor's document, Play resources
    // and uploaded models are those of globals_scene, and the other scenes' documents (the authoring one among them) are parked.
    darkangel::WorldLedger play_ledger;EditorSceneProvider scene_provider;std::unique_ptr<darkangel::editor_app::PlaySaves> play_saves;std::function<void(std::string)> save_play;
#ifdef DAE_EDITOR_GNS
    std::unique_ptr<darkangel::editor_app::EditorPartyHost> party_host;std::unique_ptr<darkangel::editor_app::EditorPartyGuest> party_guest;
#endif
    std::unique_ptr<darkangel::SceneTransitionCoordinator> travel;CharacterSceneSession* character{};StableId character_player;
    using SpawnController=std::function<std::shared_ptr<darkangel::ActorController>(const darkangel::editor_app::PreparedSpawn&,const darkangel::CollisionDefinition&)>;
    std::function<void(const SpawnPlan&)> attach_session;std::function<SpawnController()> make_controller;std::function<void(AssetId,std::unique_ptr<darkangel::EditorDocument>,const std::filesystem::path&)> enter_scene;
    AssetId globals_scene,authoring_scene;std::filesystem::path authoring_path;std::map<AssetId,std::unique_ptr<darkangel::EditorDocument>> parked_documents;std::map<AssetId,std::filesystem::path> parked_paths;bool travel_preparing=false,travel_was_busy=false;
    struct TravelLoad {AssetId scene;unsigned step{};bool parked{};std::filesystem::path path;std::unique_ptr<darkangel::EditorDocument> document;std::unique_ptr<darkangel::PreparedScene> prepared;} travel_load;std::unique_ptr<darkangel::SceneSession::Prepared> travel_preview;
    unsigned travel_arrivals{},travel_failures{},travel_overlay_frames{};std::string travel_failure;std::unique_ptr<darkangel::editor_app::CharacterPreviewResources> character_resources;CharacterSceneInput character_input;std::vector<InputEvent> combat_events;
#ifdef DAE_AUDIO
    audio_workspace.on_cue_sources_saved=[&]{play_cues.reset();};
    if(editor.assets)shell.draw_audio_panel=[&]{audio_workspace.draw_panel(bool(character));};
    if(args.audio_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Audio);if(editor.assets)editor.refresh_assets();}
#endif
    // Navigation and behaviour facts of the latest Play session, for readouts.
    unsigned navigation_polygons{};std::string behavior_running;
    // Spawn points whose NPC definition is in a cooked registry. A startup registry may predate the scene's spawn points; a prepared Play packages them all.
    auto registry_spawns=[&](const std::filesystem::path& path){std::ifstream input(path);auto registry=nlohmann::json::parse(input);std::set<std::string> known;for(const auto& record:registry.at("assets"))if(record.at("kind")=="npc")known.insert(record.at("id").get<std::string>());std::map<StableId,AssetId> result;for(const auto& [point,npc]:editor.document->plan().spawns)if(known.contains(npc.text())&&!editor.document->plan().spawn_owners.contains(point))result.emplace(point,npc);return result;};
    // NPC definitions the scene's encounters can spawn, and those of them a cooked registry holds.
    auto encounter_npcs=[&]{std::set<AssetId> result;for(const auto& [object,json]:editor.document->plan().encounters){try{for(auto npc:darkangel::decode_encounter(json).npcs())result.insert(npc);}catch(const std::exception&){}}return result;};
    auto registry_encounter_npcs=[&](const std::filesystem::path& path){std::ifstream input(path);auto registry=nlohmann::json::parse(input);std::set<std::string> known;for(const auto& record:registry.at("assets"))if(record.at("kind")=="npc")known.insert(record.at("id").get<std::string>());std::set<AssetId> result;for(auto npc:encounter_npcs())if(known.contains(npc.text()))result.insert(npc);return result;};
    // Exercises act on the scene's first spawned actor.
    auto first_actor=[&]{require(character_resources&&!character_resources->spawns.empty(),"This exercise needs a scene with an NPC spawn point");return character_resources->spawns.front().actor;};
    // HUD target: a presentation choice that writes no gameplay state and is separate from soft lock and from any lock-on.
    // The soft-locked actor if there is one, else the nearest live hostile in front of the camera; a target that stops
    // qualifying stays shown until another one does.
    std::optional<StableId> hud_target;
    auto choose_hud_target=[&]{
        if(!character||!character_resources||!character_resources->combat){hud_target.reset();return hud_target;}
#ifdef DAE_EDITOR_GNS
        // A networked client that is catching up on a new world revision holds no observer state to aim at.
        if((party_host||party_guest)&&!character->bootstrapped()){hud_target.reset();return hud_target;}
#endif
        if(auto locked=character->soft_lock();locked&&character->observer_if_ready(*locked,double(character->motor().tick))){hud_target=locked;return hud_target;}
        const auto& combat=*character_resources->combat;const auto& self=character->motor();const double yaw=director?director->yaw():self.yaw,forward_x=std::sin(yaw),forward_z=std::cos(yaw);
        std::optional<StableId> best;double nearest{};
        // Every actor the session holds now, enemies an encounter brought in included.
        for(const auto id:character->actors()){const auto* actor=character->actor(id);if(!actor||actor->faction==combat.faction)continue;auto handle=character->presentation().find(id);if(!character->presentation().valid(handle)||character->presentation().read(handle).health.current<=0)continue;
            const auto observed=character->observer_if_ready(id,double(self.tick));if(!observed)continue;const auto at=observed->frame.motor.position;const double dx=at.x-self.position.x,dz=at.z-self.position.z,distance=std::hypot(dx,dz);if(dx*forward_x+dz*forward_z<=0||distance>40)continue;if(!best||distance<nearest){best=id;nearest=distance;}}
        hud_target=best;return hud_target;};
#ifdef DAE_NAVIGATION
    // Tools > Navigation: an explicit bake of the scene's cooked collision that Play reuses while the collision is unchanged.
    darkangel::editor_app::NavigationTool navigation;
    auto navigation_key=[](const darkangel::CollisionDefinition& collision){return darkangel::editor_app::NavigationTool::collision_key(collision);};
    auto bake_navigation=[&]{navigation.bake(character_resources?character_resources->collision.get():nullptr);};
    auto navigation_for=[&](const darkangel::CollisionDefinition& collision){return navigation.mesh_for(collision);};
    // The project's save slot lives beside its asset cache. Saving is the host's: a capture of the ledger and inventories at a safe point.
    if(editor.assets)play_saves=std::make_unique<darkangel::editor_app::PlaySaves>(editor.assets->cas_path().parent_path()/"saves"/"project.dasave");
    shell.draw_saves_tool=[&]{if(!play_saves){ImGui::TextDisabled("Saves need a project with an asset cache.");return;}play_saves->draw(character&&travel&&!travel->busy(),[&]{if(save_play)save_play("Saved from Tools > Saves");});};
    shell.draw_navigation_tool=[&]{navigation.draw_panel(character_resources?character_resources->collision.get():nullptr);};
#endif
#ifdef DAE_HUD
    // Runtime HUD: the game's RmlUi document over the Game viewport, fed by a native view model of confirmed
    // state. Its geometry goes into the viewport's draw list; font atlases are device textures.
    std::map<ImTextureID,RefCntAutoPtr<ITexture>> hud_textures;
    darkangel::editor_app::ImGuiHudRenderer hud_renderer([&](const unsigned char* rgba,int width,int height){TextureDesc desc;desc.Name="HUD font atlas";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=static_cast<Uint32>(width);desc.Height=static_cast<Uint32>(height);desc.MipLevels=1;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.Usage=USAGE_IMMUTABLE;desc.BindFlags=BIND_SHADER_RESOURCE;TextureSubResData level;level.pData=rgba;level.Stride=static_cast<Uint64>(width)*4;TextureData data;data.pSubResources=&level;data.NumSubresources=1;RefCntAutoPtr<ITexture> texture;device->CreateTexture(desc,&data,&texture);require(texture,"HUD texture creation failed");auto id=reinterpret_cast<ImTextureID>(texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));hud_textures.emplace(id,std::move(texture));return id;},[&](ImTextureID id){hud_textures.erase(id);});
    std::unique_ptr<darkangel::Hud> hud;bool hud_failed=false,hud_second_drawn=false;
    // Debug view of NPC senses over the Scene and Game viewports. While authoring: the radii and cones of the selected spawn
    // point's NPC, from its draft. In Play: the same for every actor ticked in the Gameplay Inspector (or whose spawn point is
    // selected) at its current place, with a line from its eye to each actor it perceives. Reads the host's own state; draws only.
    std::set<StableId> senses_shown;bool senses_all=false,world_debug=false;unsigned senses_drawn=0,volumes_drawn=0,navigation_drawn=0,interactions_asked=0,splines_drawn=0,scatter_drawn=0;bool interact_pressed=false,interact_held=false,interaction_offered=false;std::uint64_t senses_noticed=0;double senses_noticed_z=0;bool senses_early=false;
    // Viewport picking: the first scene object whose bounds the segment meets. A model uses its cooked bounds under the object's
    // transform; a character or a spawn point uses a standing body's box.
    shell.pick_object=[&](const std::array<double,3>& from,const std::array<double,3>& to)->StableId{
        using namespace DirectX;StableId best;float nearest=2;
        auto test=[&](StableId object,const std::array<float,3>& center,const std::array<float,3>& half){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return;const auto pose=editor.displayed().read(entity).transform;
            const auto inverse=XMMatrixInverse(nullptr,XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll))*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z)));
            XMFLOAT3 a,b;XMStoreFloat3(&a,XMVector3TransformCoord(XMVectorSet(float(from[0]),float(from[1]),float(from[2]),1),inverse));XMStoreFloat3(&b,XMVector3TransformCoord(XMVectorSet(float(to[0]),float(to[1]),float(to[2]),1),inverse));
            const float start[3]={a.x,a.y,a.z},delta[3]={b.x-a.x,b.y-a.y,b.z-a.z};float enter=0,leave=1;
            for(unsigned axis=0;axis<3;++axis){const float reach=std::max(half[axis],.05f),low=center[axis]-reach,high=center[axis]+reach;if(std::abs(delta[axis])<1e-9f){if(start[axis]<low||start[axis]>high)return;continue;}float first=(low-start[axis])/delta[axis],second=(high-start[axis])/delta[axis];if(first>second)std::swap(first,second);enter=std::max(enter,first);leave=std::min(leave,second);if(enter>leave)return;}
            if(enter<nearest){nearest=enter;best=object;}};
        const std::array<float,3> body_center{0,.9f,0},body_half{.4f,.9f,.4f};
        for(const auto& [object,model]:editor.document->plan().models){if(model==id)test(object,gpu.center,gpu.half);else if(auto found=scene_models.find(model);found!=scene_models.end())test(object,found->second.center,found->second.half);else test(object,body_center,body_half);}
        for(const auto& [object,npc]:editor.document->plan().spawns)test(object,body_center,body_half);
        // A volume is picked only where no solid object smaller than it is under the cursor, so things inside a volume stay clickable.
        const StableId solid=best;const float solid_at=nearest;float solid_size=1e9f;if(solid){solid_size=1.f;if(auto model=editor.document->plan().models.find(solid);model!=editor.document->plan().models.end()){if(model->second==id)solid_size=gpu.radius;else if(auto found=scene_models.find(model->second);found!=scene_models.end())solid_size=found->second.radius;}auto entity=editor.displayed().find(solid);if(editor.displayed().valid(entity))solid_size*=float(editor.displayed().read(entity).transform.scale);}
        StableId volume;float volume_size=1e9f;auto offer=[&](StableId object,const std::array<float,3>& half){best={};nearest=2;test(object,{0,0,0},half);const float size=std::max({half[0],half[1],half[2]});if(best&&size<volume_size){volume=best;volume_size=size;}};
        for(const auto& [object,info]:editor.document->plan().volumes)offer(object,{float(info.half[0]),float(info.half[1]),float(info.half[2])});
        for(const auto& [object,json]:editor.document->plan().interactables)if(!editor.document->plan().models.contains(object))offer(object,{.4f,.4f,.4f});
        for(const auto& [object,info]:editor.document->plan().spline_points)offer(object,{.3f,.3f,.3f});for(const auto& [object,json]:editor.document->plan().splines)offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().triggers){try{const auto trigger=darkangel::decode_trigger(json);offer(object,{float(trigger.half.x),float(trigger.half.y),float(trigger.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().transitions){try{const auto transition=darkangel::decode_transition(json);offer(object,{float(transition.half.x),float(transition.half.y),float(transition.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().entries)offer(object,{.4f,.4f,.4f});
        for(const auto& [object,json]:editor.document->plan().pickups)if(!editor.document->plan().models.contains(object))offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().altars)if(!editor.document->plan().models.contains(object))offer(object,{.5f,.5f,.5f});
        for(const auto& [object,json]:editor.document->plan().movers){try{const auto mover=darkangel::decode_mover(json);test(object,{0,float(mover.half.y),0},{float(mover.half.x),float(mover.half.y),float(mover.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().encounters){best={};nearest=2;test(object,{0,.5f,0},{.4f,.4f,.4f});if(best){volume=best;volume_size=.4f;}}
        best=solid;nearest=solid_at;if(volume&&(!solid||solid_size>volume_size))best=volume;
        return best;};
    // Placement surface: rays against the scene's cooked static collision, in a query world built when the collision changes.
    std::unique_ptr<darkangel::PhysicsWorld> place_world;const darkangel::CollisionDefinition* place_collision=nullptr;
    shell.place_surface=[&](const std::array<double,3>& from,const std::array<double,3>& to)->std::optional<std::array<double,3>>{
        if(!character_resources||!character_resources->collision)return std::nullopt;
        if(place_collision!=character_resources->collision.get()){place_world=std::make_unique<darkangel::PhysicsWorld>();place_world->load_scene(*character_resources->collision);place_collision=character_resources->collision.get();}
        const auto found=place_world->query_ray({from[0],from[1],from[2]},{to[0]-from[0],to[1]-from[1],to[2]-from[2]},{darkangel::StaticCollision,0});const darkangel::CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;
        if(!nearest)return std::nullopt;return std::array<double,3>{nearest->point.x,nearest->point.y,nearest->point.z};};
    // Splines of the scene as they stand now (a point being dragged included): the curve through each spline's points in order.
    struct BuiltSpline {StableId id;darkangel::SplineDefinition definition;std::vector<darkangel::MotorVec> points;std::optional<darkangel::Spline> curve;};
    auto build_splines=[&]{std::vector<BuiltSpline> result;if(!editor.document)return result;const auto& plan=editor.document->plan();
        for(const auto& [object,json]:plan.splines){BuiltSpline built;built.id=object;try{built.definition=darkangel::decode_spline(json);}catch(const std::exception&){continue;}
            std::vector<std::pair<unsigned,StableId>> order;for(const auto& [point,info]:plan.spline_points)if(info.spline==object)order.push_back({info.index,point});std::sort(order.begin(),order.end());
            for(const auto& [index,point]:order){auto entity=editor.displayed().find(point);if(!editor.displayed().valid(entity))continue;auto pose=editor.displayed().read(entity).transform;if(shell.draft&&editor.selected==point)pose=*shell.draft;built.points.push_back({pose.x,pose.y,pose.z});}
            if(built.points.size()>=2&&built.points.size()<=darkangel::spline_point_limit){try{built.curve.emplace(built.points,built.definition.closed);}catch(const std::exception&){}}
            result.push_back(std::move(built));}
        return result;};
    shell.debug_overlay=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        using namespace DirectX;using darkangel::MotorVec;senses_drawn=0;volumes_drawn=0;splines_drawn=0;if(!shell.scene_camera||!draw)return;const auto matrix=XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection);
        auto project=[&](const MotorVec& point,ImVec2& out){XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(float(point.x),float(point.y),float(point.z),1),matrix));if(clip.w<=.05f)return false;out={area.x+(clip.x/clip.w*.5f+.5f)*area.width,area.y+(.5f-clip.y/clip.w*.5f)*area.height};return true;};
        auto line=[&](const MotorVec& a,const MotorVec& b,ImU32 colour,float width=1.5f){ImVec2 p,q;if(project(a,p)&&project(b,q))draw->AddLine(p,q,colour,width);};
#ifdef DAE_NAVIGATION
        // The baked navigation mesh: polygon edges a little above the surface, the border brighter.
        navigation_drawn=0;if(navigation.shown&&navigation.mesh)for(const auto& edge:navigation.outline){ImVec2 p,q;if(project({edge.from.x,edge.from.y+.05,edge.from.z},p)&&project({edge.to.x,edge.to.y+.05,edge.to.z},q)){draw->AddLine(p,q,edge.border?IM_COL32(80,220,255,255):IM_COL32(80,220,255,110),edge.border?2.f:1.f);++navigation_drawn;}}
#endif
        auto ring=[&](const MotorVec& centre,double radius,ImU32 colour){constexpr unsigned steps=48;for(unsigned i=0;i<steps;++i){const double a=6.283185307179586*i/steps,b=6.283185307179586*(i+1)/steps;line({centre.x+std::sin(a)*radius,centre.y,centre.z+std::cos(a)*radius},{centre.x+std::sin(b)*radius,centre.y,centre.z+std::cos(b)*radius},colour);}};
        auto shape=[&](const darkangel::SensesDefinition& senses,const MotorVec& foot,double yaw,bool alert){
            ++senses_drawn;const MotorVec eye{foot.x,foot.y+senses.eye_height,foot.z};const ImU32 vision=alert?IM_COL32(255,80,60,255):IM_COL32(90,230,120,255);constexpr double radians=3.141592653589793/180;
            auto toward=[&](double aside,double above,double range){const double a=yaw+aside,c=std::cos(above);return MotorVec{eye.x+std::sin(a)*c*range,eye.y+std::sin(above)*range,eye.z+std::cos(a)*c*range};};
            for(const auto& sensor:senses.sensors){
                if(sensor.kind==darkangel::SensorKind::Distance)ring({foot.x,foot.y+.05,foot.z},sensor.range,IM_COL32(255,200,60,255));
                else if(sensor.kind==darkangel::SensorKind::LineOfSight)ring(eye,sensor.range,IM_COL32(80,200,255,255));
                else{const double h=sensor.horizontal*radians,v=sensor.vertical*radians;constexpr unsigned steps=24;
                    // Three arcs at the far end (bottom, level, top), the two side edges, and the facing line.
                    for(double above:{-v,0.,v})for(unsigned i=0;i<steps;++i)line(toward(-h+2*h*i/steps,above,sensor.range),toward(-h+2*h*(i+1)/steps,above,sensor.range),vision,above==0?2.f:1.f);
                    for(double aside:{-h,h}){for(double above:{-v,0.,v})line(eye,toward(aside,above,sensor.range),vision,above==0?2.f:1.f);for(unsigned i=0;i<8;++i)line(toward(aside,-v+2*v*i/8,sensor.range),toward(aside,-v+2*v*(i+1)/8,sensor.range),vision,1.f);}
                    line(eye,toward(0,0,sensor.range),vision,1.f);}
            }
            if(senses.everything){ImVec2 at;if(project({eye.x,eye.y+.5,eye.z},at))draw->AddText(at,IM_COL32(255,255,255,255),"sees everything");}
        };
        auto placed=[&](StableId object,MotorVec& foot,double& yaw){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;const auto transform=editor.displayed().read(entity).transform;foot={transform.x,transform.y,transform.z};yaw=transform.yaw;return true;};
        draw->PushClipRect({area.x,area.y},{area.x+area.width,area.y+area.height},true);
        if(character&&character_resources&&editor.preview.active()){
            for(const auto& spawn:character_resources->spawns){if(!senses_all&&!senses_shown.contains(spawn.actor)&&editor.selected!=spawn.point&&editor.selected!=spawn.actor)continue;const auto* perception=character->perception(spawn.actor);MotorVec foot;double yaw{};if(!perception||!placed(spawn.actor,foot,yaw))continue;
                bool alert=false;const MotorVec eye{foot.x,foot.y+perception->senses().eye_height,foot.z};
                for(const auto& other:perception->actors()){alert|=other.detected;line(eye,{other.motor.position.x,other.motor.position.y+darkangel::perception_body_height,other.motor.position.z},other.detected?(other.visible?IM_COL32(255,60,60,255):IM_COL32(255,160,40,255)):other.sensed?IM_COL32(255,240,90,255):IM_COL32(160,160,160,255),other.detected?2.5f:1.f+float(other.awareness)*1.5f);}
                shape(perception->senses(),foot,yaw,alert);}
        }else if(auto spawn=editor.document->plan().spawns.find(editor.selected);spawn!=editor.document->plan().spawns.end()){
            try{const auto& source=editor.authoring.open(*editor.assets,spawn->second).value;MotorVec foot;double yaw{};if(source.contains("senses")&&placed(spawn->first,foot,yaw))shape(darkangel::decode_npc_senses(source.at("senses").dump()),foot,yaw,false);}catch(const std::exception&){}
        }
        // Encounter volumes, trigger volumes and encounter markers: always while authoring, on request in Play.
        if(!editor.preview.active()||world_debug){const auto& plan=editor.document->plan();auto pose_of=[&](StableId object,darkangel::Transform& out){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;out=editor.displayed().read(entity).transform;return true;};darkangel::Transform at,to;
            auto box=[&](const darkangel::Transform& pose,const std::array<double,3>& half,ImU32 colour,float width){const double c=std::cos(pose.yaw),s=std::sin(pose.yaw);auto corner=[&](int index){const double x=(index&1?1:-1)*half[0]*pose.scale,y=(index&2?1:-1)*half[1]*pose.scale,z=(index&4?1:-1)*half[2]*pose.scale;return MotorVec{pose.x+c*x+s*z,pose.y+y,pose.z-s*x+c*z};};for(int index=0;index<8;++index)for(int bit:{1,2,4})if(!(index&bit))line(corner(index),corner(index|bit),colour,width);};
            StableId focus=editor.selected;if(auto volume=plan.volumes.find(focus);volume!=plan.volumes.end())focus=volume->second.encounter;else if(auto point=plan.spawn_owners.find(focus);point!=plan.spawn_owners.end())focus=point->second;
            for(const auto& [object,volume]:plan.volumes)if(pose_of(object,at)){const bool lit=volume.encounter==focus||object==editor.selected;box(at,volume.half,lit?IM_COL32(120,220,255,255):IM_COL32(70,140,170,170),object==editor.selected?2.5f:lit?1.8f:1.f);++volumes_drawn;if(lit&&pose_of(volume.encounter,to))line({at.x,at.y,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,160),1.f);}
            for(const auto& [point,owner]:plan.spawn_owners)if(owner==focus&&pose_of(point,at)&&pose_of(owner,to))line({at.x,at.y+.9,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,220),1.5f);
            // A spline: its curve and its points.
            for(const auto& built:build_splines()){const bool lit=built.id==editor.selected||(plan.spline_points.contains(editor.selected)&&plan.spline_points.at(editor.selected).spline==built.id);const ImU32 colour=IM_COL32(255,120,220,lit?255:190);
                if(built.curve){const auto& path=built.curve->polyline();for(std::size_t piece=0;piece+1<path.size();++piece)line({path[piece].x,path[piece].y+.05,path[piece].z},{path[piece+1].x,path[piece+1].y+.05,path[piece+1].z},colour,lit?2.5f:1.6f);++splines_drawn;}
                for(const auto& point:built.points){darkangel::Transform marker;marker.x=point.x;marker.y=point.y;marker.z=point.z;marker.scale=1;box(marker,{.15,.15,.15},colour,1.2f);}}
            // An interactable: its reach as a ring on the ground and a small marker box.
            for(const auto& [object,json]:plan.interactables)if(pose_of(object,at)){try{const auto item=darkangel::decode_interactable(json);const ImU32 colour=IM_COL32(120,255,160,object==editor.selected?255:190);ring({at.x,at.y+.05,at.z},item.radius,colour);box(at,{.25,.25,.25},colour,object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            // Party travel: a transition's box, and an entry point as a ring with a mark the way the party will face.
            for(const auto& [object,json]:plan.transitions)if(pose_of(object,at)){try{const auto transition=darkangel::decode_transition(json);box(at,{transition.half.x,transition.half.y,transition.half.z},IM_COL32(190,130,255,object==editor.selected?255:190),object==editor.selected?2.5f:1.2f);if(transition.activation==darkangel::TransitionActivation::Interact)ring({at.x,at.y+.05,at.z},transition.radius,IM_COL32(190,130,255,170));ImVec2 label;if(project({at.x,at.y+transition.half.y+.3,at.z},label))draw->AddText(label,IM_COL32(230,210,255,255),"Scene transition");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.entries)if(pose_of(object,at)){const ImU32 colour=IM_COL32(120,200,255,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},.6,colour);darkangel::Transform facing=at;facing.x+=std::sin(at.yaw)*.9;facing.z+=std::cos(at.yaw)*.9;facing.scale=1;box(facing,{.12,.12,.12},colour,2.f);ImVec2 label;if(project({at.x,at.y+1.,at.z},label)){std::string name="Entry";try{name="Entry: "+darkangel::decode_entry(json).name;}catch(const std::exception&){}draw->AddText(label,IM_COL32(210,235,255,255),name.c_str());}++volumes_drawn;}
            for(const auto& [object,json]:plan.pickups)if(pose_of(object,at)){try{const auto pickup=darkangel::decode_pickup(json);const ImU32 colour=IM_COL32(255,220,90,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},pickup.radius,colour);darkangel::Transform marker=at;marker.y+=.3;marker.scale=1;box(marker,{.2,.2,.2},colour,object==editor.selected?2.5f:1.4f);ImVec2 label;if(project({at.x,at.y+.8,at.z},label))draw->AddText(label,colour,("Pickup x"+std::to_string(pickup.quantity)).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.altars)if(pose_of(object,at)){try{const auto altar=darkangel::decode_altar(json);const ImU32 colour=IM_COL32(255,150,110,object==editor.selected?255:210);ring({at.x,at.y+.05,at.z},altar.radius,colour);darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.35,.5,.35},colour,object==editor.selected?2.5f:1.6f);const auto arrival=darkangel::altar_arrival(altar,{at.x,at.y,at.z},at.yaw);ring({arrival.x,arrival.y+.05,arrival.z},.5,IM_COL32(120,200,255,200));line({at.x,at.y+.1,at.z},{arrival.x,arrival.y+.1,arrival.z},IM_COL32(120,200,255,160),1.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,colour,("Altar: "+altar.name).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.movers)if(pose_of(object,at)){try{const auto mover=darkangel::decode_mover(json);const ImU32 colour=IM_COL32(150,230,230,object==editor.selected?255:200);darkangel::Transform start=at;start.y+=mover.half.y*at.scale;box(start,{mover.half.x,mover.half.y,mover.half.z},colour,object==editor.selected?2.5f:1.4f);
                const double c=std::cos(at.yaw),s=std::sin(at.yaw);darkangel::Transform end=start;end.x+=(c*mover.offset.x+s*mover.offset.z)*at.scale;end.y+=mover.offset.y*at.scale;end.z+=(-s*mover.offset.x+c*mover.offset.z)*at.scale;box(end,{mover.half.x,mover.half.y,mover.half.z},IM_COL32(150,230,230,110),1.f);line({start.x,start.y,start.z},{end.x,end.y,end.z},IM_COL32(150,230,230,160),1.f);
                ImVec2 label;if(project({start.x,start.y+mover.half.y*at.scale+.3,start.z},label))draw->AddText(label,colour,mover.door?(mover.locked?"Door (locked)":"Door"):"Platform");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.triggers)if(pose_of(object,at)){try{const auto trigger=darkangel::decode_trigger(json);box(at,{trigger.half.x,trigger.half.y,trigger.half.z},IM_COL32(255,170,60,object==editor.selected?255:180),object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.encounters)if(pose_of(object,at)){darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.4,.4,.4},object==focus?IM_COL32(255,255,255,255):IM_COL32(120,220,255,230),2.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,IM_COL32(255,255,255,255),"Encounter");}
        }
        draw->PopClipRect();
    };
    darkangel::editor_app::RuntimeInventory runtime_inventory;darkangel::editor_app::RuntimeAltar runtime_altar;
    shell.game_overlay=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        if(!character||!character_resources||!character_resources->combat||!character->ability()){hud.reset();return;}
        if(hud_failed)return;
        try{
            const int width=std::max(1,int(area.width)),height=std::max(1,int(area.height));
            if(!hud)hud=std::make_unique<darkangel::Hud>(hud_renderer,std::filesystem::path(DAE_GAME_UI_DIR)/"hud.rml","C:/Windows/Fonts/segoeui.ttf",width,height);
            hud->resize(width,height);const auto& combat=*character_resources->combat;std::optional<darkangel::Health> target;const auto shown=choose_hud_target();if(shown){auto handle=character->presentation().find(*shown);if(character->presentation().valid(handle))target=character->presentation().read(handle).health;}
            auto hud_model=ashen_roots::royal_hud(*character->ability(),combat.attributes,target,darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability()));hud_model.target_locked=character->soft_lock().has_value();if(shown)if(const auto* actor=character->actor(*shown))hud_model.target_name=actor->name;hud_model.crosshair=director&&director->rig().cameras[director->live()].facing==CameraFacing::Camera;const auto offered_now=character->interaction_prompt();if(offered_now)hud_model.prompt=offered_now->prompt;if(const auto* owner=character->client(character_player))darkangel::editor_app::party_lifecycle_hud(hud_model,*owner,offered_now,interact_held);if(travel&&travel->busy()){const auto status=travel->status();hud_model.loading=true;hud_model.loading_text=status.text.substr(0,96);hud_model.loading_progress=status.progress;++travel_overlay_frames;}if(!hud_model.loading&&shell.scene_camera)if(const auto* viewer=character->client(character_player)){std::vector<PartyMember> roster;
#ifdef DAE_EDITOR_GNS
            if(party_host)roster=party_host->roster();else if(party_guest)roster=party_guest->roster();
#endif
            hud_model.world_markers=darkangel::editor_app::character_world_ui(*character,*viewer,darkangel::editor_app::world_ui_camera(shell.scene_view,shell.scene_projection,area.width,area.height),roster);}hud->update(hud_model);runtime_inventory.frame(*hud,character->client(character_player),*character_resources,area.x,area.y,area.width,area.height,ImGui::IsWindowFocused(),hud_model.loading);runtime_altar.frame(*hud,character->client(character_player),character->hosting(),ImGui::IsWindowFocused(),hud_model.loading);shell.inventory_open=runtime_inventory.open()||runtime_altar.open();hud_renderer.begin(draw,{area.x,area.y},{area.width,area.height});hud->render();hud_renderer.end();
        }catch(const std::exception& error){hud_renderer.end();hud.reset();hud_failed=true;editor.log(std::string("Runtime HUD unavailable: ")+error.what());}
    };
    // The second local player's HUD, from its own client: its attributes, mask and what it is offered. RmlUi allows one
    // Hud at a time, so the first viewport's Hud is laid out again with the second player's values and drawn here.
    shell.game_overlay_second=[&](ImDrawList* draw,darkangel::editor_app::ViewArea area){
        if(!character||character->players().size()<2||!character_resources||!character_resources->combat||!hud||hud_failed)return;
        const auto* second=character->client(character->players()[1]);if(!second||!second->ability())return;
        try{
            hud->resize(std::max(1,int(area.width)),std::max(1,int(area.height)));auto hud_model=ashen_roots::royal_hud(*second->ability(),character_resources->combat->attributes,std::optional<darkangel::Health>{},darkangel::editor_app::RuntimeInventory::name(*character_resources,second->ability()));hud_model.target_locked=second->soft_lock().has_value();if(const auto offered=second->interaction_prompt())hud_model.prompt=offered->prompt;if(travel&&travel->busy()){const auto status=travel->status();hud_model.loading=true;hud_model.loading_text=status.text.substr(0,96);hud_model.loading_progress=status.progress;}if(!hud_model.loading)hud_model.world_markers=darkangel::editor_app::character_world_ui(*character,*second,darkangel::editor_app::world_ui_camera(shell.game_view_second,shell.game_projection_second,area.width,area.height));hud->update(hud_model);hud_renderer.begin(draw,{area.x,area.y},{area.width,area.height});hud->render();hud_renderer.end();hud_second_drawn=true;
        }catch(const std::exception& error){hud_renderer.end();hud.reset();hud_failed=true;editor.log(std::string("Runtime HUD unavailable: ")+error.what());}
    };
#endif
    if(!args.character_clips.empty()){require(inputs!=nullptr,"Character controls require a cooked input profile");const char* required[]={"move.forward","move.lateral","look.turn","walk","jump"};for(unsigned action=1;action<=5;++action){auto definition=std::find_if(inputs->profile().actions.begin(),inputs->profile().actions.end(),[&](const auto& candidate){return candidate.id==action;});require(definition!=inputs->profile().actions.end()&&definition->name==required[action-1]&&definition->kind==(action<=3?InputActionKind::Axis:InputActionKind::Button),"Character input semantic contract");}require(scene_skins.size()==1,"Character preset requires one loaded skin");character_resources=std::make_unique<darkangel::editor_app::CharacterPreviewResources>(darkangel::editor_app::load_character_preview(args.registry,args.cas,args.character_clips,scene_skins.begin()->second.model,scene_role(args.character_player),registry_spawns(args.registry),args.character_collision.empty()?AssetId{}:AssetId::parse(args.character_collision),registry_encounter_npcs(args.registry)));shell.native_controls=true;if(args.character_clips.starts_with("kit:"))editor.authoring_kit=AssetId::parse(args.character_clips.substr(4));
    // A party guest launched after its host prepared Play with a Player gets "player:<id>": it selects that Player, so it can prepare later scenes too.
    else if(args.character_clips.starts_with("player:")&&editor.assets){editor.authoring_player=AssetId::parse(args.character_clips.substr(7));editor.authoring_kit=AssetId::parse(editor.authoring.open(*editor.assets,editor.authoring_player).value.at("loadout").at("kit").get<std::string>());}if(character_resources->spawns.size()!=editor.document->plan().spawns.size())editor.log("Some spawn points reference NPCs outside the loaded registry; they join when Play prepares the scene from the project sources.",darkangel::editor_app::ConsoleSeverity::Warning);
        // Every cue asset of the project, cooked and loaded from its frozen product. Cues are not yet in the Play package through the actions and
        // effects that name them, so they are packaged on their own; a session started from a cooked registry alone uses the cues in that registry.
        auto load_play_cues=[&]{std::vector<darkangel::CueDefinition> cues;try{
                play_cue_closure=0;if(editor.assets){editor.assets->scan();std::set<AssetId> held;std::set<std::string> keys;
                    // Cues the Play package reached through the effects that bind them come first. The project's other cues answer the keys nothing names by asset (action cue blocks, footsteps).
                    if(!args.registry.empty()&&std::filesystem::is_regular_file(args.registry)){try{for(auto& cue:darkangel::load_cooked_cues(args.registry,args.cas)){held.insert(cue.id);keys.insert(cue.key);cues.push_back(std::move(cue));}}catch(const std::exception& error){cues.clear();held.clear();keys.clear();editor.log(std::string("The Play package's cues could not be read: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}}
                    play_cue_closure=cues.size();std::vector<AssetId> roots;for(const auto& asset:editor.assets->assets())if(std::filesystem::path(asset.path).extension()==".dacue"&&!held.contains(asset.id))roots.push_back(asset.id);
                    if(roots.size()>64){editor.log("This project has more than 64 cue assets outside the Play package; Play uses the first 64 of them.",darkangel::editor_app::ConsoleSeverity::Warning);roots.resize(64);}
                    if(!roots.empty()){const auto registry=editor.assets->cas_path().parent_path()/("cues-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);for(auto& cue:darkangel::load_cooked_cues(registry,editor.assets->cas_path())){if(keys.insert(cue.key).second)cues.push_back(std::move(cue));else editor.log("Two cues answer to the key "+cue.key+"; "+cue.name+" is not used.",darkangel::editor_app::ConsoleSeverity::Warning);}}}
                else cues=darkangel::load_cooked_cues(args.registry,args.cas);
                // The weapon trails and decals the cues name, by the identities in the cue products.
                play_trails.clear();play_decals.clear();if(editor.assets){std::vector<AssetId> primitives;for(const auto& cue:cues)for(const auto id:{cue.trail_asset,cue.decal_asset})if(id!=AssetId{}&&std::find(primitives.begin(),primitives.end(),id)==primitives.end())primitives.push_back(id);
                    if(!primitives.empty()&&primitives.size()<=64){const auto registry=editor.assets->cas_path().parent_path()/("primitives-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,primitives,registry);for(auto& trail:darkangel::load_cooked_trails(registry,editor.assets->cas_path()))play_trails[trail.id]=std::move(trail);for(auto& decal:darkangel::load_cooked_decals(registry,editor.assets->cas_path()))play_decals[decal.id]=std::move(decal);}}
                // A cue's visual names an effect source by its path; each is cooked, packaged and loaded into the service.
                play_effects.clear();if(vfx&&editor.assets){std::vector<AssetId> effects;std::vector<std::string> names;for(const auto& cue:cues){if(cue.visual.empty()||std::find(names.begin(),names.end(),cue.visual)!=names.end())continue;bool found=false;for(const auto& asset:editor.assets->assets())if(cue.visual_asset!=AssetId{}?asset.id==cue.visual_asset:asset.path==cue.visual){effects.push_back(asset.id);names.push_back(cue.visual);found=true;}if(!found)editor.log("Cue "+cue.name+" names a visual effect that is not in the project: "+cue.visual,darkangel::editor_app::ConsoleSeverity::Warning);}
#ifdef DAE_AUDIO
                    // A cue's audio names a sound source by its path, the same way.
                    play_sounds.clear();if(ensure_audio()){std::vector<AssetId> sounds;for(const auto& cue:cues){if(cue.audio.empty()||play_sounds.contains(cue.audio))continue;AssetId found=cue.audio_asset;if(found==AssetId{})for(const auto& asset:editor.assets->assets())if(asset.path==cue.audio)found=asset.id;
                            if(found==AssetId{}){editor.log("Cue "+cue.name+" names a sound that is not in the project: "+cue.audio,darkangel::editor_app::ConsoleSeverity::Warning);continue;}sounds.push_back(found);play_sounds[cue.audio]=found;}
                        load_audio(sounds);}
#endif
                    if(!effects.empty()&&effects.size()<=64){const auto registry=editor.assets->cas_path().parent_path()/("vfx-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,effects,registry);play_effect_generations.clear();for(std::size_t index=0;index<effects.size();++index){const auto cooked=darkangel::load_cooked_vfx(registry,editor.assets->cas_path(),effects[index]);vfx->load(cooked);play_effects[names[index]]=effects[index];play_effect_generations[names[index]]=cooked.generation.substr(0,12);}}}
            }catch(const std::exception& error){cues.clear();editor.log(std::string("Cues are unavailable for this Play: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}
            return cues;};
        editor.prepare_gameplay=[&,load_play_cues](const SpawnPlan& plan,const World& world){navigation_polygons=0;behavior_running.clear();hud_target.reset();// Every scene session of this Play gets its own controller source: behaviours run on the navigation mesh of the collision that session simulates.
            make_controller=[&]{SpawnController controller;
#ifdef DAE_BEHAVIOR
            // A spawned NPC with an authored behaviour runs its tree on the navigation mesh of this session's collision; one without a behaviour stays passive.
            // Held by the controller source itself: an encounter asks it for controllers long after this preparation has returned.
            auto mesh_slot=std::make_shared<std::shared_ptr<const darkangel::NavigationMesh>>();
            controller=[&,mesh_slot](const darkangel::editor_app::PreparedSpawn& spawn,const darkangel::CollisionDefinition& collision)->std::shared_ptr<darkangel::ActorController>{auto& session_mesh=*mesh_slot;if(!spawn.behavior)return {};if(!session_mesh)session_mesh=navigation_for(collision);navigation_polygons=session_mesh->polygons();if(behavior_running.empty())behavior_running=spawn.behavior->name;return darkangel::make_behavior_controller(*spawn.behavior,session_mesh,spawn.tuning);};
#endif
            return controller;};
#ifdef DAE_EDITOR_GNS
            if(!args.join.empty())make_controller=[] {return SpawnController{};};
#endif
            auto controller=make_controller();
            companions.clear();if(character_resources->combat)for(int seat=1;seat<shell.players;++seat)companions.push_back(darkangel::party_member(unsigned(seat)));director_second.reset();script_at=0;script_interactions=0;
            auto first_scene=std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(plan,world,*character_resources,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);},controller));first_scene->scene=editor.document->root();character_player=first_scene->player;
            play_ledger=darkangel::WorldLedger{};parked_documents.clear();parked_paths.clear();globals_scene=authoring_scene=editor.document->root();authoring_path=editor.path;travel_preview.reset();travel_load=TravelLoad{};travel_arrivals=travel_failures=travel_overlay_frames=0;travel_failure.clear();travel_was_busy=false;player_script_at=0;player_script_interactions=0;
            travel=std::make_unique<darkangel::SceneTransitionCoordinator>(scene_provider,play_ledger,std::vector<StableId>(companions.begin(),companions.end()));if(args.travel_hold>=0)travel->minimum_seconds=args.travel_hold;
#ifdef DAE_EDITOR_GNS
            travel->inventory_profile=darkangel::editor_app::local_player_profile(args.local_profile==4?0:args.local_profile);
            const bool network_host=args.join.empty()&&!args.exercise_two_players&&(shell.players>1||!args.listen.empty());
            if(network_host){party_host=std::make_unique<darkangel::editor_app::EditorPartyHost>(darkangel::editor_app::local_player_profile(args.local_profile==4?0:args.local_profile),unsigned(shell.players),args.listen);
                travel->build_session=[&](darkangel::PreparedScene& scene,StableId entry,const darkangel::SceneStart& start,std::span<const darkangel::ScenePlayer> others){return party_host->build(scene,entry,start,others);};
                travel->ready=[&](CharacterSceneSession& session){return party_host->ready(session);};travel->resumed=[&]{party_host->resume();};travel->cancelled=[&](std::string_view reason){party_host->cancel(reason);};travel->construction_text=[&]{return party_host->status();};travel->party_size=[&]{return party_host->members();};}
#endif
            // What hangs off a session is dropped before the coordinator destroys it, and built again for the next one.
            travel->unloading=[&]{
#ifdef DAE_HUD
                runtime_inventory.reset();runtime_altar.reset();shell.inventory_open=false;
#endif
                hud_target.reset();if(character)character->present_cues(nullptr);cue_player.reset();cue_effects.bind(nullptr,nullptr,{});cue_primitives.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
                cue_sounds.bind(nullptr,nullptr,{});audio_director.reset();
#endif
                director.reset();director_second.reset();character=nullptr;};
            travel->loaded=[&]{character=travel->session();character_player=travel->scene()->player;
                // The Preview world (scripts) of the scene the editor's document now is.
                if(travel_preview&&travel->scene()->scene==globals_scene){editor.preview.retire();editor.preview.activate(std::move(travel_preview));}
                attach_session(editor.document->plan());};
            #ifdef DAE_EDITOR_GNS
            if(!args.join.empty()){
                const bool local=args.join.starts_with("127.0.0.1:");const auto slot=args.local_profile==4?(local?args.player:0):args.local_profile;
                party_guest=std::make_unique<darkangel::editor_app::EditorPartyGuest>(darkangel::editor_app::local_player_profile(slot),args.player,args.join,scene_provider,args.party_mismatch);
                party_guest->unloading=travel->unloading;party_guest->loaded=[&]{character=party_guest->session();character_player=party_guest->owner();if(travel_preview){editor.preview.retire();editor.preview.activate(std::move(travel_preview));}attach_session(editor.document->plan());};
                party_guest->initial(std::move(first_scene));travel.reset();shell.client_only=true;
            }else
#endif
            {// Play from the save slot: its ledger and inventories, and the party at its checkpoint (in this scene, or travelling there).
                std::optional<darkangel::WorldDestination> checkpoint;
                if(play_saves&&play_saves->play_from_slot){std::string warning;try{if(auto loaded=play_saves->load(warning)){play_ledger=std::move(loaded->ledger);travel->restore_inventory(std::move(loaded->inventories));checkpoint=darkangel::read_checkpoint(play_ledger);editor.log("Play starts from the save slot.");}else editor.log("The save slot is empty; Play starts fresh.");}catch(const std::exception& error){editor.log(std::string("The save could not be loaded: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}if(!warning.empty())editor.log(warning,darkangel::editor_app::ConsoleSeverity::Warning);}
                const bool here=checkpoint&&checkpoint->scene==first_scene->scene;
                travel->start(std::move(first_scene),here?checkpoint->altar:StableId{});character=travel->session();if(checkpoint&&!here&&!travel->request({checkpoint->scene,checkpoint->altar,"Play from save"}))editor.log("The saved checkpoint's scene could not be entered.",darkangel::editor_app::ConsoleSeverity::Warning);}
            save_play=[&](std::string reason){if(!play_saves||!travel||!travel->scene()||!character||!character->hosting())return;character->capture();auto* inventory=travel->inventory();
#ifdef DAE_EDITOR_GNS
                if(!inventory&&party_host)inventory=party_host->inventory();
#endif
                if(play_saves->save(darkangel::editor_app::encode_play_save(play_ledger,inventory,travel->scene()->scene,{}),std::move(reason)))editor.log("Saving the game.");};
            attach_session=[&,load_play_cues](const SpawnPlan& plan){
            // An effect cue names its cue asset; an action cue block only has a key.
            cue_player.reset();cue_effects.bind(nullptr,nullptr,{});cue_primitives.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
            cue_sounds.bind(nullptr,nullptr,{});
#endif
            cue_visual.clear();cue_audio.clear();if(character_resources->combat){if(!play_cues)play_cues=load_play_cues();cue_player=std::make_unique<darkangel::CuePlayer>([&](std::string_view key,AssetId cue)->std::optional<darkangel::CueDefinition>{for(const auto& known:*play_cues)if(cue!=AssetId{}&&known.id==cue)return known;for(const auto& known:*play_cues)if(known.key==key)return known;return std::nullopt;},&cue_effects,
#ifdef DAE_AUDIO
                    &cue_sounds
#else
                    &cue_audio
#endif
                    );character->present_cues(cue_player.get());
                cue_primitives.bind(&play_trails,&play_decals,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y),float(at.z)};});
                cue_effects.bind(vfx.get(),&play_effects,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y)+1.f,float(at.z)};});vfx_tick=0;
#ifdef DAE_AUDIO
                cue_sounds.bind(audio.get(),&play_sounds,[&](std::uint64_t network)->std::optional<std::array<float,3>>{if(!character)return std::nullopt;const auto identity=character->identity(network);if(!identity)return std::nullopt;const auto handle=character->presentation().find(*identity);if(!character->presentation().valid(handle))return std::nullopt;const auto& at=character->presentation().read(handle).transform;return std::array<float,3>{float(at.x),float(at.y)+1.f,float(at.z)};});
                // The scene's audio profile starts with Play; an encounter's is layered over it while the encounter runs.
                audio_director.reset();play_profiles.clear();encounter_profiles.clear();audio_tab.profile_audition=false;audio_in_play=true;
                if(audio){std::vector<AssetId> wanted;for(const auto& [object,profile]:plan.audio_profiles)wanted.push_back(profile);for(const auto& [object,component]:plan.encounters){try{if(const auto profile=darkangel::decode_encounter(component).audio_profile;profile!=AssetId{}){wanted.push_back(profile);encounter_profiles[object]=profile;}}catch(const std::exception&){}}
                    try{play_profiles=load_audio(wanted);for(const auto profile:wanted)if(!play_profiles.contains(profile))editor.log("An audio profile named by the scene is not in the project: "+profile.text(),darkangel::editor_app::ConsoleSeverity::Warning);}catch(const std::exception& error){editor.log(std::string("Audio profiles are unavailable for this Play: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}
                    audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();if(!plan.audio_profiles.empty())if(const auto found=play_profiles.find(plan.audio_profiles.begin()->second);found!=play_profiles.end())audio_director->scene(found->second);}
#endif
            }
            };if(character)attach_session(plan);
#ifdef DAE_EDITOR_GNS
            if(party_host){std::vector<std::string> child{"--registry",args.registry.string(),"--cas",args.cas.string(),"--model",args.model,"--scene",editor.path.string(),"--backend",args.backend,"--character-clips",args.character_clips,"--game-workspace"};
                if(!args.sources.empty())child.insert(child.end(),{"--sources",args.sources.string(),"--asset-cache",args.asset_cache.string()});if(!args.character_collision.empty())child.insert(child.end(),{"--character-collision",args.character_collision});if(!args.character_player.empty())child.insert(child.end(),{"--character-player",args.character_player});if(!args.player_asset.empty())child.insert(child.end(),{"--player-asset",args.player_asset});if(!args.input_script.empty())child.insert(child.end(),{"--player-script",args.input_script});if(args.hidden)child.push_back("--hidden");if(args.no_audio)child.push_back("--no-audio");if(args.party_mismatch)child.push_back("--party-mismatch");
                for(unsigned seat=1;seat<unsigned(shell.players);++seat){auto arguments=child;if(!args.capture.empty())arguments.insert(arguments.end(),{"--capture",(args.capture.parent_path()/(args.capture.stem().string()+"-player-"+std::to_string(seat)+args.capture.extension().string())).string()});if(!args.party_report.empty())arguments.insert(arguments.end(),{"--party-report",(args.party_report.parent_path()/(args.party_report.stem().string()+"-player-"+std::to_string(seat)+".json")).string()});party_host->launch(seat,std::move(arguments),std::filesystem::path(".cache")/"party"/("player-"+std::to_string(seat)+".log"));}editor.log("Party host listening at "+party_host->address());}
#endif
            editor.log(character_resources->combat?"Native Loopback combat Play: light attack uses the cooked kit, action and authoritative target damage. Click Game for input.":"Native Loopback character Play: locomotion and solid box collision proxies. Use Game to play; click its viewport for input.");};
        shell.draw_gameplay_inspector=[&]{
            if(!character||!character_resources->combat){ImGui::TextDisabled("Start combat Play to inspect actor state.");return;}
            const auto& content=*character_resources->combat;auto confirmed=character->ability(),predicted=character->predicted_ability();if(!confirmed)return;
            ImGui::Text("Confirmed tick %llu | predicted tick %llu",static_cast<unsigned long long>(confirmed->tick),static_cast<unsigned long long>(predicted?predicted->tick:0));ImGui::TextWrapped("Kit %s",content.kit->id.text().c_str());if(!content.actor_generation.empty())ImGui::Text("Player generation %.12s",content.actor_generation.c_str());
            if(ImGui::BeginTabBar("Gameplay inspector sections")){if(ImGui::BeginTabItem("Player")){AttributeSet spawn(content.attributes);if(ImGui::BeginTable("Gameplay attribute state",4,ImGuiTableFlags_BordersInnerV)){ImGui::TableSetupColumn("Attribute");ImGui::TableSetupColumn("Spawn");ImGui::TableSetupColumn("Confirmed");ImGui::TableSetupColumn("Predicted");ImGui::TableHeadersRow();for(const auto& definition:content.attributes){ImGui::TableNextColumn();ImGui::TextUnformatted(definition.name.c_str());ImGui::TableNextColumn();ImGui::Text("%.2f",spawn.value(definition.id));ImGui::TableNextColumn();if(auto value=darkangel::editor_app::debug_attribute(confirmed,definition.id))ImGui::Text("%.2f",*value);else ImGui::TextDisabled("--");ImGui::TableNextColumn();if(auto value=darkangel::editor_app::debug_attribute(predicted,definition.id))ImGui::Text("%.2f",*value);else ImGui::TextDisabled("--");}ImGui::EndTable();}
            ImGui::SeparatorText("Action and outcomes");ImGui::TextUnformatted(confirmed->active?"Presentation: confirmed ability action":"Presentation: locomotion graph");ImGui::Text("Graph phase %.3f",character->graph().phase);for(const auto& layer:character->graph_inputs().span()){auto id=layer.clip->definition().id;std::string name=id.text();for(const auto& asset:editor.asset_inventory)if(asset.id==id){name=std::filesystem::path(asset.path).filename().string();break;}ImGui::TextWrapped("Locomotion: %s / weight %.3f / tick %.2f",name.c_str(),layer.weight,layer.tick);}if(confirmed->action)ImGui::Text("Composer clock %.2f ticks",double(confirmed->action->clock)/action_tick_units);
            for(const auto& cooldown:confirmed->cooldowns)if(cooldown.second>confirmed->tick)ImGui::Text("Cooldown group %u: %llu ticks",cooldown.first,static_cast<unsigned long long>(cooldown.second-confirmed->tick));
            if(confirmed->reservation)for(const auto& cost:confirmed->reservation->costs)ImGui::Text("Reserved attribute %u: %.2f",cost.attribute,cost.amount);
            unsigned displayed=0;for(auto it=confirmed->operations.rbegin();it!=confirmed->operations.rend()&&displayed<8;++it,++displayed)ImGui::TextWrapped("Operation %llu: %s%s",static_cast<unsigned long long>(it->operation),darkangel::editor_app::ability_failure_text(it->failure),it->committed?" (committed)":"");
            if(confirmed->active&&ImGui::Button("Inspect active ability asset")){try{shell.open_native_asset(editor,confirmed->ability);}catch(const std::exception& error){editor.log(error.what());}}
            ImGui::SeparatorText("Player tags");auto player_effects=character->effects(character_player);const EffectFrame* owner_frame=player_effects&&player_effects->current()?&*player_effects->current():nullptr;
            if(content.tags)for(auto tag:confirmed->tags.values){const auto& definitions=content.tags->definitions();auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& definition){return definition.id==tag;});if(found==definitions.end())continue;ImGui::TextUnformatted(found->name.c_str());auto sources=darkangel::editor_app::tag_debug_sources(tag,*content.tags,confirmed,owner_frame,content.abilities,content.effects);for(const auto& source:sources)if(source.action)ImGui::Text("  Action activation %llu / block %u",static_cast<unsigned long long>(source.activation),source.block);else ImGui::Text("  Effect %llu / source actor %llu / epoch %llu",static_cast<unsigned long long>(source.effect),static_cast<unsigned long long>(source.source_network),static_cast<unsigned long long>(source.session_epoch));if(sources.empty())ImGui::TextDisabled("  Contributor not exposed in received state");}
            ImGui::EndTabItem();}auto effects_selected=shell.select_effect_debug?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;shell.select_effect_debug=false;if(ImGui::BeginTabItem("World")){const auto& world=character->world_events();ImGui::Checkbox("Draw encounter and trigger volumes in the viewports",&world_debug);
                static char event_name[49]="";ImGui::SetNextItemWidth(220);ImGui::InputText("##world-event",event_name,sizeof(event_name));ImGui::SameLine();ImGui::BeginDisabled(!darkangel::world_name_valid(event_name));if(ImGui::Button("Emit world event"))character->emit_world_event(event_name);ImGui::EndDisabled();
                ImGui::SeparatorText("World tags");if(world.tags().empty())ImGui::TextDisabled("None set.");for(const auto& tag:world.tags())ImGui::BulletText("%s",tag.c_str());
                ImGui::SeparatorText("Encounters");if(character->encounters().empty())ImGui::TextDisabled("This scene has no encounters. Place one from the Scene tab's Place panel.");
                for(auto encounter:character->encounters()){const auto status=*character->encounter(encounter);std::string name="Encounter";for(const auto& origin:editor.document->plan().origins)if(origin.object==encounter)name=origin.name;const char* phase=status.phase==darkangel::EncounterPhase::Waiting?"waiting":status.phase==darkangel::EncounterPhase::Running?"running":"complete";
                    if(status.waves)ImGui::Text("%s: %s, wave %u of %u, %u of %u spawned, %u alive",name.c_str(),phase,status.wave,status.waves,status.spawned,status.total,status.alive);else ImGui::Text("%s: %s, %u of %u spawned, %u alive",name.c_str(),phase,status.spawned,status.total,status.alive);
                    for(auto actor:character->actors())if(character->actor_encounter(actor)==encounter){auto handle=character->presentation().find(actor);const auto* config=character->actor(actor);if(character->presentation().valid(handle)&&config){const auto health=character->presentation().read(handle).health;ImGui::Text("   %s: Health %.0f / %.0f",config->name.c_str(),health.current,health.maximum);}}}
                ImGui::SeparatorText("Interactables");if(editor.document->plan().interactables.empty())ImGui::TextDisabled("None in this scene.");
                for(const auto& [object,json]:editor.document->plan().interactables)if(const auto used=character->interactable_used(object)){std::string name="Interactable";for(const auto& origin:editor.document->plan().origins)if(origin.object==object)name=origin.name;ImGui::Text("%s: used %u time(s)",name.c_str(),*used);}
                runtime_inventory.inspector(*character,character_player,*character_resources);
            if(const auto* outcome=character->last_interaction())ImGui::Text("Latest request: tick %llu, %s",static_cast<unsigned long long>(outcome->tick),darkangel::interaction_result_text(outcome->result));
                ImGui::SeparatorText("Trigger volumes");if(editor.document->plan().triggers.empty())ImGui::TextDisabled("None in this scene.");
                for(const auto& [object,json]:editor.document->plan().triggers)if(const auto fired=character->trigger_fired(object)){std::string name="Trigger volume";for(const auto& origin:editor.document->plan().origins)if(origin.object==object)name=origin.name;ImGui::Text("%s: fired %u time(s)",name.c_str(),*fired);}
                ImGui::SeparatorText("World events (latest last)");if(world.log().empty())ImGui::TextDisabled("None yet.");for(const auto& event:world.log())ImGui::Text("tick %llu  %s",static_cast<unsigned long long>(event.tick),event.name.c_str());
                ImGui::EndTabItem();}
            const auto cues_selected=cue_debug_selected?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;cue_debug_selected=false;if(ImGui::BeginTabItem("Cues",nullptr,cues_selected)){
                if(!cue_player||!play_cues)ImGui::TextDisabled("This session has no cue player.");else{
                    ImGui::TextWrapped("%zu cue asset(s) loaded. Visual cues play as effects and audio cues as sounds; the lists below are what each side was told.",play_cues->size());
                    ImGui::Text("Live %zu | keys without a cue asset %llu | dropped %llu",cue_player->live(),static_cast<unsigned long long>(cue_player->unresolved()),static_cast<unsigned long long>(cue_player->dropped()));
                    const auto who=[&](std::uint64_t network){std::string name="Actor "+std::to_string(network);if(const auto identity=character->identity(network)){if(*identity==character_player)name="Player";else if(const auto* known=character->actor(*identity))if(!known->name.empty())name=known->name;}return name;};
                    if(vfx){const auto drawn=vfx->stats();ImGui::Text("Visual effects: %zu loaded, %zu playing, %zu particles, %u quads in %u draws",drawn.effects,drawn.playing,drawn.instances,drawn.quads,drawn.draws);ImGui::Text("Trails: %zu live, %zu points. Decals: %zu. %u quad(s) of both drawn.",cue_primitives.trails.live(),cue_primitives.trails.points(),cue_primitives.decals.live(),drawn.primitives);if(drawn.unsupported)ImGui::TextWrapped("%u batch(es) skipped: they need shading the renderer does not have.",drawn.unsupported);if(cue_effects.missing())ImGui::TextWrapped("%u cue(s) named a visual effect that is not loaded.",cue_effects.missing());}else ImGui::TextWrapped("Visual effects are unavailable: %s",vfx_failed.c_str());
#ifdef DAE_AUDIO
                    if(audio){const auto heard=audio->stats();ImGui::Text("Audio (%s): %zu sound(s) loaded, %zu playing, %llu started",heard.device?"device":"silent driver",heard.sounds,heard.playing,static_cast<unsigned long long>(heard.started));if(cue_sounds.missing())ImGui::TextWrapped("%u cue(s) named a sound that is not loaded.",cue_sounds.missing());}else if(!audio_failed.empty())ImGui::TextWrapped("Audio is unavailable: %s",audio_failed.c_str());
#endif
                    ImGui::SeparatorText("Live cues");const auto live=cue_player->instances();if(live.empty())ImGui::TextDisabled("None.");
                    for(const auto& cue:live){if(cue.stopping_until)ImGui::TextWrapped("%s on %s: %s, started tick %llu, fading until tick %llu",cue.key.c_str(),who(cue.network).c_str(),cue.action?"action":"effect",static_cast<unsigned long long>(cue.started),static_cast<unsigned long long>(cue.stopping_until));else ImGui::TextWrapped("%s on %s: %s, started tick %llu",cue.key.c_str(),who(cue.network).c_str(),cue.action?"action":"effect",static_cast<unsigned long long>(cue.started));}
                    ImGui::SeparatorText("Visual (latest last)");if(cue_visual.log().empty())ImGui::TextDisabled("Nothing yet.");for(const auto& line:cue_visual.log())ImGui::TextWrapped("%s",line.c_str());
                    ImGui::SeparatorText("Audio (latest last)");if(cue_audio.log().empty())ImGui::TextDisabled("Nothing yet.");for(const auto& line:cue_audio.log())ImGui::TextWrapped("%s",line.c_str());}
                ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Actors",nullptr,effects_selected)){
                auto presented=[&](const char* label,StableId actor){if(auto death=character->death(actor)){ImGui::TextWrapped("%s: Health 0 - death timeline at tick %.0f of %u%s",label,*death,content.death->duration/action_tick_units,*death>=double(content.death->duration/action_tick_units-1)?" (final pose held)":"");return;}if(auto reaction=character->reaction(actor))ImGui::TextWrapped("%s: effect %llu shows its reaction timeline at tick %.0f of %u (priority %u)%s",label,static_cast<unsigned long long>(reaction->key.handle),reaction->tick,reaction->action->duration/action_tick_units,reaction->action->priority,reaction->suppressed==ReactionSuppression::ActiveAction?" - waiting: the actor's own action is active":reaction->suppressed==ReactionSuppression::Dead?" - suppressed: Health is 0":"");else ImGui::TextDisabled("%s: no current reaction",label);};
                ImGui::SeparatorText("Reaction presentation");presented("Player",character_resources->player);ImGui::TextDisabled("Chosen from current public effect instances: highest timeline priority, then latest onset. Presentation only.");
                if(character->actors().empty())ImGui::TextDisabled(content.encounters.empty()?"This scene has no NPC spawn points. Add one in the Scene tab.":"No actors yet: this scene's encounters have not brought any in.");
                auto actor_name=[&](StableId id){std::string name=id==character_resources->player?"Player":"another actor";if(const auto* known=character->actor(id))if(!known->name.empty())name=known->name;return name;};
                // One section per server-driven actor the session holds now, in simulation order: spawn-point NPCs and the enemies encounters brought in.
                for(const auto id:character->actors()){const auto* config=character->actor(id);if(!config)continue;const auto& actor=*config;const darkangel::editor_app::PreparedSpawn* prepared=nullptr;for(std::size_t index=0;index<content.actors.size()&&index<character_resources->spawns.size();++index)if(content.actors[index].id==id)prepared=&character_resources->spawns[index];
                    const auto owner=character->actor_encounter(id);if(!prepared&&owner)for(const auto& [npc,made]:character_resources->encounter_npcs)if(made.actor.generation==actor.generation&&made.actor.name==actor.name)prepared=&made.spawn;
                    ImGui::PushID(actor.id.text().c_str());ImGui::SeparatorText(((actor.name.empty()?std::string("Actor"):actor.name)+(owner?" (encounter)":"")).c_str());
                    auto handle=character->presentation().find(actor.id);if(character->presentation().valid(handle)){auto health=character->presentation().read(handle).health;ImGui::Text("Health %.0f / %.0f | faction %s",health.current,health.maximum,actor.faction.c_str());}
                    if(prepared&&prepared->behavior)ImGui::Text("Behaviour: %s",prepared->behavior->name.c_str());else ImGui::TextDisabled("No behaviour: this actor never acts.");
                    {const auto decision=character->controller_status(actor.id);if(!decision.empty())ImGui::TextWrapped("Decision: %s",decision.c_str());}
                    if(auto chosen=character->controller_target(actor.id))ImGui::Text("Chosen target: %s",actor_name(*chosen).c_str());
                    // What the actor's senses give its controller, as authority holds it.
                    if(const auto* perception=character->perception(actor.id)){const auto& senses=perception->senses();bool shown=senses_shown.contains(actor.id);if(ImGui::Checkbox("Draw senses in the viewports",&shown)){if(shown)senses_shown.insert(actor.id);else senses_shown.erase(actor.id);}
                        if(senses.everything)ImGui::TextUnformatted("Senses: sees everything");else if(senses.sensors.empty())ImGui::TextDisabled("Senses: none, this actor perceives nothing.");else ImGui::Text("Senses: %u sensor(s), eye at %.2f m, aware after %.2f s, forgets after %.1f s",unsigned(senses.sensors.size()),senses.eye_height,senses.awareness_seconds,senses.forget_seconds);
                        const auto now=character->motor().tick;auto self=character->presentation().find(actor.id);darkangel::Transform here{};if(character->presentation().valid(self))here=character->presentation().read(self).transform;
                        if(perception->actors().empty())ImGui::TextDisabled("Perceives nobody.");
                        for(const auto& other:perception->actors()){const std::string name=actor_name(other.id);const double dx=other.motor.position.x-here.x,dy=other.motor.position.y-here.y,dz=other.motor.position.z-here.z,distance=std::sqrt(dx*dx+dy*dy+dz*dz);
                            if(other.detected)ImGui::Text("  Detects %s at %.1f m, for %.1f s, %s",name.c_str(),distance,double(now>other.since?now-other.since:0)/60,other.visible?"line of sight":"no line of sight");else if(other.seen)ImGui::Text("  Lost %s %.1f s ago, last known %.1f m away (awareness %.0f%%)",name.c_str(),double(now>other.seen?now-other.seen:0)/60,distance,other.awareness*100);else ImGui::Text("  Becoming aware of %s: %.0f%%%s",name.c_str(),other.awareness*100,other.sensed?"":" (fading)");}}
                    if(auto receipt=character->controller_receipt(actor.id)){if(receipt->failure==darkangel::AbilityFailure::None)ImGui::Text("Last request: tick %llu started activation %llu",static_cast<unsigned long long>(receipt->tick),static_cast<unsigned long long>(receipt->handle.activation));else ImGui::Text("Last request: tick %llu refused (%s)",static_cast<unsigned long long>(receipt->tick),receipt->failure==darkangel::AbilityFailure::Resources?"not enough Stamina":receipt->failure==darkangel::AbilityFailure::Cooldown?"cooldown":receipt->failure==darkangel::AbilityFailure::TagRequirements?"blocked by a status":"not available");}
                    presented("Presentation",actor.id);
                    auto actor_effects=character->effects(actor.id);if(actor_effects&&actor_effects->current()){const auto& frame=*actor_effects->current();for(const auto& state:frame.effects){ImGui::Text("Effect %llu: %s",static_cast<unsigned long long>(state.handle),state.suppressed?"suppressed":"active");ImGui::TextWrapped("Definition %s | generation %.12s",state.definition.text().c_str(),state.generation.c_str());ImGui::Text("Source actor %llu / activation %llu",static_cast<unsigned long long>(state.credit.source_network),static_cast<unsigned long long>(state.credit.activation));if(state.end)ImGui::Text("Remaining %llu ticks | next period %llu",static_cast<unsigned long long>(state.end>frame.tick?state.end-frame.tick:0),static_cast<unsigned long long>(state.next_period));for(const auto& definition:content.effects)if(definition->id==state.definition&&definition->generation==state.generation){for(auto tag:definition->tags)for(const auto& entry:content.tags->definitions())if(entry.id==tag)ImGui::Text("  %s %s",state.suppressed?"Inactive configured tag:":"Grants",entry.name.c_str());for(const auto& modifier:definition->modifiers)ImGui::Text("  Configured modifier: attribute %u, magnitude %.2f",modifier.attribute,modifier.magnitude);}}}
                    ImGui::PopID();}
            ImGui::EndTabItem();}ImGui::EndTabBar();}
            ImGui::TextWrapped("Sources shown use available action and replicated effect state. Other contributor ownership is not exposed by aggregate tags.");
        };
        shell.gameplay_metrics=[&]{if(!character)return std::string("Character session stopped");std::ostringstream text;const auto chosen=choose_hud_target();const StableId shown=chosen?*chosen:StableId{};if(character_resources->combat){auto target=character->presentation().find(shown);if(character->presentation().valid(target)){auto health=character->presentation().read(target).health;text<<"Target Health "<<health.current<<" / "<<health.maximum<<"\n";}if(auto ability=character->predicted_ability()){for(const auto& attribute:ability->attributes)if(attribute.id==3)text<<"Stamina "<<attribute.value<<"\n";if(ability->reservation){double reserved{};for(const auto& cost:ability->reservation->costs)if(cost.attribute==3)reserved+=cost.amount;text<<"Reserved Stamina "<<reserved<<"\n";}text<<"Ability "<<(ability->active?"active":"idle")<<"\n";}if(auto status=character->effects(shown))for(const auto& cue:status->cues())text<<cue.key<<" "<<std::max(0.,(double(cue.end)-character->motor().tick)/60.)<<" s\n";if(auto reaction=character->reaction(shown))if(reaction->suppressed==ReactionSuppression::None&&!character->death(shown))text<<"Target reaction "<<reaction->tick<<" / "<<reaction->action->duration/action_tick_units<<" ticks\n";}if(character_resources->combat){if(auto death=character->death(shown))text<<"Target death "<<*death<<" / "<<character_resources->combat->death->duration/action_tick_units<<" ticks\n";}if(!darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability()).empty())text<<"Mask "<<darkangel::editor_app::RuntimeInventory::name(*character_resources,character->ability())<<"\n";if(!shell.camera_live.empty())text<<"Camera "<<shell.camera_live<<"\n";text<<"Simulation tick "<<character->motor().tick<<"\nPrediction pending "<<character->pending_prediction()<<"\nAbility pending "<<character->pending_abilities()<<"\nClock debt "<<character->debt()*1000<<" ms\nGraph phase "<<character->graph().phase;return text.str();};editor.stop_gameplay=[&]{
#ifdef DAE_HUD
            runtime_inventory.reset();runtime_altar.reset();shell.inventory_open=false;
#endif
            hud_target.reset();director.reset();shell.camera_live.clear();if(travel)travel->stop();travel.reset();
#ifdef DAE_EDITOR_GNS
            if(party_guest)party_guest->stop();party_guest.reset();if(party_host)party_host->stop();party_host.reset();
#endif
            character=nullptr;travel_preview.reset();travel_load=TravelLoad{};
            if(authoring_scene!=AssetId{}&&globals_scene!=authoring_scene)if(auto parked=parked_documents.find(authoring_scene);parked!=parked_documents.end()){editor.document=std::move(parked->second);editor.path=authoring_path;editor.selected={};}
            parked_documents.clear();parked_paths.clear();globals_scene=authoring_scene;cue_player.reset();
#ifdef DAE_AUDIO
            audio_director.reset();play_profiles.clear();encounter_profiles.clear();audio_in_play=false;
#endif
            cue_effects.bind(nullptr,nullptr,{});
#ifdef DAE_AUDIO
            cue_sounds.bind(nullptr,nullptr,{});
#endif
            shell.controls_focus=false;};editor.gameplay_view=[&]()->const World*{return character?&character->presentation():nullptr;};editor.update_gameplay=[&](double seconds){if(!character)return;
            // While the party travels its input is held.
            #ifdef DAE_EDITOR_GNS
            if(party_guest&&!party_guest->running()){character->pump();return;}if(party_host&&travel&&travel->busy()){character->pump();return;}
#endif
            // A long editor stall (cooking at Play start, a debugger) is not simulated time: the session catches up at most a quarter second.
            const double stalled=std::min(seconds,.25);if(travel&&travel->busy()){CharacterSceneInput held;held.yaw=character->predicted_motor().yaw;character->advance(stalled,held);}else character->advance(stalled,character_input);};editor.step_gameplay=[&]{if(character)character->step({0,0,character->motor().yaw});};
    }
#endif

#ifdef DAE_AGENT_ENDPOINTS
std::unique_ptr<AgentEndpoint> agent;EditorDocument* agent_document{};auto poll_agent=[&]{if(args.agent_descriptor.empty())return;if(agent_document!=editor.document.get()){agent.reset();agent=std::make_unique<AgentEndpoint>(*editor.document,args.agent_project,args.agent_authoring,args.agent_descriptor);agent_document=editor.document.get();editor.log("Issued scoped agent descriptor for this authoring document.");}agent->poll();};
#endif
    auto reload_model=[&]{auto candidate=store.prepare(args.registry,args.cas,id);auto resources=upload(device,*candidate.model,pso,pso_cull);std::map<AssetId,UploadedModel> other;for(const auto& [asset,unused]:scene_models)other.emplace(asset,upload(device,load_cooked_model(args.registry,args.cas,asset),pso,pso_cull));
#ifdef DAE_ANIMATION
        std::map<AssetId,UploadedSkin> skins;for(const auto& [asset,unused]:scene_skins)skins.emplace(asset,upload_skin(device,load_cooked_skinned_model(args.registry,args.cas,asset),args.skin_reference?pso.RawPtr():skin_pso.RawPtr(),args.skin_reference?pso_cull.RawPtr():skin_cull.RawPtr(),diagnostic_clip.get(),args.pose_tick,args.skin_reference));
#endif
#ifdef DAE_CHARACTER_SCENE
        std::unique_ptr<darkangel::editor_app::CharacterPreviewResources> preview_candidate;if(character_resources){require(skins.size()==1,"Character reload requires one skin");preview_candidate=std::make_unique<darkangel::editor_app::CharacterPreviewResources>(darkangel::editor_app::load_character_preview(args.registry,args.cas,args.character_clips,skins.begin()->second.model,scene_role(args.character_player),registry_spawns(args.registry),args.character_collision.empty()?AssetId{}:AssetId::parse(args.character_collision),registry_encounter_npcs(args.registry)));}
#endif
#ifdef DAE_CHARACTER_SCENE
        std::unique_ptr<InputManager> input_candidate;std::unique_ptr<darkangel::editor_app::NativeInput> native_candidate;
        if(preview_candidate&&preview_candidate->combat){const auto& profile=preview_candidate->combat->input;const char* semantics[]={"move.forward","move.lateral","look.turn","walk","jump"};for(unsigned i=1;i<=5;++i){auto action=std::find_if(profile.actions.begin(),profile.actions.end(),[&](const auto& a){return a.id==i;});require(action!=profile.actions.end()&&action->name==semantics[i-1]&&action->kind==(i<=3?InputActionKind::Axis:InputActionKind::Button),"Play kit input semantic contract mismatch");}input_candidate=std::make_unique<InputManager>(profile);auto controls=input_candidate->controls();native_candidate=std::make_unique<darkangel::editor_app::NativeInput>(controls);}
        if(preview_candidate){auto probe=darkangel::editor_app::prepare_character_preview(editor.document->plan(),editor.document->world(),*preview_candidate,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);});}
#endif
        context->Flush();context->WaitForIdle();scene_models=std::move(other);
#ifdef DAE_ANIMATION
        scene_skins=std::move(skins);
#ifdef DAE_CHARACTER_SCENE
        graph_pose.reset();shell.graph_preview_asset={};composer_pose.reset();composer_matrices.clear();shell.composer_clip={};
#endif
#endif

#ifdef DAE_CHARACTER_SCENE
        if(character&&!travel_preparing)editor.stop();if(preview_candidate)character_resources=std::move(preview_candidate);
        if(input_candidate){inputs=std::move(input_candidate);native_input=std::move(native_candidate);window_input=native_input.get();shell.input=inputs.get();shell.input_events.clear();combat_events.clear();shell.controls_focus=false;}

#endif
        store.publish(candidate);gpu=std::move(resources);lease=store.acquire();store.collect();editor.log("Published resource generation after GPU upload; retired unpinned data at GPU idle.");};
#ifdef DAE_CHARACTER_SCENE
    // Bakes (when bake) and returns the open scene's own collision, reading placed models from a registry that holds them.
    std::function<std::optional<AssetId>(const std::filesystem::path&,bool)> scene_collision_for=[&](const std::filesystem::path& registry,bool bake)->std::optional<AssetId>{
        if(!editor.assets||!character_resources||editor.path.empty())return std::nullopt;const auto root=std::filesystem::absolute(editor.assets->source_root());const auto scene_path=std::filesystem::absolute(editor.path);
        const auto& plan=editor.document->plan();if(!plan.collision_profiles.empty())return plan.collision_profiles.begin()->second;
        const auto file=darkangel::editor_app::scene_collision_file(editor.document->root(),scene_path,root);
        if(bake){const auto source=darkangel::editor_app::bake_scene_collision(file.asset,plan,editor.document->world(),character_resources->skin,[&](AssetId model){return load_cooked_model(registry,editor.assets->cas_path(),model);});
            if(!source)return std::nullopt;if(darkangel::editor_app::publish_scene_collision(file,*source))editor.log("Baked the scene's collision from its placed models: "+file.relative);editor.assets->cook(file.relative);}
        return std::filesystem::exists(file.source)?std::optional<AssetId>(file.asset):std::nullopt;};
    // Saving a scene rebakes its collision: package what it places, bake, publish.
    editor.scene_saved=[&]{if(!editor.assets||!character_resources||!editor.authoring_roots||!args.join.empty())return;auto roots=editor.authoring_roots();auto registry=editor.assets->cas_path().parent_path()/("bake-"+AssetId::random().text()+".json");
        darkangel::editor_app::package_authoring(*editor.assets,roots,registry);scene_collision_for(registry,true);std::error_code ignored;std::filesystem::remove(registry,ignored);};
    if(character_resources&&editor.assets&&editor.authoring_kit!=AssetId{}){editor.authoring_roots=[&]{std::set<AssetId> unique{id,editor.authoring_player==AssetId{}?editor.authoring_kit:editor.authoring_player};for(auto npc:encounter_npcs())unique.insert(npc);for(const auto& [point,npc]:editor.document->plan().spawns){unique.insert(npc);for(auto visual:editor.authoring.worn_visuals(*editor.assets,npc))unique.insert(visual);}if(editor.authoring_player!=AssetId{})for(auto visual:editor.authoring.worn_visuals(*editor.assets,editor.authoring_player))unique.insert(visual);for(const auto& [object,asset]:editor.document->plan().models)unique.insert(asset);for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem"){unique.insert(asset.id);try{const auto& item=editor.authoring.open(*editor.assets,asset.id).value;if(item.contains("properties")&&item.at("properties").contains("mask")&&item.at("properties").at("mask").is_string()){const auto& mask=editor.authoring.open(*editor.assets,AssetId::parse(item.at("properties").at("mask").get<std::string>())).value;if(mask.contains("visual")&&mask.at("visual").is_string())unique.insert(AssetId::parse(mask.at("visual").get<std::string>()));}}catch(const std::exception&){}}if(travel_preparing)for(const auto& [asset,unused]:scene_models)unique.insert(asset);if(!args.character_collision.empty())unique.insert(AssetId::parse(args.character_collision));return std::vector<AssetId>(unique.begin(),unique.end());};editor.prepare_play_resources=[&]{
        require(travel_preparing||!editor.preview.active(),"Stop Play before preparing authored resources");if(!travel_preparing)play_cues.reset();auto roots=editor.authoring_roots();
        auto candidate=editor.assets->cas_path().parent_path()/("play-"+AssetId::random().text()+".json");
#ifdef DAE_EDITOR_GNS
        if(!args.join.empty()){require(!roots.empty(),"Guest scene has no package roots");editor.assets->package(roots.front(),candidate,std::span<const AssetId>(roots).subspan(1));}
        else
#endif
        darkangel::editor_app::package_authoring(*editor.assets,roots,candidate);
        // The scene plays with its own collision, baked from its placements: a placed or moved model never leaves Play
        // with stale collision. Only the host bakes; a guest uses the file its host wrote.
        if(const auto own=scene_collision_for(candidate,args.join.empty());own&&own->text()!=args.character_collision){args.character_collision=own->text();roots.push_back(*own);
#ifdef DAE_EDITOR_GNS
            if(!args.join.empty())editor.assets->package(roots.front(),candidate,std::span<const AssetId>(roots).subspan(1));else
#endif
            darkangel::editor_app::package_authoring(*editor.assets,roots,candidate);}
        auto old_registry=args.registry,old_cas=args.cas;auto old_clips=args.character_clips;
        try{args.registry=candidate;args.cas=editor.assets->cas_path();args.character_clips=editor.authoring_player==AssetId{}?"kit:"+editor.authoring_kit.text():"player:"+editor.authoring_player.text();reload_model();if(editor.authoring_player!=AssetId{})editor.authoring_kit=character_resources->combat->kit->id;}
        catch(...){args.registry=old_registry;args.cas=old_cas;args.character_clips=old_clips;throw;}
        editor.refresh_assets();editor.log("Prepared current scene and combat kit for a fresh Play session.");
    };}
    // Scene transitions in the editor's Play. The destination becomes the scene the editor's document, Play resources and
    // uploaded models are for, prepared exactly as a fresh Play of that scene would be; the session that is still running
    // keeps what it was built from. Every scene uses the collision asset this editor was started with: a destination whose
    // placements that asset does not match fails its preparation and the party stays where it is.
    auto find_scene=[&](AssetId scene)->std::filesystem::path{if(!editor.assets)return {};std::vector<std::filesystem::path> files;std::error_code failed;for(std::filesystem::recursive_directory_iterator file(editor.assets->source_root(),failed),end;!failed&&file!=end;file.increment(failed))if(file->is_regular_file()&&file->path().extension()==".dascene")files.push_back(file->path());
        for(const auto& file:files){try{std::ifstream input(file);const auto value=nlohmann::json::parse(input);if(value.contains("root")&&value.at("root").is_string()&&value.at("root").get<std::string>()==scene.text())return file;}catch(const std::exception&){}}
        return {};};
    enter_scene=[&](AssetId scene,std::unique_ptr<darkangel::EditorDocument> document,const std::filesystem::path& path){
        require(bool(editor.prepare_play_resources),"This editor session cannot prepare another scene for Play: it was started without project sources");
        parked_documents[globals_scene]=std::move(editor.document);parked_paths[globals_scene]=editor.path;editor.document=std::move(document);editor.path=path;globals_scene=scene;editor.selected={};
        travel_preparing=true;try{editor.prepare_play_resources();}catch(...){travel_preparing=false;throw;}travel_preparing=false;
        auto scripts=editor.document->scripts();travel_preview=editor.preview.prepare(editor.document->plan(),scripts,[&](AssetId asset){if(editor.prepare_model)editor.prepare_model(asset);});};
    auto drop_load=[&]{if(travel_load.document&&travel_load.parked){parked_paths[travel_load.scene]=travel_load.path;parked_documents[travel_load.scene]=std::move(travel_load.document);}travel_load=TravelLoad{};};
    scene_provider.on_begin=[&,drop_load](AssetId scene){drop_load();travel_load.scene=scene;};
    scene_provider.on_poll=[&,find_scene]()->darkangel::SceneLoadStatus{
        auto& load=travel_load;
        try{
            if(load.step==0){
                if(auto parked=parked_documents.find(load.scene);parked!=parked_documents.end()){load.document=std::move(parked->second);load.path=parked_paths[load.scene];load.parked=true;parked_documents.erase(parked);}
                else{load.path=find_scene(load.scene);if(load.path.empty())return {darkangel::SceneLoad::Failed,0,"No scene with the identity "+load.scene.text()+" is in the project"};load.document=darkangel::EditorDocument::open(load.path);require(load.document->root()==load.scene,"The scene file names another scene");}
                load.step=1;return {darkangel::SceneLoad::Working,.25,"Opening "+load.path.filename().string()};}
            if(load.step==1){load.parked=false;enter_scene(load.scene,std::move(load.document),load.path);load.step=2;return {darkangel::SceneLoad::Working,.7,"Preparing "+load.path.filename().string()};}
            if(load.step==2){require(bool(travel_preview),"The destination has no Preview world");load.prepared=std::make_unique<darkangel::PreparedScene>(darkangel::editor_app::prepare_character_scene(travel_preview->plan,*travel_preview->world,*character_resources,[&](AssetId asset){return load_cooked_model(args.registry,args.cas,asset);},make_controller()));load.prepared->scene=load.scene;load.step=3;}
            return {darkangel::SceneLoad::Ready,1,"Entering "+load.path.filename().string()};
        }catch(const std::exception& error){return {darkangel::SceneLoad::Failed,0,error.what()};}};
    scene_provider.on_take=[&]{return std::move(travel_load.prepared);};
    scene_provider.on_cancel=[drop_load]{drop_load();};
#endif
#ifdef DAE_CHARACTER_SCENE
    if(character_resources&&editor.assets){
        shell.prepare_composer=[&](AssetId clip,unsigned duration){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");
            auto registry=editor.assets->cas_path().parent_path()/("composer-"+AssetId::random().text()+".json");
            ComposerPackageFile cleanup{registry};const std::array roots{clip};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
            auto candidate=std::make_unique<darkangel::editor_app::ComposerPosePreview>(*character_resources,load_cooked_clip(registry,editor.assets->cas_path(),clip),duration);
            auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);
            editor.log("Prepared isolated frozen clip/character pose preview; gameplay remains stopped.");
        };
        shell.prepare_action_pose=[&](AssetId action){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;auto graph=AssetId::parse(kit.at("locomotion_stance").get<std::string>());
            auto candidate=darkangel::editor_app::prepare_authored_action_pose(*editor.assets,editor.authoring,*character_resources,action,graph);auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);editor.log("Prepared frozen action/locomotion pose preview from private drafts; gameplay stays stopped.");
        };
        ActorTagSnapshot preview_graph_tags;
        auto graph_parameters=[&](const darkangel::editor_app::GraphPosePreview& pose){GraphParameters parameters{shell.graph_speed,shell.graph_forward,shell.graph_lateral,1};if(auto dictionary=pose.tag_dictionary()){preview_graph_tags={dictionary->registry(),dictionary->generation(),{}};for(const auto& tag:shell.graph_preview_tags)preview_graph_tags.values.push_back(tag.get<TagId>());parameters.tags=&preview_graph_tags;}return parameters;};
        shell.scrub_composer=[&](double tick){require((composer_pose||graph_pose)&&!editor.preview.active(),"Pose preview unavailable during Play");if(graph_pose){require(std::isfinite(tick)&&tick>=0&&tick<=600,"Graph preview tick bounds");composer_matrices=graph_pose->sample(static_cast<unsigned>(tick),graph_parameters(*graph_pose));}else composer_matrices=composer_pose->sample(tick);};
        shell.prepare_character_pose=[&](AssetId asset,AssetId clip){
            require(!editor.preview.active(),"Stop Play before preparing Character preview");auto candidate=darkangel::editor_app::prepare_authored_character_pose(*editor.assets,editor.authoring,asset,clip);auto matrices=candidate.pose->sample(0);auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);authoring_character_definition=std::move(candidate.character.definition);composer_pose=std::move(candidate.pose);graph_pose.reset();shell.graph_preview_asset={};composer_matrices=std::move(matrices);shell.composer_clip=clip;shell.composer_duration=candidate.clip.definition.ticks*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;shell.character_preview_asset=asset;shell.composer_asset={};
        };
        shell.draw_graph_info=[&]{if(!graph_pose||shell.graph_preview_asset!=shell.authored_selection)return;if(ImGui::CollapsingHeader("Native graph blend result",ImGuiTreeNodeFlags_DefaultOpen)){const auto& state=graph_pose->state();auto parameters=graph_pose->parameters();ImGui::Text("Frozen graph %.12s | tick %llu | phase %.5f",state.generation.c_str(),static_cast<unsigned long long>(state.tick),state.phase);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",state.generation.c_str());ImGui::Text("Sampled speed %.3f | forward %.3f | lateral %.3f",parameters.speed,parameters.forward,parameters.lateral);const auto& inputs=graph_pose->inputs();ImGui::Text("Root output: %u native pose layers",inputs.count);for(unsigned i=0;i<inputs.count;++i){const auto& layer=inputs.layers[i];auto id=layer.clip->definition().id;std::string name=id.text();for(const auto& asset:editor.asset_inventory)if(asset.id==id){name=std::filesystem::path(asset.path).filename().string();break;}ImGui::TextWrapped("%s | weight %.4f | clip tick %.3f",name.c_str(),layer.weight,layer.tick);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",id.text().c_str());}ImGui::TextWrapped("These are frozen native sampling results, not authored weights or authoritative gameplay decisions.");}};
        shell.prepare_graph_pose=[&](AssetId character_asset,AssetId graph_asset){require(!editor.preview.active(),"Stop Play before graph preview");auto candidate=darkangel::editor_app::prepare_authored_graph_pose(*editor.assets,editor.authoring,character_asset,graph_asset);auto matrices=candidate.pose->sample(0,graph_parameters(*candidate.pose));auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);graph_pose=std::move(candidate.pose);composer_pose.reset();composer_matrices=std::move(matrices);shell.character_preview_asset={};shell.graph_preview_asset=graph_asset;shell.composer_asset={};shell.composer_clip={};shell.composer_duration=600*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;};
        shell.draw_character_info=[&]{if(!authoring_skin)return;const auto& model=authoring_skin->model;const auto& rig=model.rig.definition;ImGui::Text("%zu joints | %zu meshes",rig.joints.size(),model.meshes.size());if(ImGui::TreeNode("Skeleton")){for(unsigned i=0;i<rig.joints.size();++i)ImGui::Text("%u: %s | parent %d",i,rig.joints[i].key.c_str(),rig.joints[i].parent);ImGui::TreePop();}if(ImGui::TreeNode("Sockets")){for(const auto& [slot,joint]:rig.sockets)ImGui::Text("%s > %s",slot.c_str(),joint.c_str());ImGui::TreePop();}};
        shell.composer_info=[&]{if(graph_pose)return std::string("Graph | frozen generation ")+graph_pose->state().generation.substr(0,12)+" | fixed tick "+std::to_string(graph_pose->state().tick);std::string name="Clip";for(const auto& asset:editor.asset_inventory)if(asset.id==composer_pose->clip().id)name=std::filesystem::path(asset.path).stem().string();auto label=name+" | frozen clip "+std::string(composer_pose->generation().substr(0,12));if(auto action=composer_pose->action()){label+=" | action "+action->generation.substr(0,12);if(action->mask)label+=" | upper-body mask "+action->mask->generation.substr(0,12);}return label;};
    }
#endif
    if(args.ability_workspace&&editor.assets&&editor.authoring_kit!=AssetId{}){auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;shell.authored_selection=AssetId::parse(kit["slots"][1]["ability"].get<std::string>());}
    if(!args.authoring_asset.empty())shell.authored_selection=AssetId::parse(args.authoring_asset);if(args.items_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Items);if(!args.authoring_asset.empty())shell.item_selection=AssetId::parse(args.authoring_asset);}
    if(args.input_workspace){strcpy_s(shell.input_binding_filter,"attack.light");shell.input_authoring_asset=args.authoring_asset.empty()?AssetId{}:AssetId::parse(args.authoring_asset);shell.open_tool(darkangel::editor_app::ToolView::InputBindings);}
    if(!args.authoring_asset.empty()){if(args.character_workspace)shell.character_selection=AssetId::parse(args.authoring_asset);if(args.player_workspace)shell.player_selection=AssetId::parse(args.authoring_asset);if(args.npc_workspace)shell.npc_selection=AssetId::parse(args.authoring_asset);}
    if(!args.mask_editor.empty())shell.mask_editor=AssetId::parse(args.mask_editor);
    if(!args.player_asset.empty()){require(editor.assets!=nullptr,"Player selection needs a project mount");editor.authoring_player=AssetId::parse(args.player_asset);auto& player=editor.authoring.open(*editor.assets,editor.authoring_player).value;editor.authoring_kit=AssetId::parse(player.at("loadout").at("kit").get<std::string>());}

    auto reload_shader=[&](const std::string& text){auto next=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,false,text.c_str());auto next_cull=pipeline(device,constant_buffer,swap_desc.ColorBufferFormat,swap_desc.DepthBufferFormat,true,text.c_str());auto resources=upload(device,*lease.model,next,next_cull);std::map<AssetId,UploadedModel> other;for(const auto& [asset,unused]:scene_models)other.emplace(asset,upload(device,load_cooked_model(args.registry,args.cas,asset),next,next_cull));context->Flush();context->WaitForIdle();scene_models=std::move(other);gpu=std::move(resources);pso=std::move(next);pso_cull=std::move(next_cull);editor.log("Static shader candidate and material bindings committed at GPU idle.");};
    RefCntAutoPtr<ITexture> scene_color,scene_depth,scene_color_second,scene_depth_second;unsigned scene_width=700,scene_height=500;auto last=std::chrono::steady_clock::now();
#ifdef DAE_EDITOR_GNS
    nlohmann::json party_last_running,party_fullest;
    auto party_receipt=[&](unsigned frame){if(args.party_report.empty())return;
        nlohmann::json value{{"schema",1},{"process",GetCurrentProcessId()},{"role",args.join.empty()?"host":"guest"},{"frame",frame},{"scene",globals_scene.text()},{"epoch",std::uint64_t(party_guest?party_guest->epoch():party_host?party_host->epoch():0)},{"playing",bool(editor.preview.active())},{"running",bool(party_guest?party_guest->running():party_host&&travel&&!travel->busy())},{"ready",character&&character->bootstrapped()},{"players",nlohmann::json::array()},{"actors",nlohmann::json::array()},{"arrivals",travel_arrivals},{"failures",travel_failures},{"script_steps",player_script_at},{"script_count",player_script.size()},{"error",party_guest?party_guest->error():travel_failure}};
#ifdef DAE_HUD
        value["world_ui"]=nlohmann::json::array();if(hud&&!hud_failed)for(const auto& marker:hud->model().world_markers)value["world_ui"].push_back({{"label",marker.label},{"x",marker.x},{"y",marker.y},{"width",marker.width},{"health_bar",marker.health_bar},{"percent",marker.percent}});
#endif
        for(std::size_t line=editor.console.size()>12?editor.console.size()-12:0;line<editor.console.size();++line)value["console"].push_back(editor.console[line].message.substr(0,300));
        if(character&&character->bootstrapped()){value["owner"]=character_player.text();value["authority"]=character->hosting();value["tick"]=character->motor().tick;value["predicted_tick"]=character->predicted_motor().tick;const auto at=character->predicted_motor().position;value["predicted_position"]={at.x,at.y,at.z};value["pending_motor"]=character->pending_prediction();value["rebases"]=character->resynchronizations();value["client"]=character->client(character_player)->bootstrap_status();value["script_steps"]=player_script_at;
            for(const auto id:character->players()){auto handle=character->presentation().find(id);if(!character->presentation().valid(handle))continue;const auto& object=character->presentation().read(handle);value["players"].push_back({{"id",id.text()},{"position",{object.transform.x,object.transform.y,object.transform.z}},{"health",object.health.current},{"pose",character->pose(id)!=nullptr}});}
            for(const auto id:character->actors()){auto handle=character->presentation().find(id);if(!character->presentation().valid(handle))continue;const auto& object=character->presentation().read(handle);value["actors"].push_back({{"id",id.text()},{"health",object.health.current},{"pose",character->pose(id)!=nullptr}});}
            if(const auto* ability=character->ability()){value["equipped_masks"]=nlohmann::json::array();for(auto mask:ability->equipped_masks)value["equipped_masks"].push_back(mask==AssetId{}?nlohmann::json(nullptr):nlohmann::json(mask.text()));value["active_mask_slot"]=ability->loadout;value["highest_operation"]=ability->highest_operation;unsigned accepted{},refused{};for(const auto& operation:ability->operations){accepted+=operation.committed;refused+=operation.failure!=AbilityFailure::None;}value["accepted"]=accepted;value["refused"]=refused;}
            if(const auto* owner=character->client(character_player);owner&&owner->inventory()){const auto& inventory=*owner->inventory();value["inventory"]=nlohmann::json::parse(darkangel::encode_inventory(inventory.inventory));value["inventory_operation"]=inventory.highest_operation;value["inventory_pending"]=owner->inventory_pending();value["inventory_failure"]=int(inventory.receipt.failure);}
            if(const auto* outcome=character->last_interaction()){value["interaction"]=darkangel::interaction_result_text(outcome->result);value["interaction_object"]=outcome->object.text();}
            if(const auto* owner=character->client(character_player)){if(const auto* world=owner->world_state())value["world"]=nlohmann::json::parse(darkangel::encode_world_state(*world));value["downed"]=owner->downed();}
            value["scene_now"]=travel&&travel->scene()?travel->scene()->scene.text():std::string();value["arrivals"]=travel_arrivals;value["travel_failures"]=travel_failures;if(play_saves)value["save_status"]=play_saves->status();
            if(character->hosting()){value["movers"]=nlohmann::json::array();for(const auto& [id,at]:character->authority().movers())value["movers"].push_back({{"id",id.text()},{"at",{at.x,at.y,at.z}}});}
        }
        // What a stopped process last saw while it ran stays in its final receipt.
        if(value.at("running").get<bool>()&&value.contains("owner")){party_last_running=value;if(party_fullest.is_null()||value.at("players").size()>=party_fullest.at("players").size())party_fullest=value;else value["fullest"]=party_fullest;}else if(!party_last_running.is_null()){value["last_running"]=party_last_running;value["fullest"]=party_fullest;}
        if(!args.party_report.parent_path().empty())std::filesystem::create_directories(args.party_report.parent_path());const auto temporary=args.party_report.string()+".tmp";{std::ofstream file(temporary);file<<value.dump(2)<<'\n';require(bool(file),"Cannot write party diagnostic receipt");}// Something on the machine briefly holds a just-replaced evidence file; a diagnostic receipt waits for it, then skips this frame.
        bool published=false;for(unsigned attempt=0;attempt<100&&!published;++attempt){published=MoveFileExW(std::filesystem::path(temporary).c_str(),args.party_report.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE;if(!published)std::this_thread::sleep_for(std::chrono::milliseconds(20));}if(!published)std::cerr<<"Party diagnostic receipt was busy; skipped one update"<<std::endl;
    };
#endif
    auto scene_editing_exercise=darkangel::editor_app::exercises::make_scene_editing({args,editor,shell,import_baseline,import_count});
    auto acceptance_exercise=darkangel::editor_app::exercises::make_acceptance({args,editor,store,id,reload_model,reload_shader});
    #ifdef DAE_CHARACTER_SCENE
    auto authoring_source_exercise=darkangel::editor_app::exercises::make_authoring_sources({args,editor,shell,character_resources,character,import_baseline});
    auto media_exercise=darkangel::editor_app::exercises::make_media_exercises({args,editor,shell,vfx_workspace,
#ifdef DAE_AUDIO
        &audio_workspace,
#else
        nullptr,
#endif
        character});
    auto navigation_exercise=darkangel::editor_app::exercises::make_navigation_exercises({args,shell,
#ifdef DAE_NAVIGATION
        &navigation,
#else
        nullptr,
#endif
        [&]()->const darkangel::CollisionDefinition*{return character_resources?character_resources->collision.get():nullptr;},navigation_drawn});
#endif
    bool running=true;unsigned frame{};while(running && (!args.frames || frame<args.frames)){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        RECT current_client{};if(GetClientRect(window,&current_client)&&current_client.right>0&&current_client.bottom>0){resize_width=static_cast<unsigned>(current_client.right);resize_height=static_cast<unsigned>(current_client.bottom);}
        if(resize_width && resize_height&&(resize_width!=swap->GetDesc().Width||resize_height!=swap->GetDesc().Height)){context->WaitForIdle();swap->Resize(resize_width,resize_height);resize_width=resize_height=0;}
        auto now=std::chrono::steady_clock::now();auto elapsed_seconds=std::chrono::duration<double>(now-last).count();auto seconds=elapsed_seconds;last=now;seconds=std::clamp(seconds,0.,.1);
#ifdef DAE_CHARACTER_SCENE
        if(args.exercise_cues){shell.focus_gameplay_inspector=true;cue_debug_selected=true;}
        media_exercise->frame(frame);
        if(args.exercise_game_debug){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;if(frame==29){auto status=character?character->effects(first_actor()):nullptr;require(status&&status->current()&&!status->current()->effects.empty(),"Game debug needs active target effect");auto& effect=status->current()->effects.front();require(effect.credit.source_network&&effect.credit.activation,"Game debug missing captured source attribution");std::cout<<"Game inspector effect="<<effect.handle<<" source="<<effect.credit.source_network<<" activation="<<effect.credit.activation<<"\n";}}
        authoring_source_exercise->frame(frame);
        // Senses exercise: the player stands for a second, then walks forward. Records when the first actor's controller first has a target.
        if(args.exercise_senses&&character&&character_resources&&!character_resources->spawns.empty()){senses_all=true;const bool aware=character->controller_target(character_resources->spawns.front().actor).has_value();if(aware&&frame<60)senses_early=true;if(aware&&!senses_noticed){senses_noticed=character->motor().tick;senses_noticed_z=character->motor().position.z;}}
#endif
        if(args.select_spawn&&frame==1&&!editor.document->plan().spawns.empty())editor.selected=editor.document->plan().spawns.begin()->first;
        if(args.exercise_encounter)world_debug=true;
        // Background cooking: one uncooked source every half second while authoring in a visible editor. Gates run hidden and
        // leave their fixtures as they are, except the exercise that drops a new source into the project and waits for it.
        if(args.join.empty()&&!args.hidden&&!editor.preview.active()&&frame%30==29)editor.cook_pending(1);
        scene_editing_exercise->frame(frame);
        acceptance_exercise->frame(frame);
#ifdef DAE_CHARACTER_SCENE
        navigation_exercise->frame(frame);
        // Gate authoring: world objects placed through the Scene tab's own paths and saved, before Play starts.
        if(!args.world_place.empty()&&frame==0){std::ifstream plan(args.world_place);require(bool(plan),"--world-place plan missing");darkangel::editor_app::place_world_plan(editor,shell,nlohmann::json::parse(plan));if(editor.prepare_play_resources)editor.prepare_play_resources();}
        if(args.play_from_save&&frame==0&&play_saves)play_saves->play_from_slot=true;
        if((args.play||args.exercise_character||args.exercise_combat||args.exercise_heavy||!args.join.empty()||!args.listen.empty())&&frame==0)editor.play();
        // The rejected-activation gate: authority will refuse the first press of this session.
        if(args.exercise_reject&&frame==0){require(bool(character),"The refusal exercise needs a Play session");character->refuse_next_activation();}
        if(args.character_clips.empty())editor.update(seconds);
#else
        editor.update(seconds);
#endif
        const auto& desc=swap->GetDesc();
        if(!scene_color || scene_color->GetDesc().Width!=scene_width || scene_color->GetDesc().Height!=scene_height){context->Flush();context->WaitForIdle();TextureDesc color;color.Name="Editor scene viewport";color.Type=RESOURCE_DIM_TEX_2D;color.Width=scene_width;color.Height=scene_height;color.Format=swap_desc.ColorBufferFormat;color.BindFlags=BIND_RENDER_TARGET|BIND_SHADER_RESOURCE;device->CreateTexture(color,nullptr,&scene_color);color.Name="Editor scene depth";color.Format=swap_desc.DepthBufferFormat;color.BindFlags=BIND_DEPTH_STENCIL;device->CreateTexture(color,nullptr,&scene_depth);require(scene_color && scene_depth,"Viewport resource creation failed");}
        // The second local player's view has its own target, sized to its viewport of the previous frame.
        if(shell.second_view){const unsigned wide=std::max(1u,static_cast<unsigned>(shell.area_second.width)),high=std::max(1u,static_cast<unsigned>(shell.area_second.height));
            if(!scene_color_second||scene_color_second->GetDesc().Width!=wide||scene_color_second->GetDesc().Height!=high){context->Flush();context->WaitForIdle();TextureDesc color;color.Name="Editor second player viewport";color.Type=RESOURCE_DIM_TEX_2D;color.Width=wide;color.Height=high;color.Format=swap_desc.ColorBufferFormat;color.BindFlags=BIND_RENDER_TARGET|BIND_SHADER_RESOURCE;device->CreateTexture(color,nullptr,&scene_color_second);color.Name="Editor second player depth";color.Format=swap_desc.DepthBufferFormat;color.BindFlags=BIND_DEPTH_STENCIL;device->CreateTexture(color,nullptr,&scene_depth_second);require(scene_color_second&&scene_depth_second,"Second viewport resource creation failed");}}
        shell.game_image_second=shell.second_view&&scene_color_second?reinterpret_cast<ImTextureID>(scene_color_second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)):ImTextureID{};
        darkangel::editor_app::set_theme_dpi(GetDpiForWindow(window)/96.f);gui->NewFrame(desc.Width,desc.Height,desc.PreTransform);ImGuizmo::BeginFrame();auto image=reinterpret_cast<ImTextureID>(scene_color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));auto area=shell.draw(editor,desc.Width,desc.Height,args.backend.c_str(),lease.number,args.composer_playback?1./60:elapsed_seconds,image);
#ifdef DAE_CHARACTER_SCENE
        if(native_input){auto raw=native_input->poll();bool allow=shell.workspace==darkangel::editor_app::Workspace::Game&&shell.controls_focus&&!shell.controls_acquired&&editor.preview.active()&&!editor.paused;
            if(darkangel::editor_app::exercises::fixed_step_play(args)){raw.time_us=std::uint64_t(frame+1)*16667;raw.samples.clear();for(auto control:inputs->controls())raw.samples.push_back({control,darkangel::editor_app::exercises::synthetic_control_down(args,frame,control)?1.f:0.f,raw.time_us,false});allow=shell.workspace==darkangel::editor_app::Workspace::Game&&editor.preview.active()&&!editor.paused;}
            if(raw.overflow){allow=false;editor.log("Input queue overflow; controls cancelled until neutral.",darkangel::editor_app::ConsoleSeverity::Warning);}auto input_start=std::chrono::steady_clock::now();InputBatch batch;try{batch=inputs->update(raw.time_us,raw.samples,allow);}catch(const std::exception& error){batch=inputs->update(raw.time_us,{},false);allow=false;editor.stop();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.input_processing_us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-input_start).count();for(const auto& event:batch.events){if(character_resources&&character_resources->combat){bool routed=false;for(auto action:character_resources->combat->loadout_actions)routed|=action&&action==event.action;for(const auto& slot:character_resources->combat->kit->slots)routed|=slot.input_action&&slot.input_action==event.action;for(const auto& loadout:character_resources->combat->loadouts)if(loadout)for(const auto& slot:loadout->kit->slots)routed|=slot.input_action&&slot.input_action==event.action;if(routed)combat_events.push_back(event);}if(event.action==5&&event.edge==InputEdge::Pressed)shell.jump=true;if(event.action==7&&event.edge==InputEdge::Pressed)interact_pressed=true;if(event.edge!=InputEdge::Hold){shell.input_events.push_back(event);if(shell.input_events.size()>48)shell.input_events.erase(shell.input_events.begin());}}interact_held=allow&&inputs->state(7).down;if(allow){shell.forward=inputs->state(1).value;shell.lateral=inputs->state(2).value;shell.turn=inputs->state(3).value;shell.walk=inputs->state(4).down;}}
        if(!args.character_clips.empty()){character_input={};if(character){double forward=shell.forward,lateral=shell.lateral;auto length=std::hypot(forward,lateral);if(length>1){forward/=length;lateral/=length;}double scale=shell.walk?.4:1;const double look_step=darkangel::editor_app::exercises::fixed_step_play(args)?1./60:std::min(elapsed_seconds,.1);
            if(!director)director=std::make_unique<CameraDirector>(character_resources->cameras,character->predicted_motor().yaw);
            // Look input orbits the camera: stick axes through semantic actions, mouse while the viewport has control.
            double look_yaw=shell.turn*look_step*2,look_pitch=0;for(const auto& action:inputs->profile().actions)if(action.name=="look.pitch"&&action.kind==InputActionKind::Axis)look_pitch=inputs->state(action.id).value*look_step*1.5;
            ImVec2 mouse_delta{};if(shell.controls_focus&&!args.hidden)mouse_delta=ImGui::GetIO().MouseDelta;
            if(args.exercise_camera_look&&frame>=8&&frame<18)mouse_delta={6,-3};
            look_yaw-=mouse_delta.x*.0035;look_pitch+=mouse_delta.y*.0035;
            director->orbit(look_yaw,look_pitch);camera_held.clear();camera_tags.clear();
            for(const auto& camera:director->rig().cameras)if(!camera.input_action.empty())for(const auto& action:inputs->profile().actions)if(action.name==camera.input_action&&inputs->state(action.id).down&&std::find(camera_held.begin(),camera_held.end(),action.name)==camera_held.end())camera_held.push_back(action.name);
            if(character->ability())camera_tags=character->ability()->tags.values;
            const auto& live_camera=director->rig().cameras[director->select(camera_held,camera_tags)];const double camera_yaw=director->yaw();
            const double move_x=scale*(lateral*std::cos(camera_yaw)+forward*std::sin(camera_yaw)),move_z=scale*(forward*std::cos(camera_yaw)-lateral*std::sin(camera_yaw));
            // Free-look turns the character toward its movement; an aim camera keeps it facing with the camera.
            double yaw=character->predicted_motor().yaw,aim_pitch=0;if(live_camera.facing==CameraFacing::Camera){yaw=camera_yaw;
                // Crosshair convergence: aim from the muzzle of the kit's aimed ability at what the screen centre is on.
                double aim_range=60;MotorVec muzzle{0,1.3,.6};if(character_resources->combat)for(const auto& ability:character_resources->combat->abilities)if(ability->aim&&!ability->melee.empty()){aim_range=ability->aim->range;muzzle={ability->melee[0].offset[0],ability->melee[0].offset[1],ability->melee[0].offset[2]};break;}
                const auto& eye=director->pose();const MotorVec toward{eye.target[0]-eye.position[0],eye.target[1]-eye.position[1],eye.target[2]-eye.position[2]};
                if(std::hypot(std::hypot(toward.x,toward.y),toward.z)>1e-6){const auto solved=character->aim({eye.position[0],eye.position[1],eye.position[2]},toward,aim_range,muzzle);yaw=solved.yaw;aim_pitch=solved.pitch;}}
            else if(std::hypot(move_x,move_z)>.05){const double difference=std::remainder(std::atan2(move_x,move_z)-yaw,6.283185307179586),step=14*look_step;yaw=std::remainder(yaw+std::clamp(difference,-step,step),6.283185307179586);}
            character_input={move_x,move_z,yaw,shell.jump,false};character_input.pitch=aim_pitch;character_input.combat_events=std::move(combat_events);
            // The interact input asks for the interactable the HUD offers; the server decides.
            if(character&&character->interaction_prompt())interaction_offered=true;if(interact_pressed){interact_pressed=false;if(const auto offered=character->interaction_prompt()){character_input.interact=offered->id;++interactions_asked;}}else if(interact_held)if(const auto offered=character->interaction_prompt();offered&&offered->hold)character_input.interact=offered->id;combat_events.clear();}else combat_events.clear();
            if(shell.inventory_open&&character)character_input={0,0,character->predicted_motor().yaw};
            // Scripts pause while the party travels; await-scene waits for the party's n-th arrival.
            script_capture=false;bool travelling_now=travel&&travel->busy();
#ifdef DAE_EDITOR_GNS
            travelling_now|=party_guest&&!party_guest->running();
#endif
            auto run_script=[&character,&character_player,&character_resources,&script_signals,travelling_now,travel_arrivals,travel_failures,frame
#ifdef DAE_HUD
                ,&hud,&runtime_inventory,&runtime_altar
#endif
                ](darkangel::editor_app::exercises::ScriptRunner& runner,const darkangel::SceneClient& who,CharacterSceneInput& out){
                darkangel::editor_app::exercises::ScriptContext context{*character,who,script_signals,travelling_now,travel_arrivals,travel_failures,{}};
#ifdef DAE_HUD
                context.menu=[&hud,&runtime_inventory,&runtime_altar,&character,&character_player,&character_resources](std::string_view verb,unsigned value){
                    if(!hud)return false;if(verb.starts_with("inventory-"))return runtime_inventory.exercise(verb,value,*hud,character->client(character_player),*character_resources);
                    return runtime_altar.exercise(verb,value,*hud,character->client(character_player),character->hosting());};
#endif
                runner.frame(context,frame,out);};
            if(character&&player_script_at<player_script.size()){character_input={0,0,character->predicted_motor().yaw};run_script(owner_script,*character->client(character_player),character_input);}
            // A second local player: a script when one was given (gates), otherwise the keyboard and mouse while its viewport has control.
            if(character&&character->players().size()>1&&character->players()[1]!=character_player&&character->client(character->players()[1])){const auto second_id=character->players()[1];const auto* second=character->client(second_id);CharacterSceneInput other;other.yaw=second->predicted_motor().yaw;
                if(travelling_now){}
                else if(script_at<script.size())run_script(companion_script,*second,other);
                else if(shell.controls_player==1&&shell.controls_focus&&director_second){
                    // The focused viewport's player takes the keyboard and mouse; the first player stands still meanwhile.
                    if(!args.hidden){const auto moved=ImGui::GetIO().MouseDelta;director_second->orbit(-moved.x*.0035,moved.y*.0035);}
                    double forward=shell.forward,lateral=shell.lateral;const auto length=std::hypot(forward,lateral);if(length>1){forward/=length;lateral/=length;}const double facing=director_second->yaw();
                    other.x=lateral*std::cos(facing)+forward*std::sin(facing);other.z=forward*std::cos(facing)-lateral*std::sin(facing);if(std::hypot(other.x,other.z)>.05)other.yaw=std::atan2(other.x,other.z);
                    other.jump=character_input.jump;other.combat_events=std::move(character_input.combat_events);if(character_input.interact){character_input.interact.reset();if(const auto offered=second->interaction_prompt())other.interact=offered->id;}
                    character_input={0,0,character->predicted_motor().yaw};}
                character->input(second_id,std::move(other));}
#ifdef DAE_EDITOR_GNS
            if(party_host){party_host->pump(std::min(elapsed_seconds,.1),character,travel&&travel->busy()&&travel->status().phase<darkangel::TransitionPhase::Swapping);for(const auto& drop:party_host->drain_drops()){std::cout<<"Party host dropped seat="<<drop.seat<<" reason="<<drop.reason<<std::endl;editor.log("Player "+std::to_string(drop.seat)+": "+drop.reason,darkangel::editor_app::ConsoleSeverity::Warning);}}
            if(party_guest){party_guest->pump();shell.client_status=party_guest->status();
                // A guest counts its entries into a scene as the host counts the party's arrivals.
                if(party_guest->running()&&party_guest->epoch()!=guest_arrived_epoch){guest_arrived_epoch=party_guest->epoch();++travel_arrivals;}if(party_guest->finished())PostMessageW(window,WM_CLOSE,0,0);}
#endif
#ifdef DAE_EDITOR_GNS
            if(frame%60==0)party_receipt(frame);
            // A scripted party run does not idle after its Play was stopped by an error.
            if(!args.party_report.empty()&&travel_arrivals&&!editor.preview.active()&&(!player_script.empty()||!script.empty())){party_receipt(frame);throw std::runtime_error("Party Play stopped: "+(editor.console.empty()?std::string("no reason given"):editor.console.back().message));}
#endif
            editor.update(darkangel::editor_app::exercises::fixed_step_play(args)?1./60:elapsed_seconds);
            // Party travel: the coordinator takes what the session's triggers raised and advances a running transition one phase a frame.
            if(travel&&editor.preview.active()&&!editor.paused){
                try{travel->update(args.exercise_heavy?1./60:std::min(elapsed_seconds,.1));}catch(const std::exception& error){editor.log(std::string("Scene transition: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}
                if(travel_was_busy&&!travel->busy()){if(const auto& ended=travel->outcome()){if(ended->arrived){++travel_arrivals;
                        // A scripted walk that carried a player into the transition ends with it.
                        if(player_script_at<player_script.size()&&player_script[player_script_at].verb=="walk-to")++player_script_at;if(script_at<script.size()&&script[script_at].verb=="walk-to")++script_at;editor.log("The party arrived in "+editor.path.filename().string()+".");if(ended->save&&save_play)save_play("Rested at an altar");}else{++travel_failures;travel_failure=ended->failure;editor.log("The party did not travel: "+ended->failure,darkangel::editor_app::ConsoleSeverity::Warning);}}}
                travel_was_busy=travel->busy();
                // The save worker reports when a write has finished; the console says what happened.
                if(play_saves){const auto before=play_saves->status();play_saves->poll();if(play_saves->status()!=before&&!play_saves->status().starts_with("Saving"))editor.log(play_saves->status());}
                if(travel->fatal()){const auto why=travel->outcome()?travel->outcome()->failure:std::string("Play cannot continue");editor.stop();editor.log(why,darkangel::editor_app::ConsoleSeverity::Error);}
                // A transition that did not arrive can leave the editor prepared for the destination: it follows the party back.
                else if(!travel->busy()&&travel->scene()&&globals_scene!=travel->scene()->scene){const auto back=travel->scene()->scene;
                    try{auto parked=parked_documents.find(back);require(parked!=parked_documents.end(),"The scene the party is in is no longer open");auto document=std::move(parked->second);const auto file=parked_paths[back];parked_documents.erase(parked);enter_scene(back,std::move(document),file);editor.preview.retire();editor.preview.activate(std::move(travel_preview));travel->unloading();character=travel->session();attach_session(editor.document->plan());}
                    catch(const std::exception& error){const std::string why=error.what();editor.stop();editor.log("The scene the party is in could not be shown again: "+why,darkangel::editor_app::ConsoleSeverity::Error);}}}
            }
#endif

#ifdef DAE_AGENT_ENDPOINTS
poll_agent();
#endif
scene_width=std::max(1u,static_cast<unsigned>(area.width));scene_height=std::max(1u,static_cast<unsigned>(area.height));
        if(shell.reload_model){try{reload_model();}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_model=false;}
        if(shell.reload_shader){try{reload_shader(shader_text());}catch(const std::exception& error){context->Flush();context->WaitForIdle();editor.log(error.what(),darkangel::editor_app::ConsoleSeverity::Error);}shell.reload_shader=false;}
        auto* rtv=scene_color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);auto* dsv=scene_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);const float background[]={.015f,.025f,.045f,1};context->ClearRenderTarget(rtv,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport;viewport.Width=static_cast<float>(scene_color->GetDesc().Width);viewport.Height=static_cast<float>(scene_color->GetDesc().Height);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);
        using namespace DirectX;auto view=XMMatrixLookAtRH(XMVectorSet(0,gpu.radius*.2f,gpu.radius*std::max(3.8f,2.4f/(.4142f*viewport.Width/viewport.Height)),1),XMVectorZero(),XMVectorSet(0,1,0,0));if(!args.scene.empty())view=XMMatrixLookAtRH(XMVectorSet(26,24,34,1),XMVectorSet(0,1,0,1),XMVectorSet(0,1,0,0));if(args.camera_close)view=XMMatrixLookAtRH(XMVectorSet(4,2.5f,8,1),XMVectorSet(0,1.3f,3,1),XMVectorSet(0,1,0,0));
#ifdef DAE_CHARACTER_SCENE
        camera_fov=XM_PIDIV4;if(!character)director.reset();
        if(character&&shell.workspace==darkangel::editor_app::Workspace::Game){const auto& state=character->motor();if(!director)director=std::make_unique<CameraDirector>(character_resources->cameras,state.yaw);
            const auto& camera=director->update(darkangel::editor_app::exercises::fixed_step_play(args)?1./60:std::min(elapsed_seconds,.1),CameraTargets{{state.position.x,state.position.y,state.position.z},std::nullopt},camera_held,camera_tags);shell.camera_live=director->rig().cameras[director->live()].name;
            view=XMMatrixLookAtRH(XMVectorSet(float(camera.position[0]),float(camera.position[1]),float(camera.position[2]),1),XMVectorSet(float(camera.target[0]),float(camera.target[1]),float(camera.target[2]),1),XMVectorSet(0,1,0,0));camera_fov=float(camera.fov_degrees)*XM_PI/180.f;}
        else if(shell.workspace==darkangel::editor_app::Workspace::Player&&shell.camera_preview_visible&&shell.camera_preview_rig&&shell.camera_preview>=0&&shell.camera_preview<static_cast<int>(shell.camera_preview_rig->cameras.size())){
            // Authoring preview: one drafted virtual camera around the scene's player placement.
            CameraTargets targets;if(character_resources){auto entity=editor.displayed().find(character_resources->player);if(editor.displayed().valid(entity)){auto placed=editor.displayed().read(entity).transform;targets.follow={placed.x,placed.y,placed.z};}}
            CameraDirector preview(*shell.camera_preview_rig,shell.camera_preview_yaw*XM_PI/180.,shell.camera_preview_pitch);auto camera=preview.solve(static_cast<unsigned>(shell.camera_preview),targets);
            view=XMMatrixLookAtRH(XMVectorSet(float(camera.position[0]),float(camera.position[1]),float(camera.position[2]),1),XMVectorSet(float(camera.target[0]),float(camera.target[1]),float(camera.target[2]),1),XMVectorSet(0,1,0,0));camera_fov=float(camera.fov_degrees)*XM_PI/180.f;}
#endif
#ifdef DAE_CHARACTER_SCENE
        const bool character_render=shell.workspace==darkangel::editor_app::Workspace::Character&&authoring_skin&&shell.character_preview_asset==shell.character_selection;
        const bool graph_render=shell.workspace==darkangel::editor_app::Workspace::Animation&&authoring_skin&&graph_pose&&shell.graph_preview_asset==shell.authored_selection;
        const bool composer_render=shell.composer_visible&&(composer_pose||graph_pose)&&!editor.preview.active()&&(shell.workspace==darkangel::editor_app::Workspace::Ability||shell.workspace==darkangel::editor_app::Workspace::Animation||character_render);
        if(composer_render){float horizontal=shell.composer_distance*std::cos(shell.composer_pitch);view=XMMatrixLookAtRH(XMVectorSet(horizontal*std::sin(shell.composer_yaw),1+shell.composer_distance*std::sin(shell.composer_pitch),horizontal*std::cos(shell.composer_yaw),1),XMVectorSet(0,1.0f,0,1),XMVectorSet(0,1,0,0));}
#endif
        if(shell.workspace==darkangel::editor_app::Workspace::Vfx){const auto& preview=vfx_preview;view=XMMatrixLookAtRH(XMVectorSet(std::sin(preview.yaw)*preview.distance,preview.height+preview.distance*.25f,std::cos(preview.yaw)*preview.distance,1),XMVectorSet(0,preview.height,0,1),XMVectorSet(0,1,0,0));camera_fov=XM_PIDIV4;}
        auto projection=XMMatrixPerspectiveFovRH(camera_fov,viewport.Width/viewport.Height,gpu.radius*.01f,gpu.radius*40);XMFLOAT4X4 view_matrix,projection_matrix;XMStoreFloat4x4(&view_matrix,view);XMStoreFloat4x4(&projection_matrix,projection);shell.scene_view=view_matrix;shell.scene_projection=projection_matrix;shell.scene_camera=true;if(!args.hidden)shell.gizmo(editor,view_matrix,projection_matrix);
        // Placed models, then the characters of spawn points: at the spawn point while authoring, as the spawned actor in Play.
        std::vector<std::pair<StableId,AssetId>> drawn_objects(editor.document->plan().models.begin(),editor.document->plan().models.end());
        // Scatter: models along splines, derived each frame from the points as they stand. They are not scene objects.
        std::map<StableId,darkangel::Transform> scatter_poses;scatter_drawn=0;
        for(const auto& built:build_splines())if(built.curve&&built.definition.scatter){const auto model=built.definition.scatter->model;if(model!=id&&!scene_models.contains(model)&&editor.prepare_model){try{editor.prepare_model(model);}catch(const std::exception&){}}if(model!=id&&!scene_models.contains(model))continue;
            unsigned index=0;for(const auto& instance:darkangel::scatter_along(*built.curve,*built.definition.scatter,built.definition.closed)){darkangel::Transform pose;pose.x=instance.position.x;pose.y=instance.position.y;pose.z=instance.position.z;pose.yaw=instance.yaw;pose.scale=1;const auto derived=darkangel::scatter_instance(built.id,index++);scatter_poses.emplace(derived,pose);drawn_objects.emplace_back(derived,model);++scatter_drawn;}}
#ifdef DAE_CHARACTER_SCENE
        if(character_resources&&scene_skins.contains(character_resources->skin)){for(const auto& [point,npc]:editor.document->plan().spawns)drawn_objects.emplace_back(point,character_resources->skin);for(const auto& spawn:character_resources->spawns)drawn_objects.emplace_back(spawn.actor,spawn.skin);if(character)for(auto actor:character->actors())if(character->actor_encounter(actor))drawn_objects.emplace_back(actor,character_resources->skin);if(character)for(const auto member:character->players())if(member!=character_resources->player)drawn_objects.emplace_back(member,character_resources->skin);}
#endif
#ifdef DAE_CHARACTER_SCENE
        using SceneViewer=darkangel::SceneClient;
#else
        struct SceneViewer;
#endif
        // One pass over the scene's objects for a view. viewer: the client whose world and poses are shown; null for the editor's own.
        auto draw_objects=[&](const XMMATRIX& view,const XMMATRIX& projection,const SceneViewer* viewer){
        const darkangel::World* shown_world=&editor.displayed();(void)viewer;
#ifdef DAE_CHARACTER_SCENE
        if(viewer)shown_world=&viewer->presentation();
#endif
        std::vector<std::pair<StableId,darkangel::MotorVec>> mover_centers;const darkangel::WorldStateView* world_view=nullptr;const darkangel::CharacterSceneCombat* world_content=nullptr;
#ifdef DAE_CHARACTER_SCENE
        if(character&&editor.preview.active())if(const auto* viewer=character->client(character_player)){mover_centers=viewer->movers();world_view=viewer->world_state();world_content=viewer->combat();}
#endif
        for(const auto& [object_id,model_id]:drawn_objects){auto entity=shown_world->find(object_id);const auto scattered=scatter_poses.find(object_id);if(!shown_world->valid(entity)&&scattered==scatter_poses.end())continue;
#ifdef DAE_CHARACTER_SCENE
            if(composer_render&&(model_id!=character_resources->skin||(character_resources->player&&object_id!=character_resources->player)))continue;
#endif
            auto pose=scattered!=scatter_poses.end()?scattered->second:shown_world->read(entity).transform;
            if(world_view&&std::find(world_view->collected.begin(),world_view->collected.end(),object_id)!=world_view->collected.end())continue;
            if(world_content)for(const auto& [mover,center]:mover_centers)if(mover==object_id)for(const auto& placed:world_content->movers)if(placed.id==mover){pose.x+=center.x-placed.start.x;pose.y+=center.y-placed.start.y;pose.z+=center.z-placed.start.z;}
#ifdef DAE_CHARACTER_SCENE
            if(composer_render){pose={};pose.scale=1;}
#endif
if(shell.draft && editor.selected==object_id)pose=*shell.draft;

#ifdef DAE_ANIMATION
            auto skin=scene_skins.find(model_id);auto* selected_skin=skin==scene_skins.end()?nullptr:&skin->second;
#ifdef DAE_CHARACTER_SCENE
            if(character_render||graph_render)selected_skin=&*authoring_skin;
#endif
            auto* drawn_ptr=selected_skin?&selected_skin->gpu:nullptr;if(drawn_ptr&&!args.skin_reference){const std::vector<JointMatrix>* gameplay_pose=nullptr;
#ifdef DAE_CHARACTER_SCENE
                if(composer_render)gameplay_pose=&composer_matrices;else if(character&&selected_skin->model.rig.definition.id==character_resources->rig.definition.id&&selected_skin->model.rig.definition.signature==character_resources->rig.definition.signature)gameplay_pose=viewer?viewer->pose(object_id):character->pose(object_id);
#endif
                upload_palette(context,bone_buffer,*selected_skin,diagnostic_clip.get(),args.pose_tick,gameplay_pose);}
#else
            UploadedModel* drawn_ptr=nullptr;
#endif
            auto& drawn=drawn_ptr?*drawn_ptr:(model_id==id?gpu:scene_models.at(model_id));auto rotation=XMMatrixRotationRollPitchYaw(pose.pitch,pose.yaw,pose.roll);auto world=(args.scene.empty()?XMMatrixTranslation(-drawn.center[0],-drawn.center[1],-drawn.center[2]):XMMatrixIdentity())*XMMatrixScaling(pose.scale,pose.scale,pose.scale)*rotation*XMMatrixTranslation(pose.x,pose.y,pose.z);
            for(const auto& mesh:drawn.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for(const auto& part:mesh.parts){const auto& material=drawn.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0]*mesh.color[0],material.value.color[1]*mesh.color[1],material.value.color[2]*mesh.color[2],material.value.color[3]*mesh.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);

#ifdef DAE_ANIMATION
                    context->SetPipelineState((drawn_ptr&&!args.skin_reference)?(material.value.double_sided?skin_pso.RawPtr():skin_cull.RawPtr()):(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr()));
#else
                    context->SetPipelineState(material.value.double_sided?pso.RawPtr():pso_cull.RawPtr());
#endif
                    context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}
            }
#ifdef DAE_CHARACTER_SCENE
            const darkangel::editor_app::WornMask* wearing=nullptr;if(character&&character_resources){if(object_id==character_player||(!args.mask_tune.empty()&&character_resources->combat&&object_id==first_actor()))wearing=darkangel::editor_app::RuntimeInventory::worn(*character_resources,character->ability());else if(auto observed=character->observer_if_ready(object_id,double(character->motor().tick)))wearing=darkangel::editor_app::RuntimeInventory::worn(*character_resources,observed->frame.ability);}
            if(wearing&&shell.workspace==darkangel::editor_app::Workspace::Game){
                auto worn=*wearing;const auto* worn_pose=viewer?viewer->pose(object_id):character->pose(object_id);if(!args.mask_tune.empty()){std::array<float,7> tune{};std::stringstream stream(args.mask_tune);std::string item;for(unsigned index=0;index<7&&std::getline(stream,item,',');++index)tune[index]=std::stof(item);worn.position={tune[0],tune[1],tune[2]};worn.rotation={tune[3],tune[4],tune[5]};worn.scale=tune[6];}
                if(!worn_models.contains(worn.model)&&!worn_failed.contains(worn.model)){try{worn_models.emplace(worn.model,upload(device,load_cooked_model(args.registry,args.cas,worn.model),pso,pso_cull));}catch(const std::exception& error){worn_failed.insert(worn.model);editor.log(std::string("Mask visual unavailable: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);}}
                auto uploaded=worn_models.find(worn.model);
                if(uploaded!=worn_models.end()&&worn_pose&&worn.joint<worn_pose->size()){
                    XMFLOAT4X4 joint;std::memcpy(&joint,(*worn_pose)[worn.joint].values.data(),sizeof(joint));const float radians=XM_PI/180.f;
                    auto mask_world=XMMatrixScaling(worn.scale,worn.scale,worn.scale)*XMMatrixRotationRollPitchYaw(worn.rotation[0]*radians,worn.rotation[1]*radians,worn.rotation[2]*radians)*XMMatrixTranslation(worn.position[0],worn.position[1],worn.position[2])*XMLoadFloat4x4(&joint)*world;
                    auto mask_rotation=mask_world;mask_rotation.r[3]=XMVectorSet(0,0,0,1);for(unsigned axis=0;axis<3;++axis)mask_rotation.r[axis]=XMVector3Normalize(mask_rotation.r[axis]);
                    for(const auto& mesh:uploaded->second.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        for(const auto& part:mesh.parts){const auto& material=uploaded->second.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,mask_world*view*projection);XMStoreFloat4x4(&constants.rotation,mask_rotation);constants.color={material.value.color[0]*mesh.color[0]*worn.tint[0],material.value.color[1]*mesh.color[1]*worn.tint[1],material.value.color[2]*mesh.color[2]*worn.tint[2],material.value.color[3]*mesh.color[3]*worn.tint[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                            context->SetPipelineState(pso.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}}
                }
            }else if(!character&&!worn_models.empty()){worn_models.clear();worn_failed.clear();}
#endif
        }};
#ifdef DAE_CHARACTER_SCENE
        // While the party travels the editor's document and resources may already be the destination's; the loading panel covers the view.
        const bool travelling=travel&&travel->busy();
#else
        const bool travelling=false;
#endif
        if(!travelling)draw_objects(view,projection,nullptr);
#ifdef DAE_CHARACTER_SCENE
        // A second local player's view: the same objects from its own client's world and poses, through its own camera.
        shell.second_view=character&&character->players().size()>1&&character->client(character->players()[1])&&shell.workspace==darkangel::editor_app::Workspace::Game;
        if(shell.second_view&&scene_color_second){const auto* second=character->client(character->players()[1]);const auto& walker=second->predicted_motor();if(!director_second)director_second=std::make_unique<CameraDirector>(character_resources->cameras,walker.yaw);
            const auto& eye=director_second->update(args.exercise_heavy?1./60:std::min(elapsed_seconds,.1),CameraTargets{{walker.position.x,walker.position.y,walker.position.z},std::nullopt},std::span<const std::string>{},std::span<const TagId>{});
            auto* rtv_second=scene_color_second->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);auto* dsv_second=scene_depth_second->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);context->SetRenderTargets(1,&rtv_second,dsv_second,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(rtv_second,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv_second,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Viewport viewport_second;viewport_second.Width=static_cast<float>(scene_color_second->GetDesc().Width);viewport_second.Height=static_cast<float>(scene_color_second->GetDesc().Height);context->SetViewports(1,&viewport_second,scene_color_second->GetDesc().Width,scene_color_second->GetDesc().Height);
            const auto view_second=XMMatrixLookAtRH(XMVectorSet(float(eye.position[0]),float(eye.position[1]),float(eye.position[2]),1),XMVectorSet(float(eye.target[0]),float(eye.target[1]),float(eye.target[2]),1),XMVectorSet(0,1,0,0));const auto projection_second=XMMatrixPerspectiveFovRH(float(eye.fov_degrees)*XM_PI/180.f,viewport_second.Width/viewport_second.Height,gpu.radius*.01f,gpu.radius*40);
            XMStoreFloat4x4(&shell.game_view_second,view_second);XMStoreFloat4x4(&shell.game_projection_second,projection_second);if(!travelling)draw_objects(view_second,projection_second,second);
            context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);}
#endif
#ifdef DAE_CHARACTER_SCENE
        // Projectiles in flight, from the client's replicated list, pointing along their velocity.
        if(character&&shell.workspace==darkangel::editor_app::Workspace::Game){projectiles_seen=std::max(projectiles_seen,character->projectiles().size());
            for(const auto& shot:character->projectiles()){const double speed=std::sqrt(shot.velocity.x*shot.velocity.x+shot.velocity.y*shot.velocity.y+shot.velocity.z*shot.velocity.z);if(speed<1e-6)continue;
                auto rotation=XMMatrixRotationRollPitchYaw(float(-std::asin(std::clamp(shot.velocity.y/speed,-1.,1.))),float(std::atan2(shot.velocity.x,shot.velocity.z)),0);auto world=rotation*XMMatrixTranslation(float(shot.position.x),float(shot.position.y),float(shot.position.z));
                for(const auto& mesh:projectile_gpu.meshes){IBuffer* vb=mesh.vertices;Uint64 offset{};context->SetVertexBuffers(0,1,&vb,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    for(const auto& part:mesh.parts){const auto& material=projectile_gpu.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);constants.color={material.value.color[0],material.value.color[1],material.value.color[2],material.value.color[3]};constants.surface={material.value.roughness,material.value.metallic,0,0};void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                        context->SetPipelineState(pso.RawPtr());context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}}}}
#ifdef DAE_AUDIO
        // Audio every frame: the mixer as the project (or its open draft) has it, encounter profiles layered while their encounter runs, and the engine's clock outside Play.
        if(audio){if(frame%6==0||audio_tab.sound!=AssetId{}||shell.workspace==darkangel::editor_app::Workspace::Audio){const auto mix=project_mix();if(!audio_applied||!(*audio_applied==mix)){if(audio_director)audio_director->mix(mix);else audio->mix(mix);audio_applied=mix;}}
            if(audio_director&&character)for(const auto id:character->encounters()){const auto wanted=encounter_profiles.find(id);if(wanted==encounter_profiles.end())continue;const auto profile=play_profiles.find(wanted->second);if(profile==play_profiles.end())continue;const auto status=character->encounter(id);const std::uint64_t key=std::hash<std::string>{}(id.text());if(status&&status->phase==darkangel::EncounterPhase::Running)audio_director->push(key,profile->second);else audio_director->pop(key);}
            if(!character)audio->update(args.hidden?1./60:std::clamp(seconds,0.,.25));}
#endif
        // The VFX tab's preview: the selected effect in a loop at the origin, cooked again when its source changes on disk.
        if(vfx&&!character&&shell.workspace==darkangel::editor_app::Workspace::Vfx&&vfx_preview.selected!=AssetId{}){auto& preview=vfx_preview;
            if(editor.assets&&frame%30==0){std::error_code ignored;const auto written=std::filesystem::last_write_time(editor.assets->source_root()/preview.path,ignored);if(!ignored&&written!=preview.seen)vfx_cook();}
            const double step=args.hidden?1./60:seconds;vfx->update(step);if(preview.cooked&&!vfx->alive(preview.handle)){preview.idle+=step;if(preview.handle==0||preview.idle>.5){preview.handle=vfx->play(preview.selected,{0,preview.height,0});preview.idle=0;}}
            darkangel::VfxView shown;XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,view);std::memcpy(shown.view.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,projection);std::memcpy(shown.projection.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,XMMatrixInverse(nullptr,view));
            shown.eye={stored._41,stored._42,stored._43};shown.forward={-stored._31,-stored._32,-stored._33};shown.near_plane=gpu.radius*.01f;shown.far_plane=gpu.radius*40;
            try{vfx->draw(shown);}catch(const std::exception& error){vfx_preview.error=error.what();}}
        // Visual effects of the Play session: advanced on the session's tick clock, drawn after the opaque scene against its depth.
        if(vfx&&character&&shell.workspace==darkangel::editor_app::Workspace::Game){const auto tick=character->motor().tick;if(vfx_tick&&tick>vfx_tick){vfx->update(double(std::min<std::uint64_t>(tick-vfx_tick,120))/60.);
#ifdef DAE_AUDIO
                if(audio)audio->update(double(std::min<std::uint64_t>(tick-vfx_tick,120))/60.);
#endif
            }vfx_tick=tick;cue_effects.follow();
            {const auto& rig=character_resources->rig.definition;
                for(const auto& request:cue_primitives.trails.emitting()){const auto identity=character->identity(request.network);if(!identity)continue;const auto* pose=character->pose(*identity);const auto handle=character->presentation().find(*identity);if(!pose||!character->presentation().valid(handle))continue;
                    std::string key=request.definition->socket;if(const auto socket=rig.sockets.find(key);socket!=rig.sockets.end())key=socket->second;const auto joint=std::find_if(rig.joints.begin(),rig.joints.end(),[&](const auto& candidate){return candidate.key==key;});if(joint==rig.joints.end())continue;const auto index=std::size_t(joint-rig.joints.begin());if(index>=pose->size())continue;
                    const auto& at=character->presentation().read(handle).transform;XMFLOAT4X4 stored_joint;std::memcpy(&stored_joint,(*pose)[index].values.data(),sizeof(stored_joint));
                    const auto joint_world=XMLoadFloat4x4(&stored_joint)*XMMatrixScaling(float(at.scale),float(at.scale),float(at.scale))*XMMatrixRotationRollPitchYaw(float(at.pitch),float(at.yaw),float(at.roll))*XMMatrixTranslation(float(at.x),float(at.y),float(at.z));
                    const auto point=[&](const std::array<float,3>& offset){XMFLOAT3 out;XMStoreFloat3(&out,XMVector3TransformCoord(XMVectorSet(offset[0],offset[1],offset[2],1),joint_world));return std::array<float,3>{out.x,out.y,out.z};};
                    cue_primitives.trails.sample(request.instance,tick,point(request.definition->base),point(request.definition->tip));}
                cue_primitives.trails.advance(tick);cue_primitives.decals.advance(tick);std::vector<darkangel::PrimitiveVertex> built;std::vector<darkangel::VfxQuadVertex> quads;
                const auto hand=[&](bool additive){quads.clear();for(const auto& vertex:built)quads.push_back({vertex.position,vertex.color});built.clear();vfx->quads(quads,additive);};
                cue_primitives.decals.build(tick,built);hand(false);cue_primitives.trails.build(tick,built);hand(true);}
            darkangel::VfxView shown;XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,view);std::memcpy(shown.view.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,projection);std::memcpy(shown.projection.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,XMMatrixInverse(nullptr,view));
            shown.eye={stored._41,stored._42,stored._43};shown.forward={-stored._31,-stored._32,-stored._33};shown.near_plane=gpu.radius*.01f;shown.far_plane=gpu.radius*40;
#ifdef DAE_AUDIO
            if(audio){audio->listener(shown.eye,shown.forward,{0,1,0});cue_sounds.follow();}
#endif
            try{vfx->draw(shown);}catch(const std::exception& error){editor.log(std::string("Visual effects stopped: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Error);cue_effects.bind(nullptr,nullptr,{});vfx.reset();}}
#endif
        // Scripted shutdown waits for a coherent final view. A baseline refresh can
        // start during the same frame as quit; two arbitrary render frames are not a
        // delivery fence and would publish an unready final receipt.
#ifdef DAE_CHARACTER_SCENE
        if(script_quit&&character&&character->bootstrapped())args.frames=frame+1;
#endif
        auto* back=swap->GetCurrentBackBufferRTV();context->SetRenderTargets(1,&back,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(back,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);gui->Render(context);if(script_capture)script_captured=true;if(!args.capture.empty() && (script_capture||(args.frames && frame+1==args.frames&&!script_captured)))capture(device,context,back->GetTexture(),args.capture,args.capture_workspace?darkangel::editor_app::ViewArea{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height)}:area);swap->Present(args.hidden?0:1);++frame;
    }
#ifdef DAE_CHARACTER_SCENE
    window_input=nullptr;
    darkangel::editor_app::exercises::check_preview_results({args,editor,shell,import_baseline,character,composer_pose.get(),graph_pose.get(),authoring_skin.has_value(),composer_matrices.size()});
    // Presentation checks read what the frame loop drew; they run before gameplay checks stop Play.
    navigation_exercise->finish();
    if(args.exercise_spline){const auto built=build_splines();std::size_t points=0,expected=0;for(const auto& spline:built){points+=spline.points.size();if(spline.curve&&spline.definition.scatter)expected+=darkangel::scatter_along(*spline.curve,*spline.definition.scatter,spline.definition.closed).size();}
        require(!built.empty()&&splines_drawn==built.size()&&points>=2&&expected>=2&&scatter_drawn==expected,"Editor Scene: the scene's spline was not drawn, or its scattered models were not");std::cout<<"Editor spline splines="<<built.size()<<" points="<<points<<" length="<<(built.front().curve?built.front().curve->length():0.)<<" scatter="<<scatter_drawn<<"\n";}
    if(args.select_encounter){require(volumes_drawn>0,"Editor Scene: the encounter's volumes were not drawn");std::cout<<"Editor encounter_scene volumes_drawn="<<volumes_drawn<<"\n";}
    media_exercise->finish();
    if(args.exercise_cues){require(bool(character)&&bool(cue_player)&&bool(play_cues),"Cue exercise needs a combat session with a cue player");const auto list=[](const std::vector<std::string>& keys){std::string all;for(const auto& key:keys)all+=(all.empty()?"":",")+key;return all.empty()?std::string("none"):all;};
        // What the sinks hold is exactly what the player holds for them, and nothing was dropped.
        std::size_t visual_live=0,audio_live=0;for(const auto& [instance,held]:cue_visual.held()){(void)instance;require(character->identity(held.network).has_value(),"A live visual cue names an actor that is not in the session");++visual_live;}for(const auto& [instance,held]:cue_audio.held()){(void)instance;require(character->identity(held.network).has_value(),"A live audio cue names an actor that is not in the session");++audio_live;}
        require(cue_player->dropped()==0&&cue_player->live()>=std::max(visual_live,audio_live)&&cue_visual.starts()==cue_visual.releases()+visual_live&&cue_audio.starts()==cue_audio.releases()+audio_live,"Cue sinks and the cue player disagree about what is live");
        std::cout<<"Editor cues tick="<<character->motor().tick<<" assets="<<play_cues->size()<<" closure="<<play_cue_closure<<" live="<<cue_player->live()<<" visual_live="<<visual_live<<" audio_live="<<audio_live<<" refused="<<(args.exercise_reject?1:0)<<" visual_starts="<<cue_visual.starts()<<" audio_starts="<<cue_audio.starts()<<" audio_stops="<<cue_audio.stops()<<" unresolved="<<cue_player->unresolved()<<" visual="<<list(cue_visual.started())<<" audio="<<list(cue_audio.started());{const auto drawn=vfx?vfx->stats():darkangel::VfxStats{};std::cout<<" vfx_loaded="<<drawn.effects<<" vfx_playing="<<drawn.playing<<" vfx_particles="<<drawn.instances<<" vfx_quads="<<drawn.quads<<" vfx_draws="<<drawn.draws<<" vfx_unsupported="<<drawn.unsupported<<" vfx_missing="<<cue_effects.missing()<<" trails="<<cue_primitives.trails.live()<<" trail_points="<<cue_primitives.trails.points()<<" decals="<<cue_primitives.decals.live()<<" primitive_quads="<<drawn.primitives<<" primitive_missing="<<cue_primitives.missing();{std::cout<<" effect_generations=";bool first=true;for(const auto& [name,generation]:play_effect_generations){std::cout<<(first?"":";")<<name<<":"<<generation;first=false;}if(first)std::cout<<"none";}}
#ifdef DAE_AUDIO
        {std::cout<<" sound_generations=";bool first=true;for(const auto& [name,id]:play_sounds){std::cout<<(first?"":";")<<name<<":"<<(audio_generations.contains(id)?audio_generations.at(id).substr(0,12):std::string("none"));first=false;}if(first)std::cout<<"none";}{const auto heard=audio?audio->stats():darkangel::AudioStats{};std::cout<<" audio_service="<<(audio?1:0)<<" audio_device="<<(heard.device?1:0)<<" audio_loaded="<<heard.sounds<<" audio_played="<<heard.started<<" audio_missing="<<cue_sounds.missing();}
#endif
        std::cout<<"\n";}
    if(args.select_spawn){require(senses_drawn>0,"Editor Scene: the selected spawn point's senses were not drawn");std::cout<<"Editor senses_scene drawn="<<senses_drawn<<"\n";}
    darkangel::editor_app::exercises::check_gameplay_results({args,editor,shell,import_baseline,character,character_resources,director,travel,hud,
#ifdef DAE_HUD
        &hud_renderer,
#else
        nullptr,
#endif
        play_ledger,authoring_scene,companion_script,owner_script,first_actor,choose_hud_target,
        {projectiles_seen,navigation_polygons,behavior_running,senses_drawn,volumes_drawn,interactions_asked,interaction_offered,senses_early,
         senses_noticed,senses_noticed_z,travel_arrivals,travel_failures,travel_overlay_frames,travel_failure,bool(scene_color_second)&&hud_second_drawn}});
#endif
#ifdef DAE_EDITOR_GNS
    party_receipt(frame);
#endif
    #ifdef DAE_CHARACTER_SCENE
    authoring_source_exercise->finish();
#endif
    render.shutdown();std::cout<<"DarkAngel editor rendered "<<frame<<" frames with "<<args.backend<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"DarkAngelEditor: "<<e.what()<<'\n';return 1;}}
