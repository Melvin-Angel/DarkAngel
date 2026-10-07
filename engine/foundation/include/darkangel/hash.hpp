#pragma once
#include <filesystem>
#include <span>
#include <string>
namespace darkangel {
std::string sha256(std::span<const std::byte> bytes);
std::string sha256(std::string_view bytes);
std::string file_sha256(const std::filesystem::path&,std::size_t limit=64*1024*1024);
}
