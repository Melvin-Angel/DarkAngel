#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include "editor_app.hpp"
#include "exercises/scene_camera_exercise.hpp"
#include "exercises/prefab_exercise.hpp"
#include "exercises/material_exercise.hpp"
#include "exercises/ui_exercise.hpp"
#include "exercises/ui_play_exercise.hpp"
#include "exercises/editor_ux_exercise.hpp"
#include "exercises/live_previews_exercise.hpp"
#include "editor_controller.hpp"
#include "editor_theme.hpp"
#include "editor_ui.hpp"
#include "render/render_system.hpp"
#include "render/scene_resources.hpp"
#include "render/scene_viewport.hpp"
#include "exercises/acceptance.hpp"
#include "exercises/scene_editing.hpp"
#ifdef DAE_CHARACTER_SCENE
#include "cue_debug.hpp"
#include "gameplay_inspector.hpp"
#include "play_controller.hpp"
#include "play_saves.hpp"
#include "pose_previews.hpp"
#include "vfx_workspace.hpp"
#include "viewport_tools.hpp"
#include "exercises/authoring_sources.hpp"
#include "exercises/gameplay_results.hpp"
#include "exercises/media_exercises.hpp"
#include "exercises/navigation_exercises.hpp"
#include "exercises/play_exercises.hpp"
#include "exercises/preview_results.hpp"
#include "exercises/scale_exercise.hpp"
#endif
#ifdef DAE_AUDIO
#include "audio_workspace.hpp"
#endif
#ifdef DAE_NAVIGATION
#include "navigation_tool.hpp"
#endif
#ifdef DAE_HUD
#include "play_hud.hpp"
#include "runtime_altar.hpp"
#include "runtime_inventory.hpp"
#endif
#ifdef DAE_AGENT_ENDPOINTS
#include <darkangel/editor_rpc.hpp>
#endif
#include <darkangel/check.hpp>
#include <darkangel/vfx.hpp>
#include <DirectXTex.h>
#include <ImGuiImplWin32.hpp>
#include <ImGuizmo.h>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace darkangel::editor_app {
// COM for the WIC image codecs, and per-monitor DPI awareness, for the process; made before the window.
struct EditorApp::ProcessSetup {
    ProcessSetup() {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Editor COM initialization failed");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    ~ProcessSetup() {
        DirectX::SetWICFactory(nullptr);
        CoUninitialize();
    }
};

