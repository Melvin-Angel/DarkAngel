#pragma once
#include <darkangel/inventory.hpp>
#include <filesystem>
namespace darkangel {
struct CookedItem {ItemDefinition definition;std::string generation;};
CookedItem load_cooked_item(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
