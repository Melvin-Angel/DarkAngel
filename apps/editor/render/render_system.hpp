#pragma once
#define NOMINMAX
#define UNICODE
#include <Windows.h>
#include "gpu_resources.hpp"
#include <SwapChain.h>
#include <memory>
#include <string>

namespace Diligent {
class ImGuiImplWin32;
}
namespace darkangel::editor_app {
class NativeInput;
// Owns the editor window, the graphics device, context and swap chain, the ImGui backend, the frame constants and the
// stylized prop pipelines. Made first and shut down last by main; everything that holds device resources lives between.
class RenderSystem {
  public:
    RenderSystem(unsigned width, unsigned height, const std::string& backend);
    ~RenderSystem();
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    // The frame constants buffer and both prop pipelines (with and without back-face culling).
    void create_frame_pipelines();
    // Waits for the GPU, releases the ImGui backend and destroys the window. Called once, after the frame loop.
    void shutdown();
    // Dispatches this frame's window messages; false once the window asked to quit.
    bool pump_messages();
    // Follows the window's client size: the swap chain is resized when it changed.
    void follow_window_size();
    // Binds and clears the back buffer and draws the ImGui frame into it; returns the back buffer.
    Diligent::ITextureView* draw_ui(const float* background);
    void present(bool hidden);

    HWND window{};
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swap;
    Diligent::SwapChainDesc swap_desc;
    std::unique_ptr<Diligent::ImGuiImplWin32> gui;
    // Description of the frame constants buffer; other uniform buffers start from a copy.
    Diligent::BufferDesc constants_desc;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constant_buffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso, pso_cull;
    // Their transparent (blending) variants, R5 A3; same resource layout, so a material's binding serves all four.
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso_blend, pso_blend_cull;
    // Window messages: the native input adapter that receives them while Play has one, and a pending client resize.
    NativeInput* input{};
    unsigned resize_width{}, resize_height{};
};
} // namespace darkangel::editor_app
