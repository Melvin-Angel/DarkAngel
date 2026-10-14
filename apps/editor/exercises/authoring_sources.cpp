#include "authoring_sources.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../character_preview.hpp"
#include <darkangel/check.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace darkangel::editor_app::exercises {
namespace {
class AuthoringSources final : public Exercise {
    AuthoringSourceContext context_;
    bool done_{};

  public:
    explicit AuthoringSources(AuthoringSourceContext context) : context_(context) {}
    void frame(unsigned frame) override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& shell = context_.shell;
        const auto& character_resources = context_.resources;
        const auto& character = context_.session;
        const auto& import_baseline = context_.scene_baseline;
        if (args.exercise_composer && frame == 0) {
            auto& value=editor.authoring.open(*editor.assets,shell.authored_selection).value;
            auto clip=AssetId::parse(value.at("motion").at("clip").get<std::string>());
            auto duration=value.at("duration").get<unsigned>();
            shell.prepare_action_pose(shell.authored_selection);shell.composer_clip=clip;shell.composer_duration=duration;
            shell.composer_asset=shell.authored_selection;shell.composer_tick=args.composer_tick;shell.composer_playing=args.composer_playback;shell.composer_block=2;
        }
        if (args.exercise_source_compare && frame == 0) {
            auto& draft = editor.authoring.open(*editor.assets, shell.authored_selection);
            auto path = editor.assets->source_root() / draft.asset.path;
            auto baseline = draft.saved;
            auto revision = editor.authoring.revision();
            auto matching = editor.authoring.compare_source(*editor.assets, shell.authored_selection);
            require(matching.matches && matching.exists, "GUI source comparison lost original baseline");
            std::ofstream(path, std::ios::binary) << baseline << "\n";
            try {
                shell.last_source_comparison = editor.authoring.compare_source(*editor.assets, shell.authored_selection);
            } catch (...) {
                std::ofstream(path, std::ios::binary) << baseline;
                throw;
            }
            std::ofstream(path, std::ios::binary) << baseline;
            require(shell.last_source_comparison->exists && !shell.last_source_comparison->matches &&
                        editor.authoring.revision() == revision && editor.authoring.history().empty() &&
                        editor.authoring.compare_source(*editor.assets, shell.authored_selection).matches,
                    "GUI source comparison changed history or failed restored baseline");
            editor.log(
                "Read-only disk comparison captured external-byte conflict; exact fixture source restored, no publication/history change.");
            if (args.exercise_history_source_compare)
                shell.open_tool(darkangel::editor_app::ToolView::AuthoringHistory);
        }
        if (args.exercise_effect_source_preview && frame == 0) {
            require(editor.assets && editor.authoring_kit != AssetId{}, "Effect source preview needs native kit");
            auto kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            auto original = AssetId::parse(kit.at("effects")[0].get<std::string>());
            auto status = editor.authoring.duplicate(*editor.assets, original, "source_owned_preview", true);
            auto value = editor.authoring.open(*editor.assets, status).value;
            value["duration_ticks"] = 60;
            value["stacking"] = "refresh_per_source";
            value["remove_with_source"] = true;
            value["remove_on_death"] = true;
            value["granted_tags"] = {3};
            if (args.exercise_effect_preview_failure)
                value["application"]["none"] = {3};
            editor.authoring.apply(*editor.assets, status, editor.authoring.revision(), value, "Source-owned preview policies");
            editor.authoring.add_effect_modifier(*editor.assets, status, 6);
            value = editor.authoring.open(*editor.assets, status).value;
            value["modifiers"][0]["magnitude"] = 5;
            editor.authoring.apply(*editor.assets, status, editor.authoring.revision(), value, "Source-owned preview magnitude");
            editor.refresh_assets();
            shell.authored_selection = status;
            shell.effect_preview_asset = status;
            shell.effect_preview_source = 2;
            shell.show_kit_slots = false;
            shell.effect_preview = std::make_unique<darkangel::editor_app::EffectPreview>(*editor.assets, editor.authoring, status);
            auto base = shell.effect_preview->attributes->value(6);
            if (args.exercise_effect_preview_failure) {
                shell.effect_preview->apply(1);
                auto before = shell.effect_preview->effects->snapshot();
                bool rejected = false;
                try {
                    shell.effect_preview->apply(2);
                } catch (const std::exception& error) {
                    shell.effect_preview->record_failure("Apply effect", error.what(), 2);
                    rejected = true;
                }
                require(rejected && shell.effect_preview->effects->snapshot().size() == 1 &&
                            shell.effect_preview->effects->snapshot().front().handle == before.front().handle &&
                            shell.effect_preview->attributes->value(6) == base + 5,
                        "Blocked preview application changed owned state");
                require(!shell.effect_preview->effects->tags().matches(shell.effect_preview->cooked.definition->application) &&
                            shell.effect_preview->last_error.find("source 2") != std::string::npos,
                        "Blocked preview lost native requirement/context");
            } else {
                shell.effect_preview->apply(1);
                shell.effect_preview->apply(2);
                shell.effect_preview->apply(1);
                require(shell.effect_preview->contributors(3).size() == 2 && shell.effect_preview->attributes->value(6) == base + 10,
                        "Effect preview source grants/modifiers did not coexist");
                shell.effect_preview->destroy_source(1);
                require(shell.effect_preview->contributors(3).size() == 1 &&
                            shell.effect_preview->contributors(3).front().credit.source_network == 2 &&
                            shell.effect_preview->attributes->value(6) == base + 5,
                        "Effect preview source cleanup removed independent owner");
            }
            require(!std::filesystem::exists(editor.assets->source_root() / editor.authoring.open(*editor.assets, status).asset.path),
                    "Effect source preview published source");
            editor.log(args.exercise_effect_preview_failure
                           ? "Blocked isolated effect preview retained prior state and source context."
                           : "Isolated source-owned effect grants/modifiers retained source2 after source1 cleanup.");
        }
        if (args.exercise_effect_preview_reset && frame == 1) {
            auto frozen = shell.effect_preview->cooked.definition;
            auto revision = shell.effect_preview->authoring_revision;
            auto prior = shell.effect_preview->attributes->value(6);
            shell.effect_preview->advance(30);
            shell.effect_preview->reset();
            require(shell.effect_preview->tick == 0 && shell.effect_preview->effects->snapshot().empty() &&
                        shell.effect_preview->attributes->value(6) == prior - 5 && shell.effect_preview->cooked.definition == frozen &&
                        shell.effect_preview->authoring_revision == revision,
                    "Effect preview reset changed frozen definition or retained state");
            auto edited = editor.authoring.open(*editor.assets, shell.effect_preview_asset).value;
            edited["duration_ticks"] = 120;
            editor.authoring.apply(*editor.assets, shell.effect_preview_asset, editor.authoring.revision(), edited,
                                   "Edit lifetime after frozen reset");
            shell.effect_preview->apply(2);
            require(shell.effect_preview->effects->snapshot().front().credit.activation == 1 &&
                        shell.effect_preview->effects->snapshot().front().end == 60 &&
                        shell.effect_preview->authoring_revision != editor.authoring.revision(),
                    "Frozen reset silently loaded edited lifetime or retained activation identity");
            editor.log("Frozen reset retained duration60 while draft became120; activation restarted1 without publication.");
        }
        if (args.exercise_effect_preview_reprepare && frame == 1) {
            auto previous = shell.effect_preview.get();
            auto frozen = shell.effect_preview->cooked.definition;
            auto amount = shell.effect_preview->attributes->value(6);
            auto edited = editor.authoring.open(*editor.assets, shell.effect_preview_asset).value;
            edited["duration_ticks"] = 0;
            editor.authoring.apply(*editor.assets, shell.effect_preview_asset, editor.authoring.revision(), edited,
                                   "Invalid edited finite lifetime");
            bool failed = false;
            try {
                shell.prepare_effect_preview(editor, shell.effect_preview_asset);
            } catch (const std::exception& error) {
                shell.author_diagnostic = error.what();
                failed = true;
            }
            require(failed && shell.effect_preview.get() == previous && shell.effect_preview->cooked.definition == frozen &&
                        shell.effect_preview->effects->snapshot().size() == 1 && shell.effect_preview->attributes->value(6) == amount,
                    "Failed reprepare discarded or patched previous snapshot");
            require(shell.effect_preview->last_error.find("Reprepare") != std::string::npos &&
                        shell.effect_preview->last_error.find("finite duration") != std::string::npos,
                    "Failed reprepare lost frozen/source cook diagnostic");
            shell.effect_preview->advance(30);
            require(shell.effect_preview->tick == 30 && shell.effect_preview->effects->snapshot().size() == 1,
                    "Previous snapshot unusable after failed reprepare");
            editor.log("Failed Effect reprepare retained previous snapshot and advanced it to tick30 without publication.");
        }
        if (args.exercise_effect_preview_corrected && frame == 2) {
            auto old = shell.effect_preview->cooked.definition;
            auto value = editor.authoring.open(*editor.assets, shell.effect_preview_asset).value;
            value["duration_ticks"] = 120;
            editor.authoring.apply(*editor.assets, shell.effect_preview_asset, editor.authoring.revision(), value,
                                   "Correct edited finite lifetime");
            shell.prepare_effect_preview(editor, shell.effect_preview_asset);
            require(shell.author_diagnostic.empty() && shell.effect_preview->last_error.empty() && shell.effect_preview->tick == 0 &&
                        shell.effect_preview->effects->snapshot().empty() &&
                        shell.effect_preview->cooked.definition->generation != old->generation &&
                        shell.effect_preview->authoring_revision == editor.authoring.revision(),
                    "Corrected reprepare retained stale state/error/generation");
            shell.effect_preview->apply(2);
            require(shell.effect_preview->effects->snapshot().front().end == 120 &&
                        shell.effect_preview->effects->snapshot().front().credit.activation == 1,
                    "Corrected snapshot did not load authored lifetime/reset activation");
            shell.author_diagnostic = "Unrelated authoring diagnostic retained by preview preparation";
            shell.prepare_effect_preview(editor, shell.effect_preview_asset);
            require(shell.author_diagnostic == "Unrelated authoring diagnostic retained by preview preparation",
                    "Preview preparation cleared unrelated authoring error");
            shell.author_diagnostic.clear();
            shell.effect_preview->apply(2);
            editor.log(
                "Corrected Effect reprepare loaded duration120/new generation/activation1 and scoped error cleanup without publication.");
        }
        if (args.exercise_player_kit_copy && frame == 0) {
            require(editor.assets && character_resources, "Player Kit copy needs native resources");
            shell.character_selection =
                editor.authoring.create_character(*editor.assets, character_resources->skin, "assigned_kit_character");
            shell.player_selection =
                editor.authoring.create_player(*editor.assets, shell.character_selection, editor.authoring_kit, "assigned_kit_player");
            shell.kit_copy_origin = editor.authoring_kit;
            shell.last_created_kit = editor.authoring.duplicate(*editor.assets, editor.authoring_kit, "assigned_kit_copy");
            editor.authoring_player = shell.player_selection;
            auto before = editor.authoring.open(*editor.assets, shell.player_selection).value;
            editor.authoring.assign_player_kit(*editor.assets, shell.player_selection, shell.last_created_kit);
            auto assigned = editor.authoring.open(*editor.assets, shell.player_selection).value;
            editor.authoring.undo(*editor.assets);
            require(editor.authoring.open(*editor.assets, shell.player_selection).value == before, "Player Kit copy Undo failed");
            editor.authoring.redo(*editor.assets);
            require(editor.authoring.open(*editor.assets, shell.player_selection).value == assigned, "Player Kit copy Redo failed");
            editor.authoring_kit = shell.last_created_kit;
            editor.refresh_assets();
            auto roots = editor.authoring_roots ? editor.authoring_roots() : std::vector<AssetId>{};
            shell.last_validation = editor.authoring.validate(*editor.assets, shell.player_selection, roots);
            require(shell.last_validation->sources == 3, "Player Kit copy closure did not validate three pending sources");
            for (const auto& [id, draft] : editor.authoring.drafts)
                if (draft.pending)
                    require(!std::filesystem::exists(editor.assets->source_root() / draft.asset.path),
                            "Player Kit copy preview published source");
            editor.log("Player Kit copy assignment and private closure passed; no publication/live patch.");
        }
        if (args.exercise_player_kit_play && frame == 1) {
            editor.save_gameplay();
            require(!editor.authoring.dirty(), "Player Kit coordinated Save retained dirty sources");
            for (const auto& [id, draft] : editor.authoring.drafts)
                require(!draft.pending && std::filesystem::exists(editor.assets->source_root() / draft.asset.path),
                        "Player Kit Save lost published source");
            for (const auto& command : editor.authoring.history())
                require(!command.creation, "Published Player Kit creation remained deletable");
            editor.refresh_assets();
        }
        if (args.exercise_player_kit_play && frame == 4) {
            shell.select_workspace(darkangel::editor_app::Workspace::Game);
            shell.focus_gameplay_inspector = true;
            editor.play();
            require(character_resources->combat && character_resources->combat->kit->id == shell.last_created_kit &&
                        !character_resources->combat->actor_generation.empty(),
                    "Player Kit fresh Play retained kit-only or stale resources");
        }
        if (args.exercise_player_kit_context && frame == 1) {
            editor.authoring_kit = shell.kit_copy_origin;
            if (args.exercise_play_player_navigation)
                shell.select_workspace(darkangel::editor_app::Workspace::Ability);
        }
        if (args.exercise_player_kit_context && frame == 2) {
            require(editor.authoring_kit == shell.kit_copy_origin &&
                        editor.authoring.open(*editor.assets, shell.player_selection).value.at("loadout").at("kit") ==
                            shell.last_created_kit.text(),
                    "Inspected Kit changed Player assignment or silently synchronized");
            require(editor.authoring.history().size() == 4, "Player Kit context modified source history");
            editor.log("Player loadout and inspected Kit remain distinct with four authored commands.");
        }
        if (args.exercise_play_player_navigation && frame == 3)
            shell.open_native_asset(editor, editor.authoring_player);
        if (args.exercise_play_player_navigation && frame == 4) {
            require(shell.workspace == darkangel::editor_app::Workspace::Player && shell.player_selection == editor.authoring_player,
                    "Play Player navigation missed actual next-Play definition");
            require(editor.authoring.history().size() == 4 &&
                        editor.authoring.open(*editor.assets, editor.authoring_player).value.at("loadout").at("kit") ==
                            shell.last_created_kit.text(),
                    "Play Player navigation changed source/history");
            editor.log("Play Player native navigation passed; loadout/history retained.");
        }
        if (args.exercise_binding_links && frame == 0) {
            auto& kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            require(kit.contains("effect_bindings") && !kit.at("effect_bindings").empty(), "Binding links need an existing authored row");
            const auto& row = kit.at("effect_bindings")[0];
            shell.binding_ability = AssetId::parse(row.at("ability").get<std::string>());
            shell.binding_effect = AssetId::parse(row.at("effect").get<std::string>());
            shell.binding_block = row.at("hit_block").get<int>();
            shell.binding_power = row.at("power").get<double>();
            shell.authored_selection = shell.binding_ability;
            shell.show_kit_slots = false;
        }
        if (args.exercise_binding_links && frame == 1)
            shell.open_native_asset(editor, shell.binding_effect);
        if (args.exercise_binding_links && frame == 2) {
            require(shell.authored_selection == shell.binding_effect &&
                        editor.authoring.open(*editor.assets, shell.authored_selection).value.at("kind") == "effect",
                    "Existing binding Effect route failed");
            shell.open_native_asset(editor, shell.binding_ability);
        }
        if (args.exercise_binding_links && frame == 3) {
            require(shell.authored_selection == shell.binding_ability &&
                        editor.authoring.open(*editor.assets, shell.authored_selection).value.at("kind") == "ability",
                    "Existing binding Ability route failed");
            require(!editor.authoring.dirty() && editor.authoring.history().empty(), "Binding navigation changed source data/history");
            editor.log("Existing binding Ability/Effect routes passed; native drafts/history unchanged.");
        }
        if (args.exercise_kit_copy_undo && frame == 0) {
            require(editor.assets && editor.authoring_kit != AssetId{}, "Kit copy Undo needs native kit");
            shell.kit_copy_origin = editor.authoring_kit;
            editor.authoring_kit = editor.authoring.duplicate(*editor.assets, editor.authoring_kit, "removed_selection_kit");
            editor.authoring.undo(*editor.assets);
            editor.refresh_assets();
        }
        if (args.exercise_kit_copy_undo && frame == 1) {
            require(editor.authoring_kit != shell.kit_copy_origin && !editor.authoring.drafts.contains(editor.authoring_kit),
                    "Removed Kit selection was silently replaced");
            require(editor.authoring.history().empty() && !editor.authoring.dirty(), "Kit recovery form changed source history");
            editor.log("Removed Kit selection retained explicit recovery without reopening an erased draft.");
        }
        if (args.exercise_pending_selection && frame == 0) {
            require(editor.assets && editor.authoring_kit != AssetId{}, "Pending selection needs native kit");
            auto kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            auto effect = AssetId::parse(kit.at("effects")[0].get<std::string>());
            shell.binding_effect = editor.authoring.duplicate(*editor.assets, effect, "removed_selection_effect");
            editor.authoring.undo(*editor.assets);
            editor.refresh_assets();
        }
        if (args.exercise_pending_selection && frame == 1) {
            require(shell.binding_effect == AssetId{}, "Removed pending effect selection survived authoring draw");
            auto kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            auto input_id = AssetId::parse(kit.at("input").get<std::string>());
            shell.input_authoring_asset = editor.authoring.duplicate(*editor.assets, input_id, "removed_selection_input");
            editor.authoring.undo(*editor.assets);
            editor.refresh_assets();
            shell.open_tool(darkangel::editor_app::ToolView::InputBindings);
        }
        if (args.exercise_pending_selection && frame == 2) {
            require(shell.input_authoring_asset != AssetId{} &&
                        std::any_of(editor.asset_inventory.begin(), editor.asset_inventory.end(),
                                    [&](const auto& asset) { return asset.id == shell.input_authoring_asset; }),
                    "Input Mapper did not recover published selection after creation Undo");
            require(editor.authoring.history().empty() && !editor.authoring.dirty(),
                    "Selection cleanup changed native history or source drafts");
            editor.log("Pending Effect and Input selection cleanup passed; authored sources/history unchanged.");
        }
        if (args.exercise_native_navigation && frame == 0) {
            require(editor.assets && character_resources, "Native navigation exercise needs project and character");
            auto character_asset = editor.authoring.create_character(*editor.assets, character_resources->skin, "navigation_character");
            auto player_asset = editor.authoring.create_player(*editor.assets, character_asset, editor.authoring_kit, "navigation_player");
            editor.refresh_assets();
            auto kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            auto ability = AssetId::parse(kit["slots"][1]["ability"].get<std::string>());
            shell.open_native_asset(editor, ability);
            require(shell.workspace == darkangel::editor_app::Workspace::Ability && shell.authored_selection == ability,
                    "Ability route failed");
            auto value = editor.authoring.open(*editor.assets, ability).value;
            value["costs"][0]["amount"] = 21;
            editor.authoring.apply(*editor.assets, ability, editor.authoring.revision(), value, "Navigation draft cost");
            auto action = AssetId::parse(value["action"].get<std::string>());
            shell.open_native_asset(editor, action);
            require(shell.workspace == darkangel::editor_app::Workspace::Animation && shell.authored_selection == action,
                    "Action route failed");
            auto graph = AssetId::parse(kit["locomotion_stance"].get<std::string>());
            shell.open_native_asset(editor, graph);
            require(shell.workspace == darkangel::editor_app::Workspace::Animation && shell.authored_selection == graph,
                    "Graph route failed");
            auto input = AssetId::parse(kit["input"].get<std::string>());
            shell.open_native_asset(editor, input);
            require(shell.tool == darkangel::editor_app::ToolView::InputBindings && shell.input_authoring_asset == input,
                    "Input route failed");
            shell.open_native_asset(editor, character_asset);
            require(shell.workspace == darkangel::editor_app::Workspace::Character && shell.character_selection == character_asset,
                    "Character route failed");
            shell.open_native_asset(editor, editor.authoring_kit);
            require(shell.workspace == darkangel::editor_app::Workspace::Ability, "Kit route failed");
            auto attributes = AssetId::parse(kit["attributes"].get<std::string>());
            shell.open_native_asset(editor, attributes);
            require(shell.workspace == darkangel::editor_app::Workspace::Ability && shell.authored_selection == attributes,
                    "Attribute route failed");
            shell.open_native_asset(editor, player_asset);
            require(shell.workspace == darkangel::editor_app::Workspace::Player && shell.player_selection == player_asset,
                    "Player route failed");
            bool rejected = false;
            try {
                shell.open_native_asset(editor, character_resources->skin);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && shell.workspace == darkangel::editor_app::Workspace::Player && shell.player_selection == player_asset,
                    "Unsupported navigation changed workflow");
            require(editor.authoring.open(*editor.assets, ability).value == value && editor.authoring.history().size() == 3,
                    "Navigation lost or duplicated history");
            if (args.exercise_validation) {
                auto roots = editor.authoring_roots ? editor.authoring_roots() : std::vector<AssetId>{};
                shell.last_validation = editor.authoring.validate(*editor.assets, player_asset, roots);
                require(shell.last_validation->revision == editor.authoring.revision() && editor.authoring.history().size() == 3,
                        "Private validation changed history");
                if (args.exercise_history_validation)
                    shell.open_tool(darkangel::editor_app::ToolView::AuthoringHistory);
            }
        }
        if (args.exercise_graph_canvas && frame == 1) {
            auto graph = AssetId::parse(
                editor.authoring.open(*editor.assets, editor.authoring_kit).value.at("locomotion_stance").get<std::string>());
            auto root = editor.authoring.open(*editor.assets, graph).value.at("root").get<unsigned>();
            auto graph_value = editor.authoring.open(*editor.assets, graph).value;
            auto root_node = std::find_if(graph_value.at("nodes").begin(), graph_value.at("nodes").end(),
                                          [&](const auto& entry) { return entry.at("id") == root; });
            auto previous = root_node->at("points").at(0).at("input").get<unsigned>();
            auto node = editor.authoring.duplicate_graph_node(*editor.assets, graph, previous);
            editor.authoring.connect_graph_input(*editor.assets, graph, root, 0, node);
            editor.authoring.undo(*editor.assets);
            editor.authoring.redo(*editor.assets);
            editor.authoring.remove_graph_node(*editor.assets, graph, previous);
            editor.refresh_assets();
            shell.open_native_asset(editor, graph);
            shell.graph_node = root;
            require(editor.authoring.history().size() == 3 && editor.authoring.dirty(),
                    "Graph canvas exercise lost native topology history");
            editor.log("Visual native graph: copied clip, connected input, Undo/Redo, removed unused clip; drafts remain private.");
        }
        if (args.exercise_blend_space && frame == 1) {
            shell.show_graph_connections = false;
            shell.graph_point = 2;
            shell.graph_point_node = shell.graph_node;
            shell.graph_lateral = .5f;
            shell.graph_forward = 1.2f;
            require(editor.authoring.history().size() == 3, "Blend point selection changed native history");
        }
        if (args.exercise_independent_ability && !args.exercise_creation_vertical && frame == 1) {
            auto original_id = AssetId::parse(args.authoring_asset);
            auto original = editor.authoring.open(*editor.assets, original_id).value;
            auto copy = editor.authoring.duplicate_ability_with_action(*editor.assets, original_id, "visual_independent_attack", true);
            auto value = editor.authoring.open(*editor.assets, copy).value;
            require(value.at("action") != original.at("action") && editor.authoring.history().size() == 1 &&
                        editor.authoring.history().front().assets.size() == 2,
                    "Independent Ability/Composer creation failed");
            editor.authoring.undo(*editor.assets);
            require(!editor.authoring.drafts.contains(copy), "Independent creation Undo retained Ability");
            editor.authoring.redo(*editor.assets);
            editor.refresh_assets();
            shell.open_native_asset(editor, copy);
            editor.log("Independent Ability/Composer: paired creation, Undo/Redo, native references; no publication.");
        }
        if (args.exercise_history_source_filter && frame == 1) {
            strcpy_s(shell.history_source_filter, "NAVIGATION");
            shell.history_pending_only = true;
            shell.open_tool(darkangel::editor_app::ToolView::AuthoringHistory);
            require(editor.authoring.history().size() == 3 && editor.authoring.dirty(), "History source filter changed authored commands");
        }
        if (args.exercise_validation_error && frame == 1) {
            auto kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
            auto ability = AssetId::parse(kit["slots"][1]["ability"].get<std::string>());
            auto value = editor.authoring.open(*editor.assets, ability).value;
            value["costs"][0]["amount"] = -1;
            editor.authoring.apply(*editor.assets, ability, editor.authoring.revision(), value, "Invalid cost diagnostic");
            shell.last_validation.reset();
            bool failed = false;
            try {
                auto roots = editor.authoring_roots ? editor.authoring_roots() : std::vector<AssetId>{};
                editor.authoring.validate(*editor.assets, shell.player_selection, roots);
            } catch (const std::exception& error) {
                shell.author_diagnostic = error.what();
                editor.log(shell.author_diagnostic, darkangel::editor_app::ConsoleSeverity::Error);
                failed = true;
            }
            require(failed && shell.author_diagnostic.find("Native candidate for ") != std::string::npos &&
                        editor.authoring.open(*editor.assets, ability).value == value,
                    "Validation error lost context/draft");
        }
        if (args.exercise_creation_drafts && frame == 0) {
            require(editor.assets && character_resources && !args.character_preview_clip.empty(),
                    "Creation draft exercise needs project/clip");
            shell.character_selection = editor.authoring.create_character(*editor.assets, character_resources->skin, "draft_character");
            shell.player_selection =
                editor.authoring.create_player(*editor.assets, shell.character_selection, editor.authoring_kit, "draft_player");
            editor.authoring.undo(*editor.assets);
            require(!editor.authoring.drafts.contains(shell.player_selection) &&
                        editor.authoring.drafts.contains(shell.character_selection),
                    "Creation Undo lost shared Character");
            editor.authoring.redo(*editor.assets);
            editor.refresh_assets();
            shell.character_clip = AssetId::parse(args.character_preview_clip);
            shell.prepare_character_pose(shell.character_selection, shell.character_clip);
            shell.character_preview_hash = sha256(editor.authoring.open(*editor.assets, shell.character_selection).value.dump());
            shell.composer_tick = 20;
        }
        if (args.exercise_creation_drafts && frame == 1) {
            editor.authoring.undo(*editor.assets);
            editor.refresh_assets();
            shell.select_workspace(darkangel::editor_app::Workspace::Player);
        }
        if (args.exercise_creation_drafts && frame == 2) {
            require(shell.player_selection == AssetId{} && shell.author_diagnostic.empty(),
                    "Creation Undo retained missing selection or source error");
            auto history = editor.authoring.history(true);
            require(!history.empty() && history.front().assets.size() == 1, "Creation Redo source is unavailable");
            auto player = history.front().assets.front();
            editor.authoring.redo(*editor.assets);
            editor.refresh_assets();
            shell.player_selection = player;
            shell.select_workspace(darkangel::editor_app::Workspace::Character);
        }