EditorApp::EditorApp(EditorOptions options)
    : args(std::move(options)), id(AssetId::parse(args.model)), lease(store.prepare(args.registry, args.cas, id)) {
    store.publish(lease);
    process_ = std::make_unique<ProcessSetup>();
    // The render system is made first and shut down after the frame loop; every device resource lives between.
    render_ = std::make_unique<RenderSystem>(args.width, args.height, args.backend);
    auto& render = *render_;
    darkangel::editor_app::initialize_theme(GetDpiForWindow(render.window) / 96.f);
    if (!args.hidden)
        ShowWindow(render.window, SW_SHOW);
    render.create_frame_pipelines();
    scene_ = std::make_unique<render::SceneResources>(render, args, id, lease);
    editor_ = std::make_unique<Controller>(id, scene_->gpu.radius);
    shell_ = std::make_unique<Shell>();
    auto& editor = *editor_;
    auto& shell = *shell_;
    select_startup_workspace();
    shell.initialize_preferences(args.sources,!args.hidden);
    import_count = editor.asset_inventory.size();
    editor.prepare_model = [this](AssetId requested) {
        try {
            scene_->prepare_model(requested);
        } catch (const std::exception&) {
            // Not in the Play package: cook and package that model alone and upload it; nothing else reloads.
            if (!editor_->assets)
                throw;
            scene_->prepare_model(requested, editor_->package_model(requested), editor_->assets->cas_path());
        }
    };
    editor.refresh_model=[this](AssetId requested){require(!editor_->preview.active(),"Stop Play before refreshing model materials");const auto registry=editor_->package_model(requested);const auto cooked=load_cooked_model(registry,editor_->assets->cas_path(),requested);
        for(const auto& [object,slots]:editor_->document->plan().materials)if(editor_->document->plan().models.at(object)==requested)for(const auto& [slot,material]:slots)require(std::any_of(cooked.materials.begin(),cooked.materials.end(),[&](const auto& value){return value.id==slot;}),"A scene material slot was removed; repair the preserved assignment before refreshing");
        scene_->prepare_model(requested,registry,editor_->assets->cas_path(),true);};
    editor.prepare_material=[this](AssetId model,AssetId slot,AssetId material){
        // Active scene travel consumes the freshly prepared frozen package, never an ad-hoc recook of artist sources.
        const auto registry=editor_->assets&&!editor_->preview.active()?editor_->package_model(material):args.registry;
        const auto cas=editor_->assets&&!editor_->preview.active()?editor_->assets->cas_path():args.cas;
        // SceneResources owns the model/skin slot table and validates the slot against the uploaded resource.
        // Calling load_cooked_model here would misclassify a human-binding asset as a static model.
        scene_->prepare_material(model,slot,load_cooked_material_instance(registry,cas,material));
    };
    if (!args.scene.empty())
        editor.open(args.scene);
    shell.navigation_scene=editor.document->root();
    if (args.scene.empty())
        shell.scene_navigation.camera.look_from({0,scene_->gpu.radius*.2,scene_->gpu.radius*4.14},{0,0,0});
    if (args.camera_close)
        shell.scene_navigation.camera.look_from({4,2.5,8},{0,1.3,3});
    import_baseline = editor.document->world().serialize();
#ifdef DAE_CHARACTER_SCENE
    // What Play's cue player tells each side, for the Cues tab. Made before the media workspaces, whose sinks forward to it.
    cue_recorders_ = std::make_unique<CueRecorders>();
    // Visual effects: one service on this device. Play's cue sinks play the effect asset behind each cue visual.
    vfx_ = std::make_unique<VfxWorkspace>(editor);
    vfx_->start(render.device, render.context, int(render.swap_desc.ColorBufferFormat), int(render.swap_desc.DepthBufferFormat));
#ifdef DAE_AUDIO
    // Sounds, music and the Audio tab: one workspace for the process, its service made when the first Play or audition needs it.
    audio_ = std::make_unique<AudioWorkspace>(editor, args, cue_recorders_->audio);
#endif
    // The VFX tab: the selected effect is cooked and played in a loop in the preview, and cooked again whenever its source file changes on
    // disk.
    if (editor.assets)
        shell.draw_vfx_panel = [this] { vfx_->draw_panel(); };
    // Open / Edit for sounds, audio profiles and effects lands on the Audio or VFX tab with that asset selected.
    shell.open_host_asset = [this](AssetId id, std::string_view extension) {
#ifdef DAE_AUDIO
        if (audio_ && (extension == ".wav" || extension == ".dasound" || extension == ".daaudioprofile")) {
            (extension == ".daaudioprofile" ? audio_->state.profile : audio_->state.sound) = id;
            shell_->select_workspace(darkangel::editor_app::Workspace::Audio);
            return true;
        }
#endif
        if (extension == ".efkproj" || extension == ".efkefc")
            for (const auto& asset : vfx_->effects())
                if (asset.id == id) {
                    vfx_->select(asset);
                    shell_->select_workspace(darkangel::editor_app::Workspace::Vfx);
                    return true;
                }
        return false;
    };
    if (args.vfx_workspace) {
        shell.select_workspace(darkangel::editor_app::Workspace::Vfx);
        if (editor.assets)
            editor.refresh_assets();
        for (const auto& asset : vfx_->effects())
            if (asset.path == args.vfx_select)
                vfx_->select(asset);
    }
#ifdef DAE_NAVIGATION
    // Tools > Navigation: an explicit bake of the scene's cooked collision that Play reuses while the collision is unchanged.
    navigation_ = std::make_unique<NavigationTool>();
#endif
    // The project's save slot lives beside its asset cache. Saving is the host's: a capture of the ledger and inventories at a safe point.
    if (editor.assets)
        play_saves_ = std::make_unique<PlaySaves>(editor.assets->cas_path().parent_path() / "saves" / "project.dasave");
#ifdef DAE_HUD
    // Runtime HUD resources, and the game menus drawn into it.
    hud_ = std::make_unique<HudResources>(render.device);
    shell_->ui_renderer = &hud_->hud_renderer;
    inventory_ = std::make_unique<RuntimeInventory>();
    altar_ = std::make_unique<RuntimeAltar>();
#endif
    // Play: the running session, its party, cue presentation, cameras and scripted input.
    play_ = std::make_unique<PlayController>(PlayContext{args, editor, shell, *vfx_,
#ifdef DAE_AUDIO
                                                         audio_.get(),
#else
                                                         nullptr,
#endif
#ifdef DAE_NAVIGATION
                                                         navigation_.get(),
#else
                                                         nullptr,
#endif
#ifdef DAE_HUD
                                                         hud_.get(),
#else
                                                         nullptr,
#endif
                                                         *cue_recorders_, play_saves_, *inventory_, *altar_, render.input,
                                                         [this] { PostMessageW(render_->window, WM_CLOSE, 0, 0); },
                                                         [this] { reload_model(); }, scene_->scene_models});
    auto& play = *play_;
    vfx_->on_cue_sources_saved = [this] { play_->play_cues.reset(); };
    // The Gameplay Inspector, and what it asks the viewports to draw.
    inspector_ = std::make_unique<GameplayInspector>(GameplayInspectorContext{editor, shell, play.character, play.character_resources,
                                                                              play.character_player, play.cue_state(),
#ifdef DAE_HUD
                                                                              inventory_.get(),
#else
                                                                              nullptr,
#endif
                                                                              [this] { return play_->choose_hud_target(); }});
    // The Composer, Character and graph pose previews of Play's prepared character.
    previews_ = std::make_unique<PosePreviews>(PosePreviewContext{editor, shell, render, *scene_, play.character_resources});
#ifdef DAE_AUDIO
    audio_->on_cue_sources_saved = [this] { play_->play_cues.reset(); };
    if (editor.assets)
        shell.draw_audio_panel = [this] { audio_->draw_panel(bool(play_->character)); };
    if (args.audio_workspace) {
        shell.select_workspace(darkangel::editor_app::Workspace::Audio);
        if (editor.assets)
            editor.refresh_assets();
    }
#endif
#ifdef DAE_NAVIGATION
    shell.draw_navigation_tool = [this] {
        navigation_->draw_panel(play_->static_collision().get());
    };
#endif
    // Picking, the placement surface and the viewports' debug overlay.
    tools_ = std::make_unique<ViewportTools>(ViewportToolsContext{editor, shell, *scene_, play.character, play.character_resources,
                                                                  [this] { return play_->static_collision(); },
#ifdef DAE_NAVIGATION
                                                                  navigation_.get(),
#else
                                                                  nullptr,
#endif
                                                                  inspector_->selection});
    tools_->install();
    // Play resources for --character-clips, Controller's Play callbacks, and the Gameplay Inspector for them.
    play.start(scene_->scene_skins.size(), scene_->scene_skins.size() == 1 ? &scene_->scene_skins.begin()->second.model : nullptr);
    if (play.character_resources) {
        shell.draw_gameplay_inspector = [this] { inspector_->draw(); };
        shell.gameplay_metrics = [this] { return inspector_->metrics(); };
    }
    play.install_preparation();
    previews_->install();
#endif
    select_startup_assets();
    viewport_ = std::make_unique<render::SceneViewport>(render::SceneViewportContext{render, *scene_, args, editor, shell,
#ifdef DAE_CHARACTER_SCENE
                                                                                     *vfx_, play_.get(), previews_.get()
#endif
    });
    last_ = std::chrono::steady_clock::now();
}

