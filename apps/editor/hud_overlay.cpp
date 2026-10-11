#include "hud_overlay.hpp"
#include <algorithm>
#include <stdexcept>
namespace darkangel::editor_app {
namespace {
// RmlUi colours are premultiplied; the editor's ImGui pipeline blends straight alpha.
ImU32 straight(Rml::ColourbPremultiplied colour){
    const unsigned a=colour.alpha;if(!a)return 0;auto channel=[&](unsigned value){return std::min(255u,value*255u/a);};
    return IM_COL32(channel(colour.red),channel(colour.green),channel(colour.blue),a);
}
}
ImGuiHudRenderer::ImGuiHudRenderer(Create create,Release release):create_(std::move(create)),release_(std::move(release)){if(!create_||!release_)throw std::invalid_argument("HUD renderer needs texture callbacks");}
ImGuiHudRenderer::~ImGuiHudRenderer(){for(auto& [handle,texture]:textures_)release_(texture);}
void ImGuiHudRenderer::begin(ImDrawList* draw,ImVec2 origin,ImVec2 size){draw_=draw;origin_=origin;size_=size;scissor_=false;drawn_=0;}
void ImGuiHudRenderer::end(){draw_=nullptr;}
Rml::CompiledGeometryHandle ImGuiHudRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,Rml::Span<const int> indices){
    auto geometry=std::make_unique<Geometry>();geometry->vertices.assign(vertices.begin(),vertices.end());geometry->indices.assign(indices.begin(),indices.end());
    for(int index:geometry->indices)if(index<0||std::size_t(index)>=geometry->vertices.size())throw std::runtime_error("HUD geometry index out of range");
    auto handle=next_geometry_++;geometry_.emplace(handle,std::move(geometry));return handle;
}
void ImGuiHudRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle,Rml::Vector2f translation,Rml::TextureHandle texture){
    auto found=geometry_.find(handle);if(!draw_||found==geometry_.end()||found->second->indices.empty())return;const auto& geometry=*found->second;
    ImVec2 low=origin_,high{origin_.x+size_.x,origin_.y+size_.y};
    if(scissor_){low={std::max(low.x,origin_.x+float(region_.Left())),std::max(low.y,origin_.y+float(region_.Top()))};high={std::min(high.x,origin_.x+float(region_.Right())),std::min(high.y,origin_.y+float(region_.Bottom()))};}
    if(high.x<=low.x||high.y<=low.y)return;
    draw_->PushClipRect(low,high,true);const auto image=texture?textures_.find(texture):textures_.end();const bool textured=image!=textures_.end();if(textured)draw_->PushTexture(ImTextureRef(image->second));
    const ImVec2 white=ImGui::GetFontTexUvWhitePixel();draw_->PrimReserve(int(geometry.indices.size()),int(geometry.vertices.size()));const auto base=draw_->_VtxCurrentIdx;
    for(const auto& vertex:geometry.vertices)draw_->PrimWriteVtx({origin_.x+translation.x+vertex.position.x,origin_.y+translation.y+vertex.position.y},textured?ImVec2{vertex.tex_coord.x,vertex.tex_coord.y}:white,straight(vertex.colour));
    for(int index:geometry.indices)draw_->PrimWriteIdx(ImDrawIdx(base+unsigned(index)));
    if(textured)draw_->PopTexture();draw_->PopClipRect();++drawn_;
}
void ImGuiHudRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle){geometry_.erase(handle);}
// The HUD document uses no image files; decorators that need them are not supported here.
Rml::TextureHandle ImGuiHudRenderer::LoadTexture(Rml::Vector2i&,const Rml::String&){return 0;}
Rml::TextureHandle ImGuiHudRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,Rml::Vector2i dimensions){
    if(dimensions.x<=0||dimensions.y<=0||source.size()!=std::size_t(dimensions.x)*std::size_t(dimensions.y)*4)return 0;
    std::vector<unsigned char> pixels(source.begin(),source.end());
    for(std::size_t i=0;i<pixels.size();i+=4){const unsigned a=pixels[i+3];for(unsigned c=0;c<3;++c)pixels[i+c]=a?static_cast<unsigned char>(std::min(255u,pixels[i+c]*255u/a)):255;}
    auto texture=create_(pixels.data(),dimensions.x,dimensions.y);if(!texture)return 0;auto handle=next_texture_++;textures_.emplace(handle,texture);return handle;
}
void ImGuiHudRenderer::ReleaseTexture(Rml::TextureHandle handle){auto found=textures_.find(handle);if(found==textures_.end())return;release_(found->second);textures_.erase(found);}
void ImGuiHudRenderer::EnableScissorRegion(bool enable){scissor_=enable;}
void ImGuiHudRenderer::SetScissorRegion(Rml::Rectanglei region){region_=region;}
}
