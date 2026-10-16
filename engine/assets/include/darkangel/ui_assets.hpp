#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// R6.1 UI document (.daui under ui/): one native source holding the descriptor and its owned markup and style, so a Save
// publishes all three together. Purpose and permitted surfaces are separate; the attachment that uses a document picks
// the surface. Documents bind the "ui" data model (see UiPreviewSample) and are self-contained: external stylesheets,
// templates, images and scripts are not supported yet.
struct UiDocumentDefinition {AssetId id;std::string name,purpose,markup,style;std::vector<std::string> surfaces;std::array<int,2> size{1280,720};};
UiDocumentDefinition decode_ui_document(std::string_view json);
// Starting markup/style for New: "hud", "marker", "menu" or "puzzle".
struct UiTemplate {std::string markup,style;std::vector<std::string> surfaces;};
UiTemplate ui_template(std::string_view purpose);
}