EditorApp::~EditorApp() {
    // The reverse of creation.
    viewport_.reset();
#ifdef DAE_AGENT_ENDPOINTS
    agent_.reset();
#endif
#ifdef DAE_CHARACTER_SCENE
    tools_.reset();
    previews_.reset();
    inspector_.reset();
    play_.reset();
#ifdef DAE_HUD
    altar_.reset();
    inventory_.reset();
    if (shell_)
        shell_->ui_state.reset(); // the UI preview renders through the HUD renderer
    hud_.reset();
#endif
    play_saves_.reset();
#ifdef DAE_NAVIGATION
    navigation_.reset();
#endif
#ifdef DAE_AUDIO
    audio_.reset();
#endif
    vfx_.reset();
    cue_recorders_.reset();
#endif
    shell_.reset();
    editor_.reset();
    scene_.reset();
    render_.reset();
    process_.reset();
}

void EditorApp::select_startup_workspace() {
    auto& editor = *editor_;
    auto& shell = *shell_;
    if (!args.sources.empty())
        editor.configure_assets(args.sources, args.asset_cache);
    shell.select_workspace(args.character_workspace   ? darkangel::editor_app::Workspace::Character
                           : args.npc_workspace       ? darkangel::editor_app::Workspace::Npc
                           : args.player_workspace    ? darkangel::editor_app::Workspace::Player
                           : args.animation_workspace ? darkangel::editor_app::Workspace::Animation
                           : args.ability_workspace   ? darkangel::editor_app::Workspace::Ability
                           : args.assets_workspace    ? darkangel::editor_app::Workspace::Assets
                           : (args.game_workspace || darkangel::editor_app::exercises::starts_play(args))
                               ? darkangel::editor_app::Workspace::Game
                               : darkangel::editor_app::Workspace::Scene);
    darkangel::editor_app::exercises::select_startup_views(args, shell);
    if (args.tools_workspace)
        shell.open_tool(darkangel::editor_app::ToolView::InputBindings);
    if (args.navigation_tool)
        shell.open_tool(darkangel::editor_app::ToolView::Navigation);
    if (args.settings_workspace)
        shell.select_workspace(darkangel::editor_app::Workspace::Settings);
    if (!args.import_preview.empty())
        shell.open_import(args.import_preview);
}
void EditorApp::select_startup_assets() {
    auto& editor = *editor_;
    auto& shell = *shell_;
    if (args.ability_workspace && editor.assets && editor.authoring_kit != AssetId{}) {
        auto& kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
        shell.authored_selection = AssetId::parse(kit["slots"][1]["ability"].get<std::string>());
    }
    if (!args.authoring_asset.empty())
        shell.authored_selection = AssetId::parse(args.authoring_asset);
    if (args.items_workspace) {
        shell.select_workspace(darkangel::editor_app::Workspace::Items);
        if (!args.authoring_asset.empty())
            shell.item_selection = AssetId::parse(args.authoring_asset);
    }
    if (args.input_workspace) {
        strcpy_s(shell.input_binding_filter, "attack.light");
        shell.input_authoring_asset = args.authoring_asset.empty() ? AssetId{} : AssetId::parse(args.authoring_asset);
        shell.open_tool(darkangel::editor_app::ToolView::InputBindings);
    }
    if (!args.authoring_asset.empty()) {
        if (args.character_workspace)
            shell.character_selection = AssetId::parse(args.authoring_asset);
        if (args.player_workspace)
            shell.player_selection = AssetId::parse(args.authoring_asset);
        if (args.npc_workspace)
            shell.npc_selection = AssetId::parse(args.authoring_asset);
    }
    if (!args.mask_editor.empty())
        shell.mask_editor = AssetId::parse(args.mask_editor);
    if (!args.player_asset.empty()) {
        require(editor.assets != nullptr, "Player selection needs a project mount");
        editor.authoring_player = AssetId::parse(args.player_asset);
        auto& player = editor.authoring.open(*editor.assets, editor.authoring_player).value;
        editor.authoring_kit = AssetId::parse(player.at("loadout").at("kit").get<std::string>());
    }
}

