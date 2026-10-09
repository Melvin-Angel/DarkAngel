#pragma once
#include <darkangel/tags.hpp>
#include <filesystem>
namespace darkangel {
struct TagAssetField {AssetId key;TagDefinition definition;};
struct TagAsset {
 AssetId id;std::string generation;std::vector<TagAssetField> fields;
 std::shared_ptr<const TagDictionary> dictionary()const;
};
TagAsset decode_tag_asset(std::string_view);
TagAsset load_cooked_tags(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
