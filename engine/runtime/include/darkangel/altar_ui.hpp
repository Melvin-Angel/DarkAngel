#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace darkangel {
// The altar menu's view model: what the player may do at the altar it used. Plain values; the host decides.
struct AltarUiDestination {std::string key,element,name;bool here{};bool operator==(const AltarUiDestination&)const=default;};
struct AltarUiModel {
    bool open{},can_rest{},can_travel{},busy{};std::string name,status;std::vector<AltarUiDestination> destinations;
    bool operator==(const AltarUiModel&)const=default;
};
enum class AltarUiActionKind {Rest,Travel,Close};
struct AltarUiAction {AltarUiActionKind kind{};std::size_t destination{};};
}
