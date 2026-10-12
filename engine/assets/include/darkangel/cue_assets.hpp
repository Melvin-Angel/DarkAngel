#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/cue_player.hpp>
namespace darkangel {
// Every cue product of a verified cooked registry, in registry order. Two cues answering to one key are refused.
std::vector<CueDefinition> load_cooked_cues(const std::filesystem::path& registry,const std::filesystem::path& cas);
}