void EditorApp::reload_model() {
    auto reload = scene_->prepare_reload(store);
#ifdef DAE_CHARACTER_SCENE
    auto play_reload = play_->prepare_reload(reload.skins.size(), reload.skins.empty() ? nullptr : &reload.skins.begin()->second.model);
#endif
    scene_->commit_models(reload);
#ifdef DAE_CHARACTER_SCENE
    previews_->reset();
    play_->commit_reload(std::move(play_reload));
#endif
    scene_->commit_model(reload, store, lease);
    editor_->log("Published resource generation after GPU upload; retired unpinned data at GPU idle.");
}
void EditorApp::reload_shader(const std::string& text) {
    scene_->reload_shader(text, lease);
    editor_->log("Static shader candidate and material bindings committed at GPU idle.");
}
#ifdef DAE_AGENT_ENDPOINTS
void EditorApp::poll_agent() {
    auto& editor = *editor_;
    if (args.agent_descriptor.empty())
        return;
    if (agent_document_ != editor.document.get()) {
        agent_.reset();
        agent_ = std::make_unique<AgentEndpoint>(*editor.document, args.agent_project, args.agent_authoring, args.agent_descriptor);
        agent_document_ = editor.document.get();
        editor.log("Issued scoped agent descriptor for this authoring document.");
    }
    agent_->poll();
}
#endif

