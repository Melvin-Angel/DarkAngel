#include <darkangel/smooth_normals.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel::assets_detail;
struct Vertex { std::array<float,3> position,normal; std::array<float,2> uv{}; unsigned joint=7; };
int main() { try {
    // Two split triangles meeting at 45 degrees, including a UV seam.
    constexpr float n=.70710678f;
    std::vector<Vertex> vertices = {
        {{0,0,0},{0,1,0}}, {{0,0,1},{0,1,0}}, {{1,0,0},{0,1,0}},
        {{0,0,0},{n,n,0},{1,1}}, {{0,0,1},{n,n,0}}, {{-1,1,0},{n,n,0}},
        {{0,0,0},{0,0,1}}, {{1,0,0},{0,0,1}}, {{0,1,0},{0,0,1}}
    };
    const auto original=vertices;
    const std::vector<std::uint32_t> indices{0,1,2,3,4,5,6,7,8};
    smooth_normals(vertices,indices);
    if (vertices[0].normal!=vertices[3].normal || vertices[0].normal[0]<.3f || vertices[0].normal[1]<.8f)
        throw std::runtime_error("Curved split/UV normals not smoothed");
    if(vertices[6].normal!=original[6].normal) throw std::runtime_error("Architectural crease was rounded");
    auto organic=original;
    smooth_normals(organic,indices,-1.);
    if(organic[0].normal!=organic[6].normal)throw std::runtime_error("Character profile retained flat-shaded creases");
    for(std::size_t i=0;i<vertices.size();++i){
        const auto& a=vertices[i]; const auto& b=original[i];
        if(a.position!=b.position||a.uv!=b.uv||a.joint!=b.joint)throw std::runtime_error("Smoothing changed geometry/UV/skin stream");
        double length=0;for(float value:a.normal)length+=value*value;
        if(std::abs(length-1)>1e-5)throw std::runtime_error("Smooth normal is not unit length");
    }
    std::cout<<"Smooth normals: curved seams, hard creases and geometry/UV/skin preservation pass\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;} }
