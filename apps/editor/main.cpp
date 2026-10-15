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
#include "scene_splines.hpp"
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
#include "play_controller.hpp"
#include "gameplay_inspector.hpp"
#include "exercises/play_exercises.hpp"
#ifdef DAE_EDITOR_GNS
#include "network_play.hpp"
#endif
#ifdef DAE_HUD
#include "hud_overlay.hpp"
#include "play_hud.hpp"
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
    auto gpu=upload(device,*lease.model,pso,pso_cull);darkangel::editor_app::Controller editor(id,gpu.radius);darkangel::editor_app::Shell shell;if(!args.sources.empty())editor.configure_assets(args.sources,args.asset_cache);shell.select_workspace(args.character_workspace?darkangel::editor_app::Workspace::Character:args.npc_workspace?darkangel::editor_app::Workspace::Npc:args.player_workspace?darkangel::editor_app::Workspace::Player:args.animation_workspace?darkangel::editor_app::Workspace::Animation:args.ability_workspace?darkangel::editor_app::Workspace::Ability:args.assets_workspace?darkangel::editor_app::Workspace::Assets:(args.game_workspace||darkangel::editor_app::exercises::starts_play(args))?darkangel::editor_app::Workspace::Game:darkangel::editor_app::Workspace::Scene);darkangel::editor_app::exercises::select_startup_views(args,shell);if(args.tools_workspace)shell.open_tool(darkangel::editor_app::ToolView::InputBindings);if(args.navigation_tool)shell.open_tool(darkangel::editor_app::ToolView::Navigation);if(args.settings_workspace)shell.select_workspace(darkangel::editor_app::Workspace::Settings);if(!args.import_preview.empty())shell.open_import(args.import_preview);std::size_t import_count=editor.asset_inventory.size();
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
    std::optional<UploadedSkin> authoring_skin;CharacterDefinition authoring_character_definition;
    std::unique_ptr<darkangel::editor_app::GraphPosePreview> graph_pose;std::unique_ptr<darkangel::editor_app::ComposerPosePreview> composer_pose;std::vector<JointMatrix> composer_matrices;
    // What Play's cue player tells each side, for the Cues tab. Made before the media workspaces, whose sinks forward to it.
    darkangel::editor_app::CueRecorders cue_recorders;
    // Visual effects: one service on this device. Play's cue sinks play the effect asset behind each cue visual.
    darkangel::editor_app::VfxWorkspace vfx_workspace(editor);auto& vfx=vfx_workspace.service;
    vfx_workspace.start(device,context,int(swap_desc.ColorBufferFormat),int(swap_desc.DepthBufferFormat));
#ifdef DAE_AUDIO
    // Sounds, music and the Audio tab: one workspace for the process, its service made when the first Play or audition needs it.
    darkangel::editor_app::AudioWorkspace audio_workspace(editor,args,cue_recorders.audio);
    auto& audio=audio_workspace.service;auto& audio_director=audio_workspace.director;auto& play_profiles=audio_workspace.play_profiles;auto& encounter_profiles=audio_workspace.encounter_profiles;
    auto& audio_applied=audio_workspace.applied;auto& audio_tab=audio_workspace.state;auto project_mix=[&]{return audio_workspace.project_mix();};
#endif
    // The VFX tab: the selected effect is cooked and played in a loop in the preview, and cooked again whenever its source file changes on disk
    // (that is how an edit saved in Effekseer shows up). A refusal by the cooker is shown as it is.
    auto& vfx_preview=vfx_workspace.state;
    auto vfx_effects=[&]{return vfx_workspace.effects();};auto vfx_cook=[&]{vfx_workspace.cook();};auto vfx_select=[&](const darkangel::AssetInfo& asset){vfx_workspace.select(asset);};
    if(editor.assets)shell.draw_vfx_panel=[&]{vfx_workspace.draw_panel();};
    if(args.vfx_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Vfx);if(editor.assets)editor.refresh_assets();for(const auto& asset:vfx_effects())if(asset.path==args.vfx_select)vfx_select(asset);}
