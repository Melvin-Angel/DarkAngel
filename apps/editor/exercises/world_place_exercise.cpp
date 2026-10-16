#include "world_place_exercise.hpp"
#include "../editor_world_objects.hpp"
#include "../prefab_commands.hpp"
#include <darkangel/scene_travel.hpp>
#include <darkangel/world_objects.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {
void require(bool b, const std::string& e) {
    if (!b)
        throw std::runtime_error(e);
}
} // namespace
// Each step places one object through the Scene tab's Place path, then edits its component the way the Inspector does,
// as one scene edit each. Values may name earlier steps' objects as "@key" (their stable identity) and the scene's own
// asset as "@scene"; "@item:<path>" names an item asset by its project path.
std::map<std::string, StableId> place_world_plan(Controller& editor, Shell& shell, const nlohmann::json& plan) {
    require(plan.is_object() && plan.contains("objects") && plan.at("objects").is_array() && plan.at("objects").size() <= 64,
            "World placement plan shape");
    shell.refresh_place_items(editor);
    std::map<std::string, StableId> placed;
    auto item = [&](int gameplay) {
        auto found = std::find_if(shell.place_items.begin(), shell.place_items.end(),
                                  [&](const auto& candidate) { return candidate.gameplay == gameplay; });
        require(found != shell.place_items.end(), "The Place panel lacks a world object item");
        return *found;
    };
    std::function<void(nlohmann::json&)> resolve = [&](nlohmann::json& value) {
        if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (text == "@scene")
                value = editor.document->root().text();
            else if (text.starts_with("@item:")) {
                const auto path = text.substr(6);
                auto asset = std::find_if(editor.asset_inventory.begin(), editor.asset_inventory.end(),
                                          [&](const auto& a) { return a.path == path; });
                require(asset != editor.asset_inventory.end(), "No item asset at " + path);
                value = asset->id.text();
            } else if (text.starts_with("@")) {
                auto found = placed.find(text.substr(1));
                require(found != placed.end(), "Unknown placed object " + text);
                value = found->second.text();
            }
        } else if (value.is_object() || value.is_array())
            for (auto& child : value)
                resolve(child);
    };
    const std::map<std::string, std::pair<int, const char*>> kinds{
        {"pickup", {place_pickup, "pickup"}},    {"altar", {place_altar, "altar"}}, {"door", {place_door, "mover"}},
        {"platform", {place_platform, "mover"}}, {"entry", {7, "entry"}},           {"transition", {8, "transition"}},
        {"interactable", {4, "interactable"}}};
    for (auto step : plan.at("objects")) {
        const auto kind = step.at("kind").get<std::string>();
        // A plain model placement, as the Place panel's Environment items do.
        if (kind == "model") {
            const auto object =
                shell.place(editor, Shell::PlaceItem{AssetId::parse(step.at("asset").get<std::string>()), "Model", "Environment", false, 0},
                            step.at("x").get<double>(), step.at("z").get<double>(), step.value("yaw", 0.0));
            if(step.contains("materials")){const auto model=AssetId::parse(step.at("asset").get<std::string>());const auto& table=step.at("materials");require(table.is_object()&&table.size()<=128,"World placement material table limit");
                for(const auto& [slot,value]:table.items()){const auto material=value.is_null()?AssetId{}:AssetId::parse(value.get<std::string>());if(material!=AssetId{}&&editor.prepare_material)editor.prepare_material(model,AssetId::parse(slot),material);set_object_material(*editor.document,object,AssetId::parse(slot),material);}}
            if (step.contains("key"))
                placed[step.at("key").get<std::string>()] = object;
            continue;
        }
        auto known = kinds.find(kind);
        require(known != kinds.end(), "Unknown world placement kind " + kind);
        const double yaw = step.value("yaw", 0.0);
        std::optional<double> height;
        if (step.contains("y"))
            height = step.at("y").get<double>();
        const auto object =
            shell.place(editor, item(known->second.first), step.at("x").get<double>(), step.at("z").get<double>(), yaw, height);
        if (step.contains("component")) {
            auto component = step.at("component");
            resolve(component);
            auto current = nlohmann::json::parse([&] {
                const auto& p = editor.document->plan();
                if (kind == "pickup")
                    return p.pickups.at(object);
                if (kind == "altar")
                    return p.altars.at(object);
                if (kind == "entry")
                    return p.entries.at(object);
                if (kind == "transition")
                    return p.transitions.at(object);
                if (kind == "interactable")
                    return p.interactables.at(object);
                return p.movers.at(object);
            }());
            current.merge_patch(component);
            scene_set_component(editor, object, known->second.second, current, "Edited a placed world object.");
        }
        if (step.contains("key"))
            placed[step.at("key").get<std::string>()] = object;
    }
    editor.save(editor.path);
    for (const auto& [key, id] : placed)
        std::cout << "Editor world_place " << key << "=" << id.text() << "\n";
    std::cout << "Editor world_place scene=" << editor.document->root().text() << " objects=" << placed.size() << std::endl;
    return placed;
}
} // namespace darkangel::editor_app
