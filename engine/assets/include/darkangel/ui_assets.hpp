#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// UI documents (.daui under ui/). Purpose and permitted surfaces are separate; the attachment that uses a document picks the
// surface. Documents bind the "ui" data model (see UiPreviewSample).
//
// Schema 1 (R6.1) holds the markup and style inline. Schema 2 (R6 U0) is the descriptor of a multi-file document: its owned
// member files (members.markup *.rml and members.style *.rcss, in the descriptor's folder), shared style documents (purpose
// "style": a descriptor with only a style member) applied before its own style, and packaged fonts (.ttf/.otf font assets
// with a family name) and images (colour texture assets) it uses. Markup and style address images by virtual URL
// da://<asset-id> and fonts by family; any other external reference, template or script is refused. Schema 1 documents keep
// working; the UI tab migrates one explicitly.
struct UiDependency {AssetId asset;std::string source,family;};
struct UiDocumentDefinition {
    AssetId id;std::string name,purpose,markup,style,binding{"ui-v1"};std::vector<std::string> surfaces;std::array<int,2> size{1280,720};
    int schema{1};std::string markup_member,style_member,script_member,script;std::vector<UiDependency> styles,fonts,images;
};
// Reads an owned member file by its name in the descriptor's folder (schema 2). Without a reader, member text stays empty
// and only the descriptor is validated.
using UiMemberReader=std::function<std::string(std::string_view member)>;
UiDocumentDefinition decode_ui_document(std::string_view json,const UiMemberReader& members={});
// Checks markup and style: an <rml> document with a <head> (style documents have no markup), no external stylesheet,
// template, script or url() other than da://<id> of a declared image.
void validate_ui_text(const UiDocumentDefinition&);
// Starting markup/style for New: "hud", "marker", "menu", "puzzle" or "style" (a shared stylesheet).
struct UiTemplate {std::string markup,style;std::vector<std::string> surfaces;};
UiTemplate ui_template(std::string_view purpose);
// A cooked UI document: its definition with markup and the composed style (shared styles first), and the packaged fonts
// (family and bytes) and images (virtual URL and RGBA8 pixels of the top mip) it needs, from registry/CAS only.
struct CookedUiDocument {UiDocumentDefinition definition;std::vector<std::pair<std::string,std::string>> fonts;struct Image {std::string url;int width{},height{};std::vector<unsigned char> rgba;};std::vector<Image> images;};
CookedUiDocument load_cooked_ui_document(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
// A packaged font asset (importer font-v1): its bytes.
std::string load_cooked_font(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