int EditorApp::run() {
    auto& render = *render_;
    auto& editor = *editor_;
    auto& shell = *shell_;
    auto& viewport = *viewport_;
    auto& device = render.device;
    auto& context = render.context;
    auto& swap = render.swap;
    auto editor_ux_exercise=exercises::make_editor_ux(args.exercise_editor_ux,editor,shell);
    auto live_previews_exercise=exercises::make_live_previews(args.exercise_live_previews,editor,shell);
    auto scene_camera_exercise = exercises::make_scene_camera(args.exercise_scene_camera,editor,shell);
    auto prefab_exercise = exercises::make_prefabs(args.exercise_prefabs,editor,shell);
    auto material_exercise = exercises::make_materials(args.exercise_materials,editor,shell);
    auto ui_exercise = exercises::make_ui(args.exercise_ui,editor,shell);
    auto scene_editing_exercise =
        darkangel::editor_app::exercises::make_scene_editing({args, editor, shell, import_baseline, import_count});
    auto acceptance_exercise = darkangel::editor_app::exercises::make_acceptance(
        {args, editor, store, id, [this] { reload_model(); }, [this](const std::string& text) { reload_shader(text); }});
#ifdef DAE_CHARACTER_SCENE
    auto& play = *play_;
    auto ui_play_exercise=exercises::make_ui_play(args.exercise_ui_play,editor,shell,play);
    auto& tools = *tools_;
    auto& previews = *previews_;
    auto authoring_source_exercise = darkangel::editor_app::exercises::make_authoring_sources(
        {args, editor, shell, play.character_resources, play.character, import_baseline});
    auto media_exercise = darkangel::editor_app::exercises::make_media_exercises({args, editor, shell, *vfx_,
#ifdef DAE_AUDIO
                                                                                  audio_.get(),
#else
                                                                                  nullptr,
#endif
                                                                                  play.character});
    auto navigation_exercise = darkangel::editor_app::exercises::make_navigation_exercises(
        {args, shell,
#ifdef DAE_NAVIGATION
         navigation_.get(),
#else
         nullptr,
#endif
         [this]() -> const darkangel::CollisionDefinition* { return play_->static_collision().get(); },
         tools.navigation_drawn});
    auto play_exercise = darkangel::editor_app::exercises::make_play_exercises(
        {args, editor, shell, play.character, play.character_resources, inspector_->selection, play.cue_state(),
         [this] { return play_->first_actor(); }, tools.senses_drawn, tools.volumes_drawn, tools.splines_drawn, viewport.scatter_drawn});
    auto scale_exercise = darkangel::editor_app::exercises::make_scale({args, editor, shell});
#endif
    unsigned frame{};
    while (!args.frames || frame < args.frames) {
        if (!render.pump_messages())
            break;
        // Where the frame's time goes, for scale measurement: exercises and update, UI, Play, scene render, effects, present.
        auto section_start = std::chrono::steady_clock::now();
        const auto mark = [&](unsigned section) {
            const auto now = std::chrono::steady_clock::now();
            shell.frame_sections[section] = std::chrono::duration<double, std::milli>(now - section_start).count();
            section_start = now;
        };
        render.follow_window_size();
        auto now = std::chrono::steady_clock::now();
        auto elapsed_seconds = std::chrono::duration<double>(now - last_).count();
        auto seconds = elapsed_seconds;
        last_ = now;
        seconds = std::clamp(seconds, 0., .1);
#ifdef DAE_CHARACTER_SCENE
        ui_play_exercise->frame(frame);
        play_exercise->frame(frame);
        media_exercise->frame(frame);
        authoring_source_exercise->frame(frame);
#endif
        // Background cooking: one uncooked source every half second while authoring in a visible editor. Gates run hidden and
        // leave their fixtures as they are, except the exercise that drops a new source into the project and waits for it.
        if (args.join.empty() && !args.hidden && !editor.preview.active() && frame % 30 == 29)
            editor.cook_pending(1);
        editor_ux_exercise->frame(frame);
        live_previews_exercise->frame(frame);
        scene_camera_exercise->frame(frame);
        prefab_exercise->frame(frame);
        material_exercise->frame(frame);
        ui_exercise->frame(frame);
        scene_editing_exercise->frame(frame);
        acceptance_exercise->frame(frame);
#ifdef DAE_CHARACTER_SCENE
        navigation_exercise->frame(frame);
        scale_exercise->frame(frame);
        play.launch(frame);
        play_exercise->launched(frame);
        if (args.character_clips.empty())
            editor.update(seconds);
#else
        editor.update(seconds);
#endif
        mark(0);
        const auto& desc = swap->GetDesc();
        viewport.prepare_targets();
        shell.scale_workspace_layouts(GetDpiForWindow(render.window) / 96.f * shell.ui_scale);
        darkangel::editor_app::set_theme_dpi(GetDpiForWindow(render.window) / 96.f * shell.ui_scale);
        render.gui->NewFrame(desc.Width, desc.Height, desc.PreTransform);
        ImGuizmo::BeginFrame();
        auto image = viewport.scene_texture();
        auto area = shell.draw(editor, desc.Width, desc.Height, args.backend.c_str(), lease.number,
                               (args.composer_playback||args.exercise_scene_camera) ? 1. / 60 : elapsed_seconds, image);
        mark(1);
#ifdef DAE_CHARACTER_SCENE
        play.poll_input(frame);
        play.frame(frame, elapsed_seconds);
#endif
#ifdef DAE_AGENT_ENDPOINTS
        poll_agent();
#endif
        mark(2);
        viewport.resize(area);
        if (shell.reload_model) {
            try {
                // A stopped authoring session reloads published sources, including edited material assignments.
                if(editor.prepare_play_resources&&!editor.preview.active()&&!editor.authoring.dirty())editor.prepare_play_resources();else reload_model();
            } catch (const std::exception& error) {
                context->Flush();
                context->WaitForIdle();
                editor.log(error.what(), darkangel::editor_app::ConsoleSeverity::Error);
            }
            shell.reload_model = false;
        }
        if (shell.reload_shader) {
            try {
                reload_shader(render::shader_text());
            } catch (const std::exception& error) {
                context->Flush();
                context->WaitForIdle();
                editor.log(error.what(), darkangel::editor_app::ConsoleSeverity::Error);
            }
            shell.reload_shader = false;
        }
        viewport.render(elapsed_seconds);
        mark(3);
#ifdef DAE_CHARACTER_SCENE
#ifdef DAE_AUDIO
        // Audio every frame: the mixer, encounter profiles layered while their encounter runs, and the engine's clock outside Play.
        audio_->frame(frame, seconds, shell.workspace == darkangel::editor_app::Workspace::Audio, play.character);
#endif
        // The VFX tab's preview, then the visual effects of the Play session, drawn after the opaque scene against its depth.
        vfx_->preview_frame(frame, args.hidden ? 1. / 60 : seconds,
                            !play.character && shell.workspace == darkangel::editor_app::Workspace::Vfx, viewport.vfx_view());
        play.present_effects(viewport.vfx_view());
        // Scripted shutdown waits for a coherent final view. A baseline refresh can
        // start during the same frame as quit; two arbitrary render frames are not a
        // delivery fence and would publish an unready final receipt.
        if (play.quit_ready())
            args.frames = frame + 1;
        auto& script_signals = play.script_signals;
#endif
        mark(4);
        auto* back = render.draw_ui(render::SceneViewport::background);
        if (script_signals.capture)
            script_signals.captured = true;
        if (!args.capture.empty() && (script_signals.capture || (args.frames && frame + 1 == args.frames && !script_signals.captured)))
            render::capture(device, context, back->GetTexture(), args.capture,
                            args.capture_workspace
                                ? darkangel::editor_app::ViewArea{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height)}
                                : area);
        render.present(args.hidden);
        mark(5);
        ++frame;
    }
