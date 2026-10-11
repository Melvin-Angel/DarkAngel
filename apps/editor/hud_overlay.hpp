#pragma once
#include <RmlUi/Core/RenderInterface.h>
#include <imgui.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
namespace darkangel::editor_app {
// Editor host adapter: RmlUi geometry is appended to the Game viewport's ImGui draw list,
// so the HUD is composited by the renderer the editor already has. Textures (font atlases)
// are created through the host's device callbacks. Presentation only.
class ImGuiHudRenderer final:public Rml::RenderInterface {
public:
    using Create=std::function<ImTextureID(const unsigned char* rgba,int width,int height)>;
    using Release=std::function<void(ImTextureID)>;
    ImGuiHudRenderer(Create,Release);
    ~ImGuiHudRenderer()override;
    // Target for the next Hud::render(): a draw list and the viewport rectangle in screen pixels.
    void begin(ImDrawList*,ImVec2 origin,ImVec2 size);
    void end();
    unsigned drawn()const{return drawn_;}
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,Rml::Span<const int>)override;
    void RenderGeometry(Rml::CompiledGeometryHandle,Rml::Vector2f,Rml::TextureHandle)override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle)override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i&,const Rml::String&)override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,Rml::Vector2i)override;
    void ReleaseTexture(Rml::TextureHandle)override;
    void EnableScissorRegion(bool)override;
    void SetScissorRegion(Rml::Rectanglei)override;
private:
    struct Geometry {std::vector<Rml::Vertex> vertices;std::vector<int> indices;};
    Create create_;Release release_;std::map<Rml::CompiledGeometryHandle,std::unique_ptr<Geometry>> geometry_;std::map<Rml::TextureHandle,ImTextureID> textures_;
    Rml::CompiledGeometryHandle next_geometry_{1};Rml::TextureHandle next_texture_{1};
    ImDrawList* draw_{};ImVec2 origin_{},size_{};bool scissor_{};Rml::Rectanglei region_{};unsigned drawn_{};
};
}