#ifdef DAE_NAVIGATION
    // Tools > Navigation: an explicit bake of the scene's cooked collision that Play reuses while the collision is unchanged.
    darkangel::editor_app::NavigationTool navigation;
    // The project's save slot lives beside its asset cache. Saving is the host's: a capture of the ledger and inventories at a safe point.
    std::unique_ptr<darkangel::editor_app::PlaySaves> play_saves;if(editor.assets)play_saves=std::make_unique<darkangel::editor_app::PlaySaves>(editor.assets->cas_path().parent_path()/"saves"/"project.dasave");
#endif
#ifdef DAE_HUD
    // Runtime HUD resources, and the game menus drawn into it.
    darkangel::editor_app::HudResources hud_resources(device);
    darkangel::editor_app::RuntimeInventory runtime_inventory;darkangel::editor_app::RuntimeAltar runtime_altar;
#endif
    // Play: the running session, its party, cue presentation, cameras and scripted input.
    std::function<void()> reload_model;
    darkangel::editor_app::PlayController play({args,editor,shell,vfx_workspace,
#ifdef DAE_AUDIO
        &audio_workspace,
#else
        nullptr,
#endif
#ifdef DAE_NAVIGATION
        &navigation,
#else
        nullptr,
#endif
#ifdef DAE_HUD
        &hud_resources,
#else
        nullptr,
#endif
        cue_recorders,play_saves,runtime_inventory,runtime_altar,window_input,[&]{PostMessageW(window,WM_CLOSE,0,0);},[&]{reload_model();},scene_models});
    auto& character=play.character;auto& character_resources=play.character_resources;auto& character_player=play.character_player;auto& travel=play.travel;
    vfx_workspace.on_cue_sources_saved=[&]{play.play_cues.reset();};
    // The Gameplay Inspector, and what it asks the viewports to draw.
    darkangel::editor_app::GameplayInspector gameplay_inspector({editor,shell,play.character,play.character_resources,play.character_player,play.cue_state(),
#ifdef DAE_HUD
        &runtime_inventory,
#else
        nullptr,
#endif
        [&]{return play.choose_hud_target();}});
    auto& senses_shown=gameplay_inspector.selection.senses_shown;auto& senses_all=gameplay_inspector.selection.senses_all;auto& world_debug=gameplay_inspector.selection.world_debug;
#ifdef DAE_AUDIO
    audio_workspace.on_cue_sources_saved=[&]{play.play_cues.reset();};
    if(editor.assets)shell.draw_audio_panel=[&]{audio_workspace.draw_panel(bool(character));};
    if(args.audio_workspace){shell.select_workspace(darkangel::editor_app::Workspace::Audio);if(editor.assets)editor.refresh_assets();}
#endif
    // Exercises act on the scene's first spawned actor.
    auto first_actor=[&]{return play.first_actor();};
#ifdef DAE_NAVIGATION
    shell.draw_navigation_tool=[&]{navigation.draw_panel(character_resources?character_resources->collision.get():nullptr);};
#endif
    // Debug view of NPC senses over the Scene and Game viewports. While authoring: the radii and cones of the selected spawn
    // point's NPC, from its draft. In Play: the same for every actor ticked in the Gameplay Inspector (or whose spawn point is
    // selected) at its current place, with a line from its eye to each actor it perceives. Reads the host's own state; draws only.
    unsigned senses_drawn=0,volumes_drawn=0,navigation_drawn=0,splines_drawn=0,scatter_drawn=0;
    // The Scene and Game viewports' field of view this frame.
    float camera_fov=DirectX::XM_PIDIV4;
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
    auto build_splines=[&]{return darkangel::editor_app::build_splines(editor,shell);};
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
    // Play resources for --character-clips, Controller's Play callbacks, and the Gameplay Inspector for them.
    play.start(scene_skins.size(),scene_skins.size()==1?&scene_skins.begin()->second.model:nullptr);
    if(character_resources){shell.draw_gameplay_inspector=[&]{gameplay_inspector.draw();};shell.gameplay_metrics=[&]{return gameplay_inspector.metrics();};}
