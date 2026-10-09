#pragma once
#include <darkangel/tag_assets.hpp>
namespace darkangel {
struct TagConstants {std::string cpp,luau;};
// Export a prepared registry. Generated runtime IDs are fenced by its UUID and
// generation; persistent keys remain available for authoring/rename correlation.
TagConstants generate_tag_constants(const TagAsset&);
// Publish both outputs in one immutable content-addressed directory. An
// existing matching generation is reused; conflicting contents reject.
std::filesystem::path publish_tag_constants(const TagAsset&,const std::filesystem::path& destination);
}
