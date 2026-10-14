#include "character_preview.hpp"
#include <darkangel/scene_inventory.hpp>
#include <algorithm>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}}
void prepare_player_inventory(const std::filesystem::path& registry,const std::filesystem::path& cas,const CookedPlayer& player,CharacterPreviewResources& resources){
    if(!player.definition.inventory)return;auto& combat=*resources.combat;const auto& starting=*player.definition.inventory;
    auto prepared=std::make_shared<SceneInventoryDefinition>();auto catalog=std::make_shared<ItemCatalog>();
    prepared->starting=starting.items;prepared->equipped=starting.equipped;prepared->active=starting.active;prepared->base={player.base_kit.definition,combat.input,player.base_kit.abilities};prepared->generation=player.definition.generation;
    auto include=[&](const CookedCombatKit& kit){
        require(kit.attributes.id==player.base_kit.attributes.id&&kit.attributes.generation==player.base_kit.attributes.generation&&kit.stance.rig.definition.id==resources.rig.definition.id&&kit.stance.rig.definition.signature==resources.rig.definition.signature&&bool(kit.tags)==bool(player.base_kit.tags)&&(!kit.tags||kit.tags->generation==player.base_kit.tags->generation),"Inventory mask must share Player attributes, tags and rig");validate_combat_kit(*kit.definition,combat.input);
        auto clip=[&](AssetId id){if(!combat.clips.contains(id)){auto value=load_cooked_clip(registry,cas,id);combat.clips.emplace(id,std::make_shared<const AnimationClip>(value.definition,value.archive));}};
        for(const auto& ability:kit.abilities){auto existing=std::find_if(combat.abilities.begin(),combat.abilities.end(),[&](auto a){return a->id==ability->id;});require(existing==combat.abilities.end()||(*existing)->generation==ability->generation,"Inventory mask ability generation conflict");if(existing==combat.abilities.end())combat.abilities.push_back(ability);require(ability->action->motion.has_value(),"Inventory mask action needs frozen motion");clip(ability->action->motion->clip.id);}
        for(const auto& effect:kit.effects){auto existing=std::find_if(combat.effects.begin(),combat.effects.end(),[&](auto e){return e->id==effect->id;});require(existing==combat.effects.end()||(*existing)->generation==effect->generation,"Inventory mask effect generation conflict");if(existing==combat.effects.end())combat.effects.push_back(effect);if(effect->reaction)clip(effect->reaction->motion->clip.id);}
    };
    include(player.base_kit);
    // The catalogue is every cooked item of the project, so pickups and grants anywhere resolve; the Player's own items
    // are among them. Every mask item gets its loadout prepared, owned at start or not.
    auto everything=load_cooked_items(registry,cas);for(const auto& item:player.items)if(std::none_of(everything.begin(),everything.end(),[&](const auto& known){return known.definition.id==item.definition.id;}))everything.push_back(item);
    for(const auto& item:everything){catalog->insert(item.definition);prepared->generation+=item.generation;if(item.definition.mask==AssetId{}||prepared->masks.contains(item.definition.mask))continue;
        auto mask=load_cooked_mask(registry,cas,item.definition.mask);include(mask.kit);prepared->masks.emplace(mask.definition.id,AbilityLoadout{mask.kit.definition,combat.input,mask.kit.abilities,mask.definition.id});resources.inventory_mask_names.emplace(mask.definition.id,mask.definition.name);
        const auto& m=mask.definition;if(m.visual!=AssetId{}){auto socket=resources.rig.definition.sockets.find(m.socket);require(socket!=resources.rig.definition.sockets.end(),"Inventory mask socket is not defined by Character");auto joint=std::find_if(resources.rig.definition.joints.begin(),resources.rig.definition.joints.end(),[&](auto j){return j.key==socket->second;});require(joint!=resources.rig.definition.joints.end(),"Inventory mask joint missing");load_cooked_model(registry,cas,m.visual);resources.inventory_masks.emplace(m.id,WornMask{m.id,m.visual,m.name,m.tint,unsigned(joint-resources.rig.definition.joints.begin()),m.position,m.rotation,m.scale});}
    }
    prepared->catalog=std::move(catalog);combat.inventory=std::move(prepared);
    const char* names[]={"mask.up","mask.right","mask.down","mask.left"};for(unsigned slot=0;slot<4;++slot)for(const auto& action:combat.input.actions)if(action.name==names[slot]&&action.kind==InputActionKind::Button)combat.loadout_actions[slot]=action.id;
}
}
