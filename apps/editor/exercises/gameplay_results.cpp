#include "gameplay_results.hpp"
#include "script_runner.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../character_preview.hpp"
#ifdef DAE_HUD
#include "../hud_overlay.hpp"
#include <darkangel/hud.hpp>
#endif
#include <darkangel/camera.hpp>
#include <darkangel/check.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace darkangel::editor_app::exercises {
void check_gameplay_results(const GameplayResultContext& context) {
    const auto& args = context.options;
    auto& editor = context.editor;
    auto& shell = context.shell;
    const auto& import_baseline = context.scene_baseline;
    CharacterSceneSession* const& character = context.session;
    const auto& character_resources = context.resources;
    const auto& director = context.director;
    const auto& travel = context.travel;
    const auto& hud = context.hud;
#ifdef DAE_HUD
    const auto& hud_renderer = *context.hud_renderer;
#endif
    const auto& play_ledger = context.ledger;
    const auto& authoring_scene = context.authoring_scene;
    const auto& script = context.companion_script.steps();
    const auto& script_at = context.companion_script.cursor();
    const auto& script_interactions = context.companion_script.interactions();
    const auto& player_script = context.owner_script.steps();
    const auto& player_script_at = context.owner_script.cursor();
    const auto& first_actor = context.first_actor;
    const auto& choose_hud_target = context.hud_target;
    const auto& observed = context.observed;
    const auto& projectiles_seen = observed.projectiles_seen;
    const auto& navigation_polygons = observed.navigation_polygons;
    const auto& behavior_running = observed.behavior_running;
    const auto& senses_drawn = observed.senses_drawn;
    const auto& volumes_drawn = observed.volumes_drawn;
    const auto& interactions_asked = observed.interactions_asked;
    const auto& interaction_offered = observed.interaction_offered;
    const auto& senses_early = observed.senses_early;
    const auto& senses_noticed = observed.senses_noticed;
    const auto& senses_noticed_z = observed.senses_noticed_z;
    const auto& travel_arrivals = observed.travel_arrivals;
    const auto& travel_failures = observed.travel_failures;
    const auto& travel_overlay_frames = observed.travel_overlay_frames;
    const auto& travel_failure = observed.travel_failure;
    const bool second_view_drawn = observed.second_view_drawn;
    if (args.exercise_player_kit_play) {
        require(character && character->ability() && character->pending_prediction() == 0 && !editor.authoring.dirty(),
                "Player Kit fresh Play failed authority/source boundary");
        auto target = character->presentation().find(first_actor());
        require(character->presentation().valid(target), "Player Kit target absent");
        double health = character->presentation().read(target).health.current, stamina = -1;
        for (const auto& attribute : character->ability()->attributes)
            if (attribute.id == 3)
                stamina = attribute.value;
        require(health == 75 && stamina == 90 && editor.document->world().serialize() == import_baseline,
                "Player Kit fresh Play damage/cost/scene isolation failed");
        std::cout << "Player Kit creation -> paired assignment/Undo/Redo -> private closure -> coordinated Save -> complete fresh Player "
                     "scene/resources -> WorldSession light health75 stamina90 pending0\n";
        editor.stop();
    }
    if (args.exercise_heavy && !character && !editor.console.empty())
        throw std::runtime_error(editor.console.back().message);
    if (args.exercise_actor_workflow) {
        require(character && character->motor().tick == args.frames - 4 && character->pending_prediction() == 0,
                "Authored Player clock/correction failed");
        auto target = character->presentation().find(first_actor());
        double health = character->presentation().read(target).health.current, stamina = -1;
        for (const auto& value : character->ability()->attributes)
            if (value.id == 3)
                stamina = value.value;
        require(health == (args.exercise_creation_vertical ? 83 : 75) &&
                    stamina == (args.exercise_creation_vertical ? 88
                                : args.exercise_player_defaults ? 50
                                                                : 90) &&
                    editor.document->world().serialize() == import_baseline,
                "Authored Player native combat/isolation failed");
        if (args.exercise_player_defaults) {
            double player_health = -1;
            for (const auto& value : character->ability()->attributes)
                if (value.id == 2)
                    player_health = value.value;
            require(player_health == 80, "Authored Player starting Health did not replicate");
            std::cout << "Player defaults owner_health=" << player_health << " target_health=" << health << " stamina=" << stamina
                      << " scene=unchanged\n";
        }
        if (args.exercise_graph_authoring)
            std::cout << "Graph point edit -> Undo -> Redo -> frozen Save -> fresh native Player Play passed\n";
        if (args.exercise_player_input)
            std::cout << "Player input Keyboard.K routed through native semantic light action; history and frozen Player override passed\n";
        if (args.exercise_creation_vertical) {
            auto effects = character->effects(first_actor());
            require(effects && effects->current() && effects->current()->effects.size() == 1 &&
                        effects->current()->effects[0].definition == shell.binding_effect &&
                        effects->current()->effects[0].credit.source_network == 4 && effects->current()->effects[0].credit.activation == 1,
                    "Created ordinary effect failed authoritative replication/attribution");
            auto ability = std::find_if(character_resources->combat->abilities.begin(), character_resources->combat->abilities.end(),
                                        [&](const auto& definition) { return definition->id == shell.binding_ability; });
            require(ability != character_resources->combat->abilities.end() && (*ability)->action->id == shell.composer_asset &&
                        (*ability)->deferred_commit_block,
                    "Fresh Play retained stale Composer/commit references");
            std::cout << "Created native vertical: six sources, private validation, coordinated Save, fresh scene/Player/Kit/Composer "
                         "resources, deferred cost12 damage17 status source4/activation1, health83 stamina88 pending0\n";
        }
        std::cout << "Authored Player fresh Play health=" << health << " stamina=" << stamina
                  << " pending=" << character->pending_prediction() << '\n';
        editor.stop();
    }
    if (args.exercise_upper_body) {
        auto definition = std::find_if(character_resources->combat->abilities.begin(), character_resources->combat->abilities.end(),
                                       [](const auto& ability) { return ability->action->upper_body; });
        require(definition != character_resources->combat->abilities.end() && (*definition)->action->mask &&
                    !(*definition)->action->motion->motor_root && character_resources->graph->active_layer_bound() <= 3,
                "Masked editor did not prepare upper-body action");
        std::cout << "Masked action frozen_generation=" << (*definition)->action->mask->generation
                  << " locomotion_bound=" << character_resources->graph->active_layer_bound()
                  << " total_bound=" << character_resources->graph->active_layer_bound() + 1 << '\n';
    }
    if (args.exercise_death) {
        require(bool(character) && bool(character_resources->combat->death), "Death exercise needs a kit death timeline");
        auto target = character->presentation().find(first_actor());
        auto health = character->presentation().read(target).health.current;
        auto shown = character->death(first_actor());
        require(health <= 0 && shown && *shown > 0 && !character->death(character_resources->player) &&
                    character->pending_prediction() == 0 && editor.document->world().serialize() == import_baseline,
                "Editor Game did not present the target death timeline");
        std::cout << "Editor death tick=" << character->motor().tick << " target_health=" << health << " death_clip_tick=" << *shown
                  << " of " << character_resources->combat->death->duration / darkangel::action_tick_units
                  << " player_death=none pending=0\n";
        editor.stop();
    }
    if (args.exercise_mask_switch) {
        require(bool(character) && character->ability() && character->loadout() == 2 && character->predicted_loadout() == 2 &&
                    character->pending_prediction() == 0 && character->pending_abilities() == 0 &&
                    editor.document->world().serialize() == import_baseline,
                "Editor Game did not switch the active mask through authority and prediction");
        const auto& name = character_resources->mask_names[2];
        require(!name.empty() && name != character_resources->mask_names[0],
                "Mask switch exercise needs two named masks in slots Up and Down");
#ifdef DAE_HUD
        require(bool(hud) && hud->model().mask == name && hud->text("mask") == name, "Runtime HUD does not show the switched mask");
#endif
        std::cout << "Editor mask switch tick=" << character->motor().tick << " loadout=" << character->loadout() << " mask=" << name
                  << " worn=" << (character_resources->masks[2] ? 1 : 0) << " pending=0\n";
        editor.stop();
    }
    if (args.exercise_ranged) {
        require(bool(character) && character->ability() && bool(director), "Ranged exercise needs a combat session");
        const auto& kit = *character_resources->combat->kit;
        const auto ranged = kit.slots[unsigned(darkangel::CombatSlot::RangedLight)].ability;
        auto ability = std::find_if(character_resources->combat->abilities.begin(), character_resources->combat->abilities.end(),
                                    [&](const auto& definition) { return definition->id == ranged; });
        auto target = character->presentation().find(first_actor());
        const double health = character->presentation().read(target).health.current;
        const auto seen = character->observer(first_actor(), double(character->motor().tick)).frame.motor.position;
        const double distance = std::hypot(seen.x - character->motor().position.x, seen.z - character->motor().position.z);
        require(ranged != darkangel::AssetId{} && ability != character_resources->combat->abilities.end() && (*ability)->aim &&
                    director->rig().cameras[director->live()].facing == CameraFacing::Camera &&
                    (health < 100 || !character->projectiles().empty()) && character->pending_prediction() == 0 &&
                    editor.document->world().serialize() == import_baseline,
                "Editor Game did not hit the target with the aimed ranged ability through authority");
        std::cout << "Editor ranged tick=" << character->motor().tick << " camera=" << director->rig().cameras[director->live()].name
                  << " range=" << (*ability)->aim->range << " distance=" << distance << " target_health=" << health
                  << " aim_pitch=" << character->motor().pitch << " projectile=" << ((*ability)->aim->projectile ? 1 : 0)
                  << " in_flight=" << character->projectiles().size() << " seen_in_flight=" << projectiles_seen << " pending=0";
#ifdef DAE_HUD
        require(bool(hud) && hud->model().crosshair && hud->box("crosshair"), "Runtime HUD does not show the crosshair while aiming");
        std::cout << " hud_crosshair=1";
#endif
        std::cout << "\n";
        editor.stop();
    }
    if (args.exercise_soft_lock) {
        require(bool(character) && character->ability(), "Soft lock exercise needs a combat session");
        const auto host = character->motor(), predicted = character->predicted_motor();
        const auto seen = character->observer(first_actor(), double(host.tick)).frame.motor.position;
        const double toward = std::atan2(seen.x - host.position.x, seen.z - host.position.z),
                     error = std::abs(std::remainder(host.yaw - toward, 6.283185307179586)),
                     distance = std::hypot(seen.x - host.position.x, seen.z - host.position.z);
        auto ability = std::find_if(character_resources->combat->abilities.begin(), character_resources->combat->abilities.end(),
                                    [&](const auto& definition) { return definition->id == character->ability()->ability; });
        require(character->ability()->active && ability != character_resources->combat->abilities.end() && (*ability)->soft_lock &&
                    character->soft_lock() == first_actor() && error < .05 && distance < 2.5 &&
                    std::hypot(predicted.position.x - host.position.x, predicted.position.z - host.position.z) < 1e-6 &&
                    std::abs(std::remainder(predicted.yaw - host.yaw, 6.283185307179586)) < 1e-9 && character->pending_prediction() == 0 &&
                    editor.document->world().serialize() == import_baseline,
                "Editor Game did not soft-lock the melee attack onto the target through authority and prediction");
        std::cout << "Editor soft lock tick=" << host.tick
                  << " action_clock=" << character->ability()->action->clock / darkangel::action_tick_units << " yaw_error=" << error
                  << " distance=" << distance << " predicted_error=0 pending=0";
#ifdef DAE_HUD
        require(bool(hud) && hud->model().target_locked && hud->box("lock"), "Runtime HUD does not show the soft-lock indicator");
        std::cout << " hud_lock=1";
#endif
        std::cout << '\n';
        editor.stop();
    }
    if (args.exercise_transition) {
        require(bool(character) && bool(travel) && travel->scene(), "The transition exercise ended without a Play");
        const auto status = travel->status();
        unsigned used = 0;
        for (const auto& [object, json] : editor.document->plan().interactables)
            if (const auto count = character->interactable_used(object))
                used += *count;
        std::string phase = darkangel::transition_phase_text(status.phase);
        std::replace(phase.begin(), phase.end(), ' ', '_');
        auto failure = travel_failure;
        std::replace(failure.begin(), failure.end(), ' ', '_');
        std::cout << "Editor transition tick=" << character->motor().tick << " players=" << character->players().size()
                  << " arrivals=" << travel_arrivals << " failures=" << travel_failures << " phase=" << phase
                  << " scene=" << travel->scene()->scene.text() << " document=" << editor.document->root().text()
                  << " authoring=" << authoring_scene.text() << " lever_used=" << used
                  << " first_prompt=" << (character->interaction_prompt() ? 1 : 0) << " overlay_frames=" << travel_overlay_frames
                  << " loading=" << (hud && hud->model().loading ? 1 : 0) << " ledger_records=" << play_ledger.records().size()
                  << " scripts=" << player_script_at << "/" << player_script.size() << "," << script_at << "/" << script.size()
                  << " pending=" << character->pending_prediction() << (failure.empty() ? "" : " failure=" + failure.substr(0, 160))
                  << "\n";
        const auto authoring = authoring_scene;
        editor.stop();
        require(editor.document->root() == authoring, "Stopping Play after travel did not bring the authoring scene back");
    } else if (args.exercise_two_players) {
        require(bool(character) && character->players().size() == 2, "The two-player exercise needs a Play with two local players");
        const auto second_id = character->players()[1];
        const auto* first = character->client(character->players()[0]);
        const auto* second = character->client(second_id);
        const auto* outcome = second->last_interaction();
        unsigned used = 0;
        for (const auto& [object, json] : editor.document->plan().interactables)
            if (const auto count = character->interactable_used(object))
                used += *count;
        const auto& walked = *character->authority().motor(second_id);
        require(script_at == script.size() && script_interactions == 1 && outcome &&
                    outcome->result == darkangel::InteractionResult::Accepted && used == 1 && !first->last_interaction() &&
                    first->pose(second_id) && second->pose(character_resources->player) && first->pending_prediction() == 0 &&
                    second->pending_prediction() == 0 && shell.second_view && second_view_drawn,
                "Editor Game: the second local player did not walk to the lever and use it through authority, or is not shown from both "
                "clients");
        std::string heard;
        for (const auto& event : character->world_events().log())
            heard += (heard.empty() ? "" : ",") + event.name;
        std::cout << "Editor two_players tick=" << character->motor().tick << " players=" << character->players().size()
                  << " viewports=2 second_x=" << walked.position.x << " second_z=" << walked.position.z
                  << " second_result=" << darkangel::interaction_result_text(outcome->result) << " used=" << used
                  << " first_asked=0 events=" << heard << "\n";
    }
    if (args.exercise_interaction) {
        require(bool(character) && !character->encounters().empty(),
                "Interaction exercise needs a combat session with an interactable and an encounter");
        const auto status = *character->encounter(character->encounters().front());
        const auto* outcome = character->last_interaction();
        unsigned used = 0;
        for (const auto& [object, json] : editor.document->plan().interactables)
            if (const auto count = character->interactable_used(object))
                used += *count;
        require(interaction_offered && interactions_asked == 1 && outcome && outcome->result == darkangel::InteractionResult::Accepted &&
                    used == 1 && status.phase != darkangel::EncounterPhase::Waiting && !character->interaction_prompt(),
                "Editor Game: the interact input did not use the offered interactable through the server and start the encounter");
        std::string heard;
        for (const auto& event : character->world_events().log())
            heard += (heard.empty() ? "" : ",") + event.name;
        std::cout << "Editor interaction asked=" << interactions_asked << " result=" << darkangel::interaction_result_text(outcome->result)
                  << " used=" << used << " spawned=" << status.spawned << " events=" << heard << "\n";
    }
    if (args.exercise_encounter) {
        require(bool(character) && !character->encounters().empty(), "Encounter exercise needs a combat session with an encounter");
        const auto status = *character->encounter(character->encounters().front());
        unsigned fired = 0;
        for (const auto& [object, json] : editor.document->plan().triggers)
            if (const auto count = character->trigger_fired(object))
                fired += *count;
        std::string heard;
        for (const auto& event : character->world_events().log())
            heard += (heard.empty() ? "" : ",") + event.name;
        require(fired >= 1 && status.phase != darkangel::EncounterPhase::Waiting && status.spawned >= 1 &&
                    character->actors().size() >= status.spawned && volumes_drawn > 0,
                "Editor Game: walking into the trigger volume did not start the encounter");
        // Encounter enemies have the same readouts as spawn-point NPCs (decision, senses, perception) and one of them is the HUD target.
        unsigned readouts = 0;
        for (const auto id : character->actors())
            if (character->actor_encounter(id)) {
                const auto* config = character->actor(id);
                require(config && !config->name.empty() && character->perception(id),
                        "Editor Game: an encounter enemy has no configuration or perception to list");
                if (!character->controller_status(id).empty())
                    ++readouts;
            }
        const auto shown = choose_hud_target();
        require(readouts >= 1 && shown && character->actor_encounter(*shown) && character->actor(*shown),
                "Editor Game: no encounter enemy has a decision readout, or none became the HUD target");
#ifdef DAE_HUD
        require(bool(hud) && hud->model().target_name == character->actor(*shown)->name,
                "Runtime HUD does not name the encounter enemy as its target");
#endif
        std::cout << "Editor encounter fired=" << fired << " spawned=" << status.spawned << " alive=" << status.alive
                  << " wave=" << status.wave << " actors=" << character->actors().size() << " volumes_drawn=" << volumes_drawn
                  << " player_z=" << character->motor().position.z << " events=" << heard
                  << " hud_target=" << character->actor(*shown)->name << " readouts=" << readouts << "\n";
    }
    if (args.exercise_senses) {
        require(bool(character), "Senses exercise needs a combat session");
        const auto* perception = character->perception(first_actor());
        require(perception && !senses_early && senses_noticed && senses_drawn > 0,
                "Editor Game: the guard noticed the player before it walked into the cone, never noticed it, or its senses were not drawn");
        std::cout << "Editor senses noticed_tick=" << senses_noticed << " noticed_z=" << senses_noticed_z
                  << " sensors=" << perception->senses().sensors.size() << " perceived=" << perception->actors().size()
                  << " senses_drawn=" << senses_drawn << "\n";
    }
    if (args.exercise_attacked && !args.exercise_encounter && !args.exercise_interaction) {
        require(bool(character) && character->ability() && !behavior_running.empty(),
                "Attacked exercise needs a combat session with a spawned NPC that runs a behaviour");
        double health = -1;
        for (const auto& attribute : character->ability()->attributes)
            if (attribute.id == character->ability()->health_attribute)
                health = attribute.value;
        auto receipt = character->controller_receipt(first_actor());
        auto reaction = character->reaction(character_resources->player);
        auto death = character->death(character_resources->player);
        require(receipt && health >= 0 && health < 100 && character->pending_prediction() == 0 &&
                    editor.document->world().serialize() == import_baseline,
                "Editor Game: no spawned NPC damaged the player through authority");
        std::cout << "Editor attacked tick=" << character->motor().tick << " player_health=" << health
                  << " reaction_tick=" << (reaction && reaction->suppressed == darkangel::ReactionSuppression::None ? reaction->tick : -1.)
                  << " death_tick=" << (death ? *death : -1.) << " pending=0 actors=" << character->actors().size()
                  << " nav_polygons=" << navigation_polygons;
        if (!behavior_running.empty()) {
            auto shown = behavior_running;
            std::replace(shown.begin(), shown.end(), ' ', '_');
            std::cout << " behavior=" << shown;
        }
        {
            auto decision = character->controller_status(first_actor());
            if (!decision.empty()) {
                std::replace(decision.begin(), decision.end(), ' ', '_');
                std::cout << " decision=" << decision;
            }
        }
#ifdef DAE_HUD
        {
            require(bool(hud) && hud_renderer.drawn() > 0 && hud->model().health == health,
                    "Editor Game did not draw the runtime HUD from confirmed Health");
            auto shown = hud->text("health-text");
            std::erase(shown, ' ');
            std::cout << " hud_health=" << shown;
            auto target_shown = hud->text("target-text");
            std::erase(target_shown, ' ');
            auto named = hud->text("target-name");
            std::replace(named.begin(), named.end(), ' ', '_');
            std::cout << " hud_target=" << target_shown << " hud_target_name=" << named;
        }
#endif
        std::cout << '\n';
        editor.stop();
    }
    if (args.exercise_dodge) {
        require(bool(character) && character->ability(), "Dodge exercise needs a combat session");
        const auto& kit = *character_resources->combat->kit;
        auto dodge = kit.slots[unsigned(darkangel::CombatSlot::Dodge)].ability;
        double stamina = -1;
        for (const auto& attribute : character->ability()->attributes)
            if (attribute.id == 3)
                stamina = attribute.value;
        auto host = character->motor().position, predicted = character->predicted_motor().position;
        require(dodge != darkangel::AssetId{} && character->ability()->active && character->ability()->ability == dodge && stamina == 85 &&
                    character->pending_prediction() == 0 && std::hypot(predicted.x - host.x, predicted.z - host.z) < 1e-6 &&
                    editor.document->world().serialize() == import_baseline,
                "Editor Game did not run the authored dodge through authority and prediction");
        std::cout << "Editor dodge tick=" << character->motor().tick
                  << " action_clock=" << character->ability()->action->clock / darkangel::action_tick_units << " stamina=" << stamina
                  << " yaw=" << character->motor().yaw << " position=" << host.x << "," << host.z << " predicted_error=0 pending=0\n";
        editor.stop();
    }
    if (args.exercise_aim) {
        require(bool(character) && bool(director), "Aim exercise needs a Play session");
        auto target = character->presentation().find(first_actor());
        const auto& live = director->rig().cameras[director->live()];
        const auto& pose = director->pose();
        const auto position = character->motor().position;
        const double reach =
            std::hypot(std::hypot(pose.position[0] - position.x, pose.position[1] - position.y), pose.position[2] - position.z);
        require(live.name == "Aim" && live.facing == CameraFacing::Camera && director->blend() == 1 &&
                    std::abs(pose.fov_degrees - live.fov_degrees) < 1e-9 && reach < 4 &&
                    std::abs(std::remainder(character->motor().yaw - director->yaw(), 6.283185307179586)) < .35 &&
                    std::abs(character->motor().pitch) < 1.4 && character->presentation().read(target).health.current == 100 &&
                    character->pending_prediction() == 0 && editor.document->world().serialize() == import_baseline,
                "Editor Game did not blend to the aim camera while the ranged modifier was held");
        std::cout << "Editor camera live=" << live.name << " blend=" << director->blend() << " fov=" << pose.fov_degrees
                  << " camera_distance=" << reach << " character_faces_camera=1 aim_yaw_offset="
                  << std::remainder(character->motor().yaw - director->yaw(), 6.283185307179586)
                  << " aim_pitch=" << character->motor().pitch << " pending=0\n";
        editor.stop();
    }
    if (args.exercise_camera_look) {
        require(bool(character) && bool(director), "Look exercise needs Play");
        require(director->rig().cameras[director->live()].name == "FreeLook" && std::abs(director->yaw() + .21) < 1e-6 &&
                    std::abs(director->pitch() - (12 * 3.141592653589793 / 180 - .105)) < 1e-6 && character->pending_prediction() == 0,
                "Scripted right/up mouse delta did not orbit right/up in FreeLook");
        std::cout << "Camera scripted mouse right/up yaw=" << director->yaw() << " pitch=" << director->pitch() << " pending=0\n";
        editor.stop();
    }
    if (args.exercise_heavy && !args.exercise_soft_lock && !args.exercise_ranged && !args.exercise_death && !args.exercise_attacked &&
        !args.exercise_mask_switch && !args.exercise_dodge && !args.exercise_aim && !args.exercise_camera_look &&
        !args.exercise_two_players) {
        require(character && character->motor().tick == args.frames && character->pending_prediction() == 0 && character->ability() &&
                    !character->ability()->reservation,
                "Native heavy editor clocks/claims failed");
        auto target = character->presentation().find(first_actor());
        double health = character->presentation().read(target).health.current, stamina = -1;
        for (const auto& attribute : character->ability()->attributes)
            if (attribute.id == 3)
                stamina = attribute.value;
        auto status = character->effects(first_actor());
        require(health == ((args.exercise_authoring || args.exercise_upper_body) ? (args.frames == 30 ? 60 : 39)
                                                                                 : (args.frames == 30 ? 65 : 55)) &&
                    stamina == ((args.exercise_authoring || args.exercise_upper_body) ? 80 : 70) && status &&
                    status->cues().size() == (args.frames == 30 ? 1 : 0) && editor.document->world().serialize() == import_baseline,
                "Native heavy editor damage/cost/status/isolation failed");
        std::cout << "Native heavy editor tick=" << character->motor().tick << " target_health=" << health << " stamina=" << stamina
                  << " pending=" << character->pending_prediction() << '\n';
        if (args.exercise_reaction) {
            auto reaction = character->reaction(first_actor());
            require(reaction && reaction->suppressed == darkangel::ReactionSuppression::None &&
                        reaction->tick == double(character->motor().tick - reaction->start) &&
                        !character->reaction(character_resources->player) && status->current() && status->current()->effects.size() == 2,
                    "Editor Game did not present the authored target reaction beside Burn");
            std::cout << "Editor reaction effect_handle=" << reaction->key.handle << " onset=" << reaction->start
                      << " clip_tick=" << reaction->tick << " of " << reaction->action->duration / darkangel::action_tick_units
                      << " player_reaction=none effects=2\n";
        }
        editor.stop();
    }
    if (args.exercise_combat) {
        require(character && character->motor().tick == args.frames && character->pending_prediction() == 0 && character->ability(),
                "Native combat editor clocks/correction failed");
        auto target = character->presentation().find(first_actor());
        require(character->presentation().valid(target) &&
                    character->presentation().read(target).health.current == (args.exercise_reject ? 100
                                                                              : args.frames >= 61  ? 50
                                                                                                   : 75),
                args.exercise_reject ? "A refused press damaged the target" : "Native combat editor target damage failed");
        double stamina = -1;
        for (const auto& attribute : character->ability()->attributes)
            if (attribute.id == 3)
                stamina = attribute.value;
        require(stamina == (args.exercise_reject ? 100
                            : args.frames >= 61  ? 80
                                                 : 90) &&
                    editor.document->world().serialize() == import_baseline,
                args.exercise_reject ? "A refused press left its cost spent" : "Native combat cost/isolation failed");
        std::cout << "Native combat editor tick=" << character->motor().tick
                  << " target_health=" << character->presentation().read(target).health.current << " stamina=" << stamina
                  << " pending=" << character->pending_prediction() << '\n';
        editor.stop();
    }
    if (args.exercise_character) {
        require(character && character->motor().tick == args.frames && character->motor().position.z > 4,
                "Native character editor exercise did not move at fixed ticks");
        if (args.exercise_workspaces)
            std::cout << "Workspace transitions preserved authoring and resumed after neutral input\n";
        std::cout << "Native character editor tick=" << character->motor().tick << " foot=" << character->motor().position.x << ','
                  << character->motor().position.y << ',' << character->motor().position.z << " graph_phase=" << character->graph().phase
                  << " pending=" << character->pending_prediction() << '\n';
        editor.stop();
    }
}
} // namespace darkangel::editor_app::exercises