#endif

#ifdef DAE_AGENT_ENDPOINTS
std::unique_ptr<AgentEndpoint> agent;EditorDocument* agent_document{};auto poll_agent=[&]{if(args.agent_descriptor.empty())return;if(agent_document!=editor.document.get()){agent.reset();agent=std::make_unique<AgentEndpoint>(*editor.document,args.agent_project,args.agent_authoring,args.agent_descriptor);agent_document=editor.document.get();editor.log("Issued scoped agent descriptor for this authoring document.");}agent->poll();};
#endif
    reload_model=[&]{auto candidate=store.prepare(args.registry,args.cas,id);auto resources=upload(device,*candidate.model,pso,pso_cull);std::map<AssetId,UploadedModel> other;for(const auto& [asset,unused]:scene_models)other.emplace(asset,upload(device,load_cooked_model(args.registry,args.cas,asset),pso,pso_cull));
#ifdef DAE_ANIMATION
        std::map<AssetId,UploadedSkin> skins;for(const auto& [asset,unused]:scene_skins)skins.emplace(asset,upload_skin(device,load_cooked_skinned_model(args.registry,args.cas,asset),args.skin_reference?pso.RawPtr():skin_pso.RawPtr(),args.skin_reference?pso_cull.RawPtr():skin_cull.RawPtr(),diagnostic_clip.get(),args.pose_tick,args.skin_reference));
#endif
#ifdef DAE_CHARACTER_SCENE
        auto reload=play.prepare_reload(skins.size(),skins.empty()?nullptr:&skins.begin()->second.model);
#endif
        context->Flush();context->WaitForIdle();scene_models=std::move(other);
#ifdef DAE_ANIMATION
        scene_skins=std::move(skins);
#ifdef DAE_CHARACTER_SCENE
        graph_pose.reset();shell.graph_preview_asset={};composer_pose.reset();composer_matrices.clear();shell.composer_clip={};
#endif
#endif

#ifdef DAE_CHARACTER_SCENE
        play.commit_reload(std::move(reload));

#endif
        store.publish(candidate);gpu=std::move(resources);lease=store.acquire();store.collect();editor.log("Published resource generation after GPU upload; retired unpinned data at GPU idle.");};
#ifdef DAE_CHARACTER_SCENE
    play.install_preparation();
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
    auto play_exercise=darkangel::editor_app::exercises::make_play_exercises({args,editor,shell,play.character,play.character_resources,gameplay_inspector.selection,play.cue_state(),first_actor,senses_drawn,volumes_drawn,splines_drawn,scatter_drawn});
