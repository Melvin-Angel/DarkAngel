#pragma once
#include <darkangel/vfx_assets.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
}
namespace darkangel {
// Camera of one draw: row-major view and projection for row vectors (as DirectXMath stores them), right-handed.
struct VfxView {std::array<float,16> view{},projection{};std::array<float,3> eye{},forward{};float near_plane{.1f},far_plane{1000.f};};
// What the service holds and what the latest draw did. unsupported counts batches the renderer skipped because
// they need shading it does not have (lit, distortion, custom materials).
struct VfxStats {std::size_t effects{},playing{},instances{};unsigned draws{},quads{},unsupported{},primitives{};};
// One corner of a plain coloured quad (a weapon trail piece, a decal slice). Four to a quad: 0 and 1 are one edge, 2 and 3 the opposite edge.
struct VfxQuadVertex {std::array<float,3> position{};std::array<float,4> color{};};
// Visual effects: Effekseer simulates, this service draws its sprites, ribbons, rings and tracks through Diligent.
// Client-side presentation only. One service per device; effects are loaded from cooked products and played by
// handle. It decides nothing and is not needed by a headless process.
class VfxService {
public:
    // color and depth are the Diligent texture formats of the targets draw() will be called on.
    VfxService(Diligent::IRenderDevice*,Diligent::IDeviceContext*,int color_format,int depth_format);
    ~VfxService();
    VfxService(const VfxService&)=delete;VfxService& operator=(const VfxService&)=delete;
    // Makes a cooked effect playable, replacing an earlier generation of the same asset. Effects already playing
    // from the earlier generation finish with it.
    void load(const CookedVfx&);
    bool loaded(AssetId)const;
    // Starts an effect at a world position; zero when the asset is not loaded.
    std::uint64_t play(AssetId,std::array<float,3> position);
    void move(std::uint64_t handle,std::array<float,3> position);
    // Stops spawning and lets what is alive finish; kill removes it at once.
    void stop(std::uint64_t handle);
    void kill(std::uint64_t handle);
    bool alive(std::uint64_t handle)const;
    // Advances every playing effect; effects are authored at sixty frames a second.
    void update(double seconds);
    // Draws into the render targets bound on the context, testing against their depth.
    // Plain coloured quads for the next draw(), tested against depth and not written to it; additive or alpha blended. primitives in the stats counts those drawn.
    void quads(std::span<const VfxQuadVertex>,bool additive);
    void draw(const VfxView&);
    VfxStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
