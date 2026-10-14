#include "render_system.hpp"
#include "../native_input.hpp"
#include <EngineFactoryD3D12.h>
#include <EngineFactoryVk.h>
#include <ImGuiImplWin32.hpp>
#include <imgui.h>
#include <darkangel/check.hpp>

using namespace Diligent;
namespace darkangel::editor_app {
namespace {
RenderSystem* active{};
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (active && active->input)
        active->input->message(message, wparam, lparam);
    if (active && active->gui && active->gui->Win32_ProcHandler(window, message, wparam, lparam))
        return 1;
    if (message == WM_SIZE) {
        if (active) {
            active->resize_width = LOWORD(lparam);
            active->resize_height = HIWORD(lparam);
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

RenderSystem::RenderSystem(unsigned width, unsigned height, const std::string& backend) {
    require(!active, "One editor render system per process");
    active = this;
    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DarkAngelM2Editor";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    require(RegisterClassW(&wc), "Window class registration failed");
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowW(wc.lpszClassName, L"DarkAngel Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                           rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);
    require(window, "Editor window creation failed");
    RECT initial_client{};
    require(GetClientRect(window, &initial_client) && initial_client.right > 0 && initial_client.bottom > 0, "Editor client size unavailable");
    swap_desc.Width = static_cast<unsigned>(initial_client.right);
    swap_desc.Height = static_cast<unsigned>(initial_client.bottom);
    swap_desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
    swap_desc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;
    NativeWindow native(window);
    if (backend == "d3d12") {
        auto* factory = GetEngineFactoryD3D12();
        EngineD3D12CreateInfo ci;
        ci.pDxCompilerPath = DAE_DXC_DLL;
        ci.EnableValidation = true;
        factory->CreateDeviceAndContextsD3D12(ci, &device, &context);
        require(device && context, "D3D12 device initialization failed");
        factory->CreateSwapChainD3D12(device, context, swap_desc, FullScreenModeDesc{}, native, &swap);
    } else {
        auto* factory = GetEngineFactoryVk();
        EngineVkCreateInfo ci;
        ci.pDxCompilerPath = DAE_DXC_DLL;
        ci.EnableValidation = true;
        factory->CreateDeviceAndContextsVk(ci, &device, &context);
        require(device && context, "Vulkan device initialization failed");
        factory->CreateSwapChainVk(device, context, swap_desc, native, &swap);
    }
    require(swap, "Swap chain creation failed");
    gui = ImGuiImplWin32::Create(ImGuiDiligentCreateInfo{device, swap->GetDesc()}, window);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().IniFilename = nullptr;
}

RenderSystem::~RenderSystem() {
    // An exception that skipped shutdown(): release what main's locals would have released, in the same order.
    input = nullptr;
    pso_cull.Release();
    pso.Release();
    constant_buffer.Release();
    gui.reset();
    active = nullptr;
}

void RenderSystem::create_frame_pipelines() {
    constants_desc.Name = "DarkAngel frame constants";
    constants_desc.Size = sizeof(render::Constants);
    constants_desc.Usage = USAGE_DYNAMIC;
    constants_desc.BindFlags = BIND_UNIFORM_BUFFER;
    constants_desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    device->CreateBuffer(constants_desc, nullptr, &constant_buffer);
    require(constant_buffer, "Constant buffer creation failed");
    auto source = render::shader_text();
    pso = render::pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, false, source.c_str());
    pso_cull = render::pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, true, source.c_str());
}

void RenderSystem::shutdown() {
    context->Flush();
    context->WaitForIdle();
    gui.reset();
    if (IsWindow(window))
        DestroyWindow(window);
}
} // namespace darkangel::editor_app
