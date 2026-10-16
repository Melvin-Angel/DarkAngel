#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Rml {class RenderInterface;class Context;}
namespace darkangel {
// R6 U0: the process's one RmlUi lifetime, shared by every UI consumer (the Play HUD, the UI tab's preview, later menus and
// world markers). RmlUi keeps process-wide state, so consumers acquire the runtime instead of initialising RmlUi themselves:
// it initialises on the first acquire and shuts down when the last holder releases it. Each consumer owns its own contexts,
// so the editor's preview and a running Play HUD coexist.
//
// Fonts and images are packaged assets, not file paths: a consumer registers the cooked bytes it was given (a font face by
// family, an image by its virtual URL "da://<asset-id>"), and render interfaces resolve image URLs through image(). A
// registration is kept while any holder of the runtime lives.
class UiRuntime {
public:
    static std::shared_ptr<UiRuntime> acquire();
    ~UiRuntime();
    UiRuntime(const UiRuntime&)=delete;
    UiRuntime& operator=(const UiRuntime&)=delete;
    // A context with a name unique in this process. Destroyed with destroy_context (or when the runtime shuts down).
    Rml::Context* create_context(std::string_view purpose,int width,int height,Rml::RenderInterface&);
    void destroy_context(Rml::Context*);
    // Registers a font face once per (family, bytes); the bytes are kept for RmlUi, which reads them in place.
    void register_font(std::string_view family,std::string bytes);
    // Straight-alpha RGBA8 image for a virtual URL ("da://<asset-id>").
    struct Image {int width{},height{};std::vector<unsigned char> rgba;};
    void register_image(std::string url,Image);
    static std::optional<Image> image(std::string_view url);
    // Messages RmlUi reported (warnings and errors) since the last take; loaders check them after each load.
    std::string take_problems();
    // Whether any holder keeps the runtime (and so RmlUi) alive.
    static bool alive();
    struct Impl;
private:
    UiRuntime();
    std::unique_ptr<Impl> impl_;
};
// "da://<asset-id>" for an image asset.
std::string ui_image_url(std::string_view asset_text);
}