        if (args.exercise_history_view && frame == 0) {
            auto& value = editor.authoring.open(*editor.assets, shell.authored_selection).value;
            require(value["kind"] == "ability", "History view exercise needs ability");
            auto original = value;
            value["costs"][0]["amount"] = 21;
            editor.authoring.record_changes(*editor.assets, "Edit cost for history inspection");
            auto history = editor.authoring.history();
            require(history.size() == 1 && history[0].assets[0] == shell.authored_selection, "History view source mismatch");
            editor.authoring.undo(*editor.assets);
            editor.authoring.redo(*editor.assets);
            shell.open_tool(darkangel::editor_app::ToolView::AuthoringHistory);
            std::cout << "Shared history view command, cost path and source identity; draft only, gameplay stopped\n";
        }
        if (args.exercise_graph_preview && frame == 0) {
            if (args.exercise_graph_result) {
                shell.show_graph_connections = false;
                shell.show_blend_geometry = false;
            }
            require(!args.graph_character.empty() && !args.authoring_asset.empty(), "Graph preview exercise needs Character/graph");
            shell.graph_character = AssetId::parse(args.graph_character);
            shell.graph_preview_tags = args.graph_preview_tags;
            shell.graph_forward = 2;
            shell.graph_speed = 2;
            shell.prepare_graph_pose(shell.graph_character, shell.authored_selection);
            shell.graph_preview_hash = sha256(editor.authoring.open(*editor.assets, shell.authored_selection).value.dump() +
                                              editor.authoring.open(*editor.assets, shell.graph_character).value.dump());
            shell.composer_tick = args.composer_tick;
        }
        if (args.exercise_character_preview && frame == 0) {
            require(!args.character_preview_clip.empty(), "Character preview exercise needs a clip");
            shell.character_clip = AssetId::parse(args.character_preview_clip);
            shell.prepare_character_pose(shell.character_selection, shell.character_clip);
            shell.character_preview_hash = sha256(editor.authoring.open(*editor.assets, shell.character_selection).value.dump());
            shell.composer_tick = args.composer_tick;
        }
        if (args.exercise_creation_vertical) {
            if (frame == 0) {
                require(editor.assets && character_resources, "Creation vertical needs prepared project/character");
                auto seed = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
                auto light = AssetId::parse(seed["slots"][0]["ability"].get<std::string>());
                auto light_value = editor.authoring.open(*editor.assets, light).value;
                auto kit = editor.authoring.duplicate(*editor.assets, editor.authoring_kit, "vertical_kit");
                auto ability = args.exercise_independent_ability
                                   ? editor.authoring.duplicate_ability_with_action(*editor.assets, light, "vertical_light", true)
                                   : editor.authoring.duplicate(*editor.assets, light, "vertical_light", true);
                auto action = args.exercise_independent_ability
                                  ? AssetId::parse(editor.authoring.open(*editor.assets, ability).value.at("action").get<std::string>())
                                  : editor.authoring.duplicate(*editor.assets, AssetId::parse(light_value["action"].get<std::string>()),
                                                               "vertical_action");
                auto commit = editor.authoring.add_action_block(
                    *editor.assets, action,
                    {0, 3, 2 * action_tick_units, 2 * action_tick_units, ActionBlockKind::Commit, "Ability.Commit"});
                auto value = editor.authoring.open(*editor.assets, ability).value;
                value["costs"] = {{{"attribute", 3}, {"amount", 12}}};
                value["melee"][0]["power"] = 17;
                value["action"] = action.text();
                value["sources"]["action"] = editor.authoring.open(*editor.assets, action).asset.path;
                value["deferred_commit_block"] = commit;
                editor.authoring.apply(*editor.assets, ability, editor.authoring.revision(), value, "Configure created light and Composer");
                auto effect = editor.authoring.duplicate(*editor.assets, AssetId::parse(seed["effects"][0].get<std::string>()),
                                                         "vertical_status", true);
                editor.authoring.add_effect_modifier(*editor.assets, effect, 6);
                auto status = editor.authoring.open(*editor.assets, effect).value;
                status["duration_ticks"] = 60;
                status["granted_tags"] = {3};
                status["modifiers"][0]["magnitude"] = 5;
                editor.authoring.apply(*editor.assets, effect, editor.authoring.revision(), status, "Configure ordinary created status");
                editor.authoring.assign(*editor.assets, kit, 0, ability);
                editor.authoring.bind(*editor.assets, kit, ability, value["melee"][0]["block"].get<unsigned>(), effect, 5);
                shell.character_selection =
                    editor.authoring.create_character(*editor.assets, character_resources->skin, "vertical_character");
                shell.player_selection = editor.authoring.create_player(*editor.assets, shell.character_selection, kit, "vertical_player");
                editor.authoring.undo(*editor.assets);
                require(!editor.authoring.drafts.contains(shell.player_selection), "Created Player Undo failed");
                editor.authoring.redo(*editor.assets);
                editor.authoring_kit = kit;
                editor.authoring_player = shell.player_selection;
                shell.binding_ability = ability;
                shell.binding_effect = effect;
                shell.composer_asset = action;
                shell.authored_selection = ability;
                editor.refresh_assets();
                auto roots = editor.authoring_roots ? editor.authoring_roots() : std::vector<AssetId>{};
                shell.last_validation = editor.authoring.validate(*editor.assets, shell.player_selection, roots);
                require(shell.last_validation->sources == 6, "Creation vertical did not validate six created sources");
                for (const auto& [id, draft] : editor.authoring.drafts)
                    if (draft.pending)
                        require(!std::filesystem::exists(editor.assets->source_root() / draft.asset.path),
                                "Private validation published creation");
            }
            if (frame == 1) {
                editor.save_gameplay();
                require(!editor.authoring.dirty(), "Creation vertical Save left dirty sources");
                auto published = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
                editor.authoring.undo(*editor.assets);
                require(editor.authoring.dirty(), "Published edit Undo failed");
                editor.authoring.redo(*editor.assets);
                require(!editor.authoring.dirty() && editor.authoring.open(*editor.assets, editor.authoring_kit).value == published,
                        "Published edit Redo lost kit");
                for (const auto& command : editor.authoring.history())
                    require(!command.creation, "Published creation remains deletable by Undo");
                for (const auto& [id, draft] : editor.authoring.drafts)
                    require(!draft.pending && std::filesystem::exists(editor.assets->source_root() / draft.asset.path),
                            "Save did not retain published sources");
                editor.refresh_assets();
            }
            if (frame == 4) {
                shell.select_workspace(darkangel::editor_app::Workspace::Game);
                shell.focus_gameplay_inspector = true;
                shell.select_effect_debug = true;
                editor.play();
                require(character_resources->combat->kit->id == editor.authoring_kit &&
                            character_resources->combat->kit->slots[0].ability == shell.binding_ability,
                        "Fresh Play retained stale kit/ability resources");
            }
        }
        if (args.exercise_actor_workflow && !args.exercise_creation_vertical) {
            if (frame == 0) {
                require(editor.assets && character_resources, "Actor workflow needs a project and prepared character");
                shell.character_selection = editor.authoring.create_character(*editor.assets, character_resources->skin, "gui_character");
                shell.player_selection =
                    editor.authoring.create_player(*editor.assets, shell.character_selection, editor.authoring_kit, "gui_player");
                editor.refresh_assets();
            }
            if (frame == 1) {
                auto& draft = editor.authoring.open(*editor.assets, shell.player_selection).value;
                auto original = draft;
                auto alternate = editor.authoring.duplicate(*editor.assets, shell.character_selection, "gui_character_variant");
                draft["character"] = alternate.text();
                draft["sources"]["character"] = editor.authoring.open(*editor.assets, alternate).asset.path;
                editor.authoring.record_changes(*editor.assets, "Change Player Character");
                require(editor.authoring.dirty(), "Actor edit did not enter history");
                editor.authoring.undo(*editor.assets);
                require(draft == original && editor.authoring.dirty(), "Actor reference Undo failed");
                editor.authoring.redo(*editor.assets);
                require(draft["character"] == alternate.text(), "Actor reference Redo failed");
                editor.authoring.undo(*editor.assets);
            }
            if (frame == 2)
                shell.select_workspace(darkangel::editor_app::Workspace::Character);
            if (frame == 3) {
                shell.select_workspace(darkangel::editor_app::Workspace::Player);
                require(editor.authoring.open(*editor.assets, shell.player_selection).value["character"] ==
                            shell.character_selection.text(),
                        "Actor workflow lost shared Character reference");
            }
            if (frame == 4) {
                if (args.exercise_graph_authoring) {
                    auto& kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
                    auto id = AssetId::parse(kit["locomotion_stance"].get<std::string>());
                    auto& graph = editor.authoring.open(*editor.assets, id).value;
                    auto old = graph;
                    bool found = false;
                    for (auto& node : graph["nodes"])
                        if (node["kind"] == "blend2d") {
                            node["points"][1]["x"] = 2.5;
                            found = true;
                        }
                    require(found, "Graph authoring fixture missing blend");
                    editor.authoring.record_changes(*editor.assets, "Edit locomotion graph");
                    auto changed = graph;
                    editor.authoring.undo(*editor.assets);
                    require(graph == old, "GUI graph Undo failed");
                    editor.authoring.redo(*editor.assets);
                    require(graph == changed, "GUI graph Redo failed");
                    editor.authoring_player = shell.player_selection;
                    editor.save_gameplay();
                    shell.authored_selection = id;
                }
                if (args.exercise_player_input) {
                    auto& kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
                    auto copy =
                        editor.authoring.duplicate(*editor.assets, AssetId::parse(kit["input"].get<std::string>()), "gui_player_input");
                    auto& profile = editor.authoring.open(*editor.assets, copy).value;
                    bool found = false;
                    for (auto& binding : profile["bindings"])
                        if (binding["action"] == kit["slots"][0]["input_action"]) {
                            binding["controls"] = {"Keyboard.K"};
                            found = true;
                        }
                    require(found, "Player light binding not found");
                    auto& player = editor.authoring.open(*editor.assets, shell.player_selection).value;
                    player["input_profile"] = copy.text();
                    player["sources"]["input_profile"] = editor.authoring.open(*editor.assets, copy).asset.path;
                    editor.authoring.record_changes(*editor.assets, "Author Player input override");
                    editor.authoring.undo(*editor.assets);
                    require(!player.contains("input_profile"), "Player input Undo failed");
                    editor.authoring.redo(*editor.assets);
                    editor.authoring_player = shell.player_selection;
                    editor.save_gameplay();
                    shell.input_authoring_asset = copy;
                }
                if (args.exercise_player_defaults) {
                    auto& value = editor.authoring.open(*editor.assets, shell.player_selection).value;
                    value["loadout"]["starting_attributes"] = {{{"attribute", 2}, {"value", 80}}, {{"attribute", 3}, {"value", 60}}};
                    editor.authoring.record_changes(*editor.assets, "Author Player starting values");
                    editor.authoring_player = shell.player_selection;
                    editor.save_gameplay();
                }
                editor.authoring_player = shell.player_selection;
                if (editor.authoring.dirty())
                    editor.save_gameplay();
                shell.select_workspace(darkangel::editor_app::Workspace::Game);
                editor.play();
                std::cout << "Actor workflow created Character/Player and preserved reference history; fresh Player Play\n";
            }
        }
        if (args.exercise_effect_revision) {
            if (frame == 0) {
                require(editor.assets && shell.authored_selection != AssetId{}, "Effect revision exercise needs a native source");
                shell.effect_preview =
                    std::make_unique<darkangel::editor_app::EffectPreview>(*editor.assets, editor.authoring, shell.authored_selection);
                shell.effect_preview_asset = shell.authored_selection;
                require(shell.effect_preview->cooked.definition->lifetime == EffectLifetime::Finite,
                        "Revision fixture needs finite ordinary status");
                shell.effect_preview->apply();
            }
            if (frame == 1) {
                auto value = editor.authoring.open(*editor.assets, shell.authored_selection).value;
                auto original = shell.effect_preview->cooked.definition->duration_ticks;
                value["duration_ticks"] = original + 60;
                editor.authoring.apply(*editor.assets, shell.authored_selection, editor.authoring.revision(), value,
                                       "Edit frozen Effect preview duration");
                require(shell.effect_preview->authoring_revision != editor.authoring.revision() &&
                            shell.effect_preview->cooked.definition->duration_ticks == original,
                        "Preview was silently patched");
                shell.effect_preview->advance(original);
                require(shell.effect_preview->effects->snapshot().empty(), "Frozen Effect duration followed changed draft");
            }
        }
        if (args.exercise_effect_preview) {
            if (frame == 0) {
                auto& definition = editor.authoring.open(*editor.assets, shell.authored_selection).value;
                require(definition["kind"] == "effect", "Effect preview exercise needs an effect");
                definition["evaluator"] = 0;
                definition["lifetime"] = "finite";
                definition["duration_ticks"] = 60;
                definition["period_ticks"] = 0;
                definition["execute_on_apply"] = false;
                definition["granted_tags"] = {3};
                definition["modifiers"] = {{{"attribute", 6}, {"kind", "flat"}, {"magnitude", 7}, {"channel", ""}, {"priority", 0}}};
                editor.authoring.record_changes(*editor.assets, "Isolated preview fixture");
                shell.effect_preview =
                    std::make_unique<darkangel::editor_app::EffectPreview>(*editor.assets, editor.authoring, shell.authored_selection);
                shell.effect_preview_asset = shell.authored_selection;
                shell.effect_preview->apply();
                require(shell.effect_preview->attributes->value(6) == 7 && shell.effect_preview->effects->tags().has(3),
                        "Effect preview missing modifier/tag");
            }
            if (frame == 1)
                shell.effect_preview->advance(30);
            if (frame == 2) {
                shell.effect_preview->remove();
                require(shell.effect_preview->attributes->value(6) == 0 && !shell.effect_preview->effects->tags().has(3),
                        "Preview removal leaked state");
            }
            if (frame == 3) {
                shell.effect_preview->apply();
                shell.effect_preview->advance(60);
                require(shell.effect_preview->effects->snapshot().empty(), "Preview expiry failed");
            }
            if (frame == 4) {
                auto& draft = editor.authoring.open(*editor.assets, shell.authored_selection);
                require(file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved) && !editor.preview.active(),
                        "Preview modified source or live authority");
                std::cout << "Effect preview modifier/tag apply, removal and expiry passed; gameplay=stopped source=unchanged\n";
            }
        }
        if (args.exercise_animation_workflow) {
            if (frame == 1) {
                auto& source = editor.authoring.open(*editor.assets, shell.authored_selection).value;
                auto block = source["blocks"][1];
                editor.authoring.set_action_block_times(*editor.assets, shell.authored_selection, block["id"].get<unsigned>(),
                                                        block["begin"].get<unsigned>(), block["end"].get<unsigned>() + 512);
            }
            if (frame == 2) {
                auto& kit = editor.authoring.open(*editor.assets, editor.authoring_kit).value;
                shell.authored_selection = AssetId::parse(kit["slots"][1]["ability"].get<std::string>());
                shell.select_workspace(darkangel::editor_app::Workspace::Ability);
            }
            if (frame == 3)
                shell.select_workspace(darkangel::editor_app::Workspace::Animation);
            if (frame == 4) {
                require(shell.authored_selection == AssetId::parse(args.authoring_asset) && editor.authoring.dirty(),
                        "Animation navigation discarded action draft");
                editor.authoring.undo(*editor.assets);
                require(!editor.authoring.dirty(), "Animation draft Undo failed");
                std::cout << "Animation workflow preserved action drafts across Ability and restored Undo\n";
            }
        }
    }
    void finish() override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& shell = context_.shell;
        const auto& character_resources = context_.resources;
        const auto& character = context_.session;
        const auto& import_baseline = context_.scene_baseline;
        if (args.exercise_effect_revision) {
            auto& draft = editor.authoring.open(*editor.assets, shell.authored_selection);
            require(editor.authoring.dirty() && file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved) &&
                        !editor.preview.active() && !character && editor.document->world().serialize() == import_baseline,
                    "Effect revision changed source/scene/gameplay");
            std::cout << "Effect snapshot revision retained original duration/expiry after draft edit; source/scene unchanged, gameplay "
                         "stopped\n";
        }
        if (args.exercise_validation) {
            if (args.exercise_validation_error) {
                require(!shell.last_validation && !shell.author_diagnostic.empty(), "Failed validation retained success receipt");
                std::cout << "Private validation failure retained invalid draft, source/consumer context and unchanged publication\n";
            } else {
                require(shell.last_validation && shell.last_validation->revision == editor.authoring.revision(),
                        "Validation receipt became stale without editing");
                std::cout << "Private native validation sources=" << shell.last_validation->sources
                          << "; pending/consumer closure valid, source and catalog unpublished\n";
            }
        }
        if (args.exercise_history_validation) {
            require(shell.workspace == darkangel::editor_app::Workspace::Tools &&
                        shell.tool == darkangel::editor_app::ToolView::AuthoringHistory && shell.last_validation &&
                        shell.last_validation->sources == 3 && editor.authoring.history().size() == 3,
                    "Shared history validation lost source/command context");
            std::cout << "Shared history validation: two new drafts, one changed source, three private validated sources, three retained "
                         "commands\n";
        }
        done_ = true;
    }
    bool done() const override {
        return done_;
    }
    std::string_view result() const override {
        return done_ ? "Authoring source exercises passed" : "Authoring source exercises pending";
    }
};
} // namespace
std::unique_ptr<Exercise> make_authoring_sources(AuthoringSourceContext context) {
    return std::make_unique<AuthoringSources>(context);
}
} // namespace darkangel::editor_app::exercises
