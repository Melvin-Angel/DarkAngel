#include "pose_previews.hpp"
#include "editor_controller.hpp"
#include "editor_ui.hpp"
#include "package_file.hpp"
#include "render/render_system.hpp"
#include "render/scene_resources.hpp"
#include <darkangel/check.hpp>
#include <imgui.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

namespace darkangel::editor_app {
using render::upload_skin;
PosePreviews::PosePreviews(PosePreviewContext previews)
    :editor(previews.editor),shell(previews.shell),device(previews.render.device),context(previews.render.context),skin_pso(previews.scene.skin_pso),skin_cull(previews.scene.skin_cull),character_resources(previews.resources){}
GraphParameters PosePreviews::graph_parameters(const darkangel::editor_app::GraphPosePreview& pose){GraphParameters parameters{shell.graph_speed,shell.graph_forward,shell.graph_lateral,1};if(auto dictionary=pose.tag_dictionary()){preview_graph_tags={dictionary->registry(),dictionary->generation(),{}};for(const auto& tag:shell.graph_preview_tags)preview_graph_tags.values.push_back(tag.get<TagId>());parameters.tags=&preview_graph_tags;}return parameters;}
void PosePreviews::reset(){graph_pose.reset();shell.graph_preview_asset={};composer_pose.reset();composer_matrices.clear();shell.composer_clip={};}
void PosePreviews::install(){
    if(!(character_resources&&editor.assets))return;
        shell.prepare_composer=[this](AssetId clip,unsigned duration){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");
            auto registry=editor.assets->cas_path().parent_path()/("composer-"+AssetId::random().text()+".json");
            ComposerPackageFile cleanup{registry};const std::array roots{clip};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
            auto candidate=std::make_unique<darkangel::editor_app::ComposerPosePreview>(*character_resources,load_cooked_clip(registry,editor.assets->cas_path(),clip),duration);
            auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);
            editor.log("Prepared isolated frozen clip/character pose preview; gameplay remains stopped.");
        };
        shell.prepare_action_pose=[this](AssetId action){
            require(!editor.preview.active(),"Stop Play before preparing Composer preview");auto& kit=editor.authoring.open(*editor.assets,editor.authoring_kit).value;auto graph=AssetId::parse(kit.at("locomotion_stance").get<std::string>());
            auto candidate=darkangel::editor_app::prepare_authored_action_pose(*editor.assets,editor.authoring,*character_resources,action,graph);auto matrices=candidate->sample(0);composer_pose=std::move(candidate);graph_pose.reset();shell.graph_preview_asset={};shell.character_preview_asset={};composer_matrices=std::move(matrices);editor.log("Prepared frozen action/locomotion pose preview from private drafts; gameplay stays stopped.");
        };
        shell.scrub_composer=[this](double tick){require((composer_pose||graph_pose)&&!editor.preview.active(),"Pose preview unavailable during Play");if(graph_pose){require(std::isfinite(tick)&&tick>=0&&tick<=600,"Graph preview tick bounds");composer_matrices=graph_pose->sample(static_cast<unsigned>(tick),graph_parameters(*graph_pose));}else composer_matrices=composer_pose->sample(tick);};
        shell.prepare_character_pose=[this](AssetId asset,AssetId clip){
            require(!editor.preview.active(),"Stop Play before preparing Character preview");auto candidate=darkangel::editor_app::prepare_authored_character_pose(*editor.assets,editor.authoring,asset,clip);auto matrices=candidate.pose->sample(0);auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);authoring_character_definition=std::move(candidate.character.definition);composer_pose=std::move(candidate.pose);graph_pose.reset();shell.graph_preview_asset={};composer_matrices=std::move(matrices);shell.composer_clip=clip;shell.composer_duration=candidate.clip.definition.ticks*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;shell.character_preview_asset=asset;shell.composer_asset={};
        };
        shell.draw_graph_info=[this]{if(!graph_pose||shell.graph_preview_asset!=shell.authored_selection)return;if(ImGui::CollapsingHeader("Native graph blend result",ImGuiTreeNodeFlags_DefaultOpen)){const auto& state=graph_pose->state();auto parameters=graph_pose->parameters();ImGui::Text("Frozen graph %.12s | tick %llu | phase %.5f",state.generation.c_str(),static_cast<unsigned long long>(state.tick),state.phase);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",state.generation.c_str());ImGui::Text("Sampled speed %.3f | forward %.3f | lateral %.3f",parameters.speed,parameters.forward,parameters.lateral);const auto& inputs=graph_pose->inputs();ImGui::Text("Root output: %u native pose layers",inputs.count);for(unsigned i=0;i<inputs.count;++i){const auto& layer=inputs.layers[i];auto id=layer.clip->definition().id;std::string name=id.text();for(const auto& asset:editor.asset_inventory)if(asset.id==id){name=std::filesystem::path(asset.path).filename().string();break;}ImGui::TextWrapped("%s | weight %.4f | clip tick %.3f",name.c_str(),layer.weight,layer.tick);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",id.text().c_str());}ImGui::TextWrapped("These are frozen native sampling results, not authored weights or authoritative gameplay decisions.");}};
        shell.prepare_graph_pose=[this](AssetId character_asset,AssetId graph_asset){require(!editor.preview.active(),"Stop Play before graph preview");auto candidate=darkangel::editor_app::prepare_authored_graph_pose(*editor.assets,editor.authoring,character_asset,graph_asset);auto matrices=candidate.pose->sample(0,graph_parameters(*candidate.pose));auto uploaded=upload_skin(device,std::move(candidate.character.skin),skin_pso.RawPtr(),skin_cull.RawPtr(),nullptr,0,false);context->Flush();context->WaitForIdle();authoring_skin=std::move(uploaded);graph_pose=std::move(candidate.pose);composer_pose.reset();composer_matrices=std::move(matrices);shell.character_preview_asset={};shell.graph_preview_asset=graph_asset;shell.composer_asset={};shell.composer_clip={};shell.composer_duration=600*action_tick_units;shell.composer_tick=0;shell.composer_playing=false;};
        shell.draw_character_info=[this]{if(!authoring_skin)return;const auto& model=authoring_skin->model;const auto& rig=model.rig.definition;ImGui::Text("%zu joints | %zu meshes",rig.joints.size(),model.meshes.size());if(ImGui::TreeNode("Skeleton")){for(unsigned i=0;i<rig.joints.size();++i)ImGui::Text("%u: %s | parent %d",i,rig.joints[i].key.c_str(),rig.joints[i].parent);ImGui::TreePop();}if(ImGui::TreeNode("Sockets")){for(const auto& [slot,joint]:rig.sockets)ImGui::Text("%s > %s",slot.c_str(),joint.c_str());ImGui::TreePop();}};
        shell.composer_info=[this]{if(graph_pose)return std::string("Graph | frozen generation ")+graph_pose->state().generation.substr(0,12)+" | fixed tick "+std::to_string(graph_pose->state().tick);std::string name="Clip";for(const auto& asset:editor.asset_inventory)if(asset.id==composer_pose->clip().id)name=std::filesystem::path(asset.path).stem().string();auto label=name+" | frozen clip "+std::string(composer_pose->generation().substr(0,12));if(auto action=composer_pose->action()){label+=" | action "+action->generation.substr(0,12);if(action->mask)label+=" | upper-body mask "+action->mask->generation.substr(0,12);}return label;};
}
} // namespace darkangel::editor_app
