#include <darkangel/json_fields.hpp>
#include <darkangel/check.hpp>
#include <darkangel/interaction.hpp>
#include <darkangel/asset_id.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json = nlohmann::json;
using darkangel::require;
} // namespace
void validate_interactable(const InteractableDefinition& definition) {
    require(!definition.prompt.empty() && definition.prompt.size() <= 48, "An interactable's prompt is 1 to 48 characters");
    require(world_name_valid(definition.event) && (definition.requires_tag.empty() || world_name_valid(definition.requires_tag)),
            "World event and tag names are lowercase letters, digits, '_' and '.', up to 48 characters");
    if(definition.puzzle){require(AssetId::parse(definition.puzzle->document)!=AssetId{}&&!definition.once&&!definition.puzzle->sequence.empty()&&definition.puzzle->sequence.size()<=8,"Puzzle identity/sequence/once policy");for(auto n:definition.puzzle->sequence)require(n>=1&&n<=9,"Puzzle rune bounds");}
    require(std::isfinite(definition.radius) && definition.radius >= .5 && definition.radius <= 10,
            "An interactable's radius is 0.5 to 10 m");
}
InteractableDefinition decode_interactable(std::string_view text) {
    require(text.size() <= 4096, "Interactable component size bound");
    const auto value = Json::parse(text);
    require(value.is_object(), "Interactable component type");
    json_fields::known(value, {"prompt", "event", "radius", "once", "requires_tag", "puzzle"}, "Interactable component has an unknown field");
    require(value.contains("prompt") && value.at("prompt").is_string() && value.contains("event") && value.at("event").is_string(),
            "An interactable needs a prompt and an event");
    InteractableDefinition result;
    result.prompt = value.at("prompt").get<std::string>();
    result.event = value.at("event").get<std::string>();
    if (value.contains("radius")) {
        require(value.at("radius").is_number(), "Interactable radius type");
        result.radius = value.at("radius").get<double>();
    }
    result.once = json_fields::flag(value, "once", result.once, "Interactable once type");
    if (value.contains("requires_tag")) {
        require(value.at("requires_tag").is_string(), "Interactable required tag type");
        result.requires_tag = value.at("requires_tag").get<std::string>();
        require(!result.requires_tag.empty(), "An interactable's required tag is a name; leave the field out for none");
    }
    if (value.contains("puzzle") && !value.at("puzzle").is_null()) {
        const auto& puzzle = value.at("puzzle");
        require(puzzle.is_object() && puzzle.size() == 2 && puzzle.contains("document") && puzzle.at("document").is_string() && puzzle.contains("sequence") &&
                    puzzle.at("sequence").is_array(),
                "A puzzle names its UI document and its sequence");
        InteractablePuzzle made;
        made.document = puzzle.at("document").get<std::string>();
        require(made.document.size() == 36, "A puzzle's UI document is an asset identity");
        require(!puzzle.at("sequence").empty() && puzzle.at("sequence").size() <= 8, "A puzzle sequence has one to eight presses");
        for (const auto& press : puzzle.at("sequence")) {
            require(press.is_number_unsigned() && press.get<std::uint64_t>() >= 1 && press.get<std::uint64_t>() <= 9, "Puzzle presses are 1 to 9");
            made.sequence.push_back(press.get<unsigned>());
        }
        // A puzzle is used many times (each use opens its panel); it is solved once.
        require(!result.once, "A puzzle interactable is not once-only (each use opens its panel)");
        result.puzzle = std::move(made);
    }
    validate_interactable(result);
    return result;
}
const char* interaction_result_text(InteractionResult result) {
    switch (result) {
    case InteractionResult::Accepted:
        return "Accepted";
    case InteractionResult::Unknown:
        return "No such interactable";
    case InteractionResult::OutOfReach:
        return "Out of reach";
    case InteractionResult::Unavailable:
        return "Not available";
    case InteractionResult::Dead:
        return "The player is dead";
    case InteractionResult::UsedUp:
        return "Used up";
    }
    return "Unknown result";
}
WorldInteractable::WorldInteractable(InteractableDefinition definition, MotorVec position)
    : definition_(std::move(definition)), position_(position) {
    validate_interactable(definition_);
    require(std::isfinite(position_.x) && std::isfinite(position_.y) && std::isfinite(position_.z), "Interactable position");
}
bool WorldInteractable::reaches(MotorVec player) const {
    const double dx = player.x - position_.x, dy = player.y - position_.y, dz = player.z - position_.z;
    return dx * dx + dy * dy + dz * dz <= definition_.radius * definition_.radius;
}
InteractionResult WorldInteractable::use(WorldEvents& world, MotorVec player, bool player_alive) {
    if (!player_alive)
        return InteractionResult::Dead;
    if (!reaches(player))
        return InteractionResult::OutOfReach;
    if ((definition_.once && used_) || (!definition_.requires_tag.empty() && !world.tag(definition_.requires_tag)))
        return InteractionResult::Unavailable;
    world.emit(definition_.event);
    ++used_;
    return InteractionResult::Accepted;
}
} // namespace darkangel
