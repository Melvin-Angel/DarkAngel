#include "preview_results.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../character_preview.hpp"
#include <darkangel/check.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace darkangel::editor_app::exercises {
void check_preview_results(const PreviewResultContext& context) {
    const auto& args = context.options;
    auto& editor = context.editor;
    auto& shell = context.shell;
    const auto& import_baseline = context.scene_baseline;
    const auto* character = context.session;
    const auto* composer_pose = context.composer_pose;
    const auto* graph_pose = context.graph_pose;
    const bool authoring_skin = context.skin_ready;
    const auto joint_count = context.joint_count;
    if (args.composer_playback) {
        const auto expected = std::min(args.composer_tick + args.frames, double(composer_pose->clip().ticks));
        require(std::abs(shell.composer_tick - expected) < 1e-6, "Composer preview clock failed");
        if (expected == composer_pose->clip().ticks)
            require(!shell.composer_playing, "Preview did not stop at clip end");
        std::cout << "Composer preview playback clock=" << shell.composer_tick << " source=unchanged\n";
    }
    if (args.exercise_composer) {
        require(composer_pose && shell.composer_visible && !editor.preview.active() && !character && !editor.authoring.dirty() &&
                    editor.document->world().serialize() == import_baseline,
                "Composer scrub changed gameplay or authoring state");
        if(composer_pose->action()){unsigned hits=0,authored_hits=0;for(const auto& block:composer_pose->action()->blocks)hits+=block.hit.has_value();for(const auto& block:editor.authoring.open(*editor.assets,composer_pose->action()->id).value.at("blocks"))authored_hits+=block.contains("hit");require(hits==authored_hits,"Composer preview used stale gameplay payloads");std::cout<<"Composer typed hit payloads="<<hits<<" camera="<<shell.composer_view._44<<","<<shell.composer_projection._34<<"\n";}
        std::cout << "Composer isolated pose tick=" << shell.composer_tick << " joints=" << joint_count
                  << " gameplay=stopped source=unchanged\n";
    }
    if (!args.mask_editor.empty()) {
        require(editor.assets && shell.mask_editor == AssetId::parse(args.mask_editor) &&
                    editor.authoring.drafts.contains(shell.mask_editor) &&
                    editor.authoring.drafts.at(shell.mask_editor).value.at("kind") == "mask" && shell.author_diagnostic.empty() &&
                    !editor.authoring.dirty() && !editor.preview.active(),
                "Mask Editor did not stay open on the mask draft, or opening it changed a source");
        std::cout << "Mask Editor open mask=" << editor.authoring.drafts.at(shell.mask_editor).value.at("name").get<std::string>()
                  << " workspace="
                  << (shell.workspace == darkangel::editor_app::Workspace::Npc      ? "npc"
                      : shell.workspace == darkangel::editor_app::Workspace::Player ? "player"
                                                                                    : "other")
                  << " selection_kept="
                  << ((shell.workspace == darkangel::editor_app::Workspace::Npc ? shell.npc_selection : shell.player_selection) !=
                              shell.mask_editor
                          ? 1
                          : 0)
                  << " dirty=0\n";
    }
    if (args.exercise_native_navigation) {
        require(editor.authoring.dirty() && !editor.preview.active() && !character &&
                    editor.document->world().serialize() == import_baseline,
                "Native navigation changed gameplay or scene");
        for (const auto& [asset, draft] : editor.authoring.drafts) {
            if (draft.pending)
                require(!std::filesystem::exists(editor.assets->source_root() / draft.asset.path), "Navigation published pending source");
            else
                require(file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved),
                        "Navigation published edited source");
        }
        std::cout << "Native typed navigation: Player/Character/input/kit/ability/action/graph/attributes, pending history retained, "
                     "invalid route preserved, source and gameplay unchanged\n";
    }
    if (args.exercise_creation_drafts) {
        require(editor.authoring.dirty() && !editor.preview.active() && !character && composer_pose && authoring_skin &&
                    shell.composer_visible && editor.document->world().serialize() == import_baseline,
                "Pending creation preview changed gameplay/scene");
        for (auto id : {shell.character_selection, shell.player_selection}) {
            const auto& draft = editor.authoring.open(*editor.assets, id);
            require(draft.pending && !std::filesystem::exists(editor.assets->source_root() / draft.asset.path),
                    "Pending creation published source");
        }
        auto catalog = editor.assets->assets();
        for (auto id : {shell.character_selection, shell.player_selection})
            require(std::none_of(catalog.begin(), catalog.end(), [&](const auto& asset) { return asset.id == id; }),
                    "Pending creation registered catalog identity");
        std::cout << "Creation draft Undo/Redo/Character preview joints=" << joint_count
                  << " source absent, catalog unchanged, gameplay stopped\n";
    }
    if (args.exercise_history_view) {
        require(editor.authoring.dirty() && !editor.preview.active() && !character &&
                    editor.document->world().serialize() == import_baseline,
                "History view changed gameplay or scene");
        auto& draft = editor.authoring.open(*editor.assets, shell.authored_selection);
        require(file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved), "History view published source");
        std::cout << "History pending cost21; source unchanged, scene unchanged, gameplay stopped\n";
    }
    if (args.exercise_graph_result) {
        require(graph_pose && graph_pose->inputs().count > 0 && graph_pose->inputs().count <= 4, "Missing bounded native graph result");
        float sum = 0;
        for (const auto& layer : graph_pose->inputs().span()) {
            require(layer.clip && std::isfinite(layer.weight) && layer.weight >= 0, "Invalid native graph result layer");
            sum += layer.weight;
        }
        require(std::abs(sum - 1) < .001f, "Native graph result weights not normalized");
        std::cout << "Native graph blend result clips=" << graph_pose->inputs().count << " normalized=" << sum
                  << " tick=" << graph_pose->state().tick << " gameplay=stopped\n";
    }
    if (args.exercise_graph_preview) {
        require(graph_pose && authoring_skin && shell.composer_visible && !editor.preview.active() && !character &&
                    !editor.authoring.dirty() && editor.document->world().serialize() == import_baseline,
                "Graph preview changed gameplay/source state");
        auto& draft = editor.authoring.open(*editor.assets, shell.authored_selection);
        require(file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved), "Graph preview published source");
        std::cout << "Graph isolated preview tick=" << graph_pose->state().tick << " joints=" << joint_count
                  << " gameplay=stopped source=unchanged\n";
    }
    if (args.exercise_character_preview) {
        require(composer_pose && authoring_skin && shell.composer_visible && !editor.preview.active() && !character &&
                    !editor.authoring.dirty() && editor.document->world().serialize() == import_baseline,
                "Character preview changed gameplay/source state");
        auto& draft = editor.authoring.open(*editor.assets, shell.character_selection);
        require(file_sha256(editor.assets->source_root() / draft.asset.path) == sha256(draft.saved), "Character preview published source");
        std::cout << "Authored Character preview tick=" << shell.composer_tick << " joints=" << joint_count
                  << " gameplay=stopped source=unchanged\n";
    }
}
} // namespace darkangel::editor_app::exercises