#ifdef DAE_CHARACTER_SCENE
    render.input = nullptr;
    darkangel::editor_app::exercises::check_preview_results({args, editor, shell, import_baseline, play.character,
                                                             previews.composer_pose.get(), previews.graph_pose.get(),
                                                             bool(previews.authoring_skin), previews.composer_matrices.size()});
    // Presentation checks read what the frame loop drew; they run before gameplay checks stop Play.
    navigation_exercise->finish();
    scale_exercise->finish();
    play_exercise->check_drawing();
    media_exercise->finish();
    play_exercise->finish();
    darkangel::editor_app::exercises::check_gameplay_results(
        {args,
         editor,
         shell,
         import_baseline,
         play.character,
         play.character_resources,
         play.director,
         play.travel,
         hud_->hud,
#ifdef DAE_HUD
         &hud_->hud_renderer,
#else
         nullptr,
#endif
         play.play_ledger,
         play.authoring_scene,
         play.companion_script,
         play.owner_script,
         [this] { return play_->first_actor(); },
         [this] { return play_->choose_hud_target(); },
         {viewport.projectiles_seen, play.navigation_polygons, play.behavior_running, tools.senses_drawn, tools.volumes_drawn,
          play.interactions_asked, play.interaction_offered, play_exercise->senses_early, play_exercise->senses_noticed,
          play_exercise->senses_noticed_z, play.travel_arrivals, play.travel_failures, play.travel_overlay_frames, play.travel_failure,
          viewport.second_target() && hud_->hud_second_drawn}});
#endif
#ifdef DAE_EDITOR_GNS
    play.party_receipt(frame);
#endif
#ifdef DAE_CHARACTER_SCENE
    authoring_source_exercise->finish();
#endif
    editor_ux_exercise->finish();
    live_previews_exercise->finish();
    scene_camera_exercise->finish();
    prefab_exercise->finish();
    material_exercise->finish();
    ui_exercise->finish();
#ifdef DAE_CHARACTER_SCENE
    ui_play_exercise->finish();
#endif
    shell.save_preferences();
    render.shutdown();
    std::cout << "DarkAngel editor rendered " << frame << " frames with " << args.backend << '\n';
    return 0;
}
} // namespace darkangel::editor_app