#endif
    bool running=true;unsigned frame{};while(running && (!args.frames || frame<args.frames)){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        RECT current_client{};if(GetClientRect(window,&current_client)&&current_client.right>0&&current_client.bottom>0){resize_width=static_cast<unsigned>(current_client.right);resize_height=static_cast<unsigned>(current_client.bottom);}
        if(resize_width && resize_height&&(resize_width!=swap->GetDesc().Width||resize_height!=swap->GetDesc().Height)){context->WaitForIdle();swap->Resize(resize_width,resize_height);resize_width=resize_height=0;}
        auto now=std::chrono::steady_clock::now();auto elapsed_seconds=std::chrono::duration<double>(now-last).count();auto seconds=elapsed_seconds;last=now;seconds=std::clamp(seconds,0.,.1);
#ifdef DAE_CHARACTER_SCENE
        play_exercise->frame(frame);
        media_exercise->frame(frame);
        authoring_source_exercise->frame(frame);
#endif
        // Background cooking: one uncooked source every half second while authoring in a visible editor. Gates run hidden and
        // leave their fixtures as they are, except the exercise that drops a new source into the project and waits for it.
        if(args.join.empty()&&!args.hidden&&!editor.preview.active()&&frame%30==29)editor.cook_pending(1);
        scene_editing_exercise->frame(frame);
        acceptance_exercise->frame(frame);
#ifdef DAE_CHARACTER_SCENE
        navigation_exercise->frame(frame);
        play.launch(frame);
        play_exercise->launched(frame);
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
        play.poll_input(frame);
        play.frame(frame,elapsed_seconds);
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
        camera_fov=XM_PIDIV4;
        if(const auto* play_camera=play.game_camera(elapsed_seconds)){
            view=XMMatrixLookAtRH(XMVectorSet(float(play_camera->position[0]),float(play_camera->position[1]),float(play_camera->position[2]),1),XMVectorSet(float(play_camera->target[0]),float(play_camera->target[1]),float(play_camera->target[2]),1),XMVectorSet(0,1,0,0));camera_fov=float(play_camera->fov_degrees)*XM_PI/180.f;}
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
        if(shell.second_view&&scene_color_second){const auto* second=character->client(character->players()[1]);const auto& eye=play.second_camera(elapsed_seconds);
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
        {darkangel::VfxView shown;XMFLOAT4X4 stored;XMStoreFloat4x4(&stored,view);std::memcpy(shown.view.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,projection);std::memcpy(shown.projection.data(),&stored,sizeof(stored));XMStoreFloat4x4(&stored,XMMatrixInverse(nullptr,view));
            shown.eye={stored._41,stored._42,stored._43};shown.forward={-stored._31,-stored._32,-stored._33};shown.near_plane=gpu.radius*.01f;shown.far_plane=gpu.radius*40;
            play.present_effects(shown);}
#endif
        // Scripted shutdown waits for a coherent final view. A baseline refresh can
        // start during the same frame as quit; two arbitrary render frames are not a
        // delivery fence and would publish an unready final receipt.
#ifdef DAE_CHARACTER_SCENE
        if(play.quit_ready())args.frames=frame+1;
#endif
        auto* back=swap->GetCurrentBackBufferRTV();context->SetRenderTargets(1,&back,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearRenderTarget(back,background,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);gui->Render(context);if(play.script_signals.capture)play.script_signals.captured=true;if(!args.capture.empty() && (play.script_signals.capture||(args.frames && frame+1==args.frames&&!play.script_signals.captured)))capture(device,context,back->GetTexture(),args.capture,args.capture_workspace?darkangel::editor_app::ViewArea{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height)}:area);swap->Present(args.hidden?0:1);++frame;
    }
#ifdef DAE_CHARACTER_SCENE
    window_input=nullptr;
    darkangel::editor_app::exercises::check_preview_results({args,editor,shell,import_baseline,character,composer_pose.get(),graph_pose.get(),authoring_skin.has_value(),composer_matrices.size()});
    // Presentation checks read what the frame loop drew; they run before gameplay checks stop Play.
    navigation_exercise->finish();
    play_exercise->check_drawing();
    media_exercise->finish();
    play_exercise->finish();
    darkangel::editor_app::exercises::check_gameplay_results({args,editor,shell,import_baseline,character,character_resources,play.director,travel,hud_resources.hud,
#ifdef DAE_HUD
        &hud_resources.hud_renderer,
#else
        nullptr,
#endif
        play.play_ledger,play.authoring_scene,play.companion_script,play.owner_script,first_actor,[&]{return play.choose_hud_target();},
        {projectiles_seen,play.navigation_polygons,play.behavior_running,senses_drawn,volumes_drawn,play.interactions_asked,play.interaction_offered,play_exercise->senses_early,
         play_exercise->senses_noticed,play_exercise->senses_noticed_z,play.travel_arrivals,play.travel_failures,play.travel_overlay_frames,play.travel_failure,bool(scene_color_second)&&hud_resources.hud_second_drawn}});
#endif
#ifdef DAE_EDITOR_GNS
    play.party_receipt(frame);
#endif
    #ifdef DAE_CHARACTER_SCENE
    authoring_source_exercise->finish();
#endif
    render.shutdown();std::cout<<"DarkAngel editor rendered "<<frame<<" frames with "<<args.backend<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"DarkAngelEditor: "<<e.what()<<'\n';return 1;}}
